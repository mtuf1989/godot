/**************************************************************************/
/*  symphony_music_transport.h                                            */
/**************************************************************************/
/* Musical tick ↔ sample mapping for procedural music (N1 / B1).          */
/* 960 PPQN ticks; tempo steps and linear BPM ramps; meter at bar origin. */
/* Fixed arrays, elapsed-segment folding, reject past-frame tempo.        */
/**************************************************************************/

#pragma once

#include "core/math/math_funcs.h"

#include <cstdint>

// Continuous, bidirectional tick/sample mapping. Changing BPM does not rescale
// elapsed history: each tempo segment is absolute from its start tick.
class SymphonyMusicTransport {
public:
	static constexpr int64_t PPQN = 960; // ticks per quarter note
	static constexpr int MAX_TEMPO_SEGMENTS = 64;
	static constexpr int MAX_METER_CHANGES = 16;

	struct TempoSegment {
		int64_t start_tick = 0;
		float bpm_start = 120.0f;
		float bpm_end = 120.0f; // equal → step; unequal → linear ramp over [start, end_tick)
		int64_t end_tick = INT64_MAX; // exclusive; INT64_MAX = open-ended
	};

	struct MeterChange {
		int64_t start_tick = 0; // phrase/bar boundary
		int beats_per_bar = 4;
		int beat_unit = 4; // denominator (4 = quarter)
	};

	struct Snapshot {
		int64_t tick = 0;
		uint64_t frame = 0;
		float bpm = 120.0f;
		int32_t beats_per_bar = 4;
		int32_t beat_unit = 4;
		int32_t bar_index = 0;
		float beat_in_bar = 0.0f;
		int32_t generation = 0;
		float sample_rate = 48000.0f;
		uint32_t _pad = 0; // SeqLock 4-byte alignment
	};
	static_assert(sizeof(Snapshot) == 48, "Snapshot is 48 bytes");
	static_assert(sizeof(Snapshot) % 4 == 0, "Snapshot must be SeqLock-compatible");

private:
	float sample_rate = 48000.0f;
	TempoSegment tempo_segs[MAX_TEMPO_SEGMENTS];
	int tempo_count = 0;
	MeterChange meter_segs[MAX_METER_CHANGES];
	int meter_count = 0;
	bool paused = false;
	uint64_t rendered_frame = 0;
	int32_t snapshot_generation = 0;
	int64_t anchor_tick = 0;
	double anchor_seconds = 0.0;

	static float _clamp_bpm(float p_bpm) {
		return CLAMP(p_bpm, 1.0f, 999.0f);
	}

	// Seconds spanned by ticks [t0, t1) inside one segment (t1 may exceed end).
	static double _segment_seconds(const TempoSegment &p_seg, int64_t p_t0, int64_t p_t1) {
		const int64_t a = MAX(p_t0, p_seg.start_tick);
		const int64_t b = MIN(p_t1, p_seg.end_tick);
		if (b <= a) {
			return 0.0;
		}
		const double span = (double)(p_seg.end_tick - p_seg.start_tick);
		const double u0 = (double)(a - p_seg.start_tick);
		const double u1 = (double)(b - p_seg.start_tick);
		const double b0 = (double)p_seg.bpm_start;
		const double b1 = (double)p_seg.bpm_end;
		const double k = 60.0 / (double)PPQN;
		if (Math::abs(b1 - b0) < 1e-9 || span <= 0.0) {
			return k * (u1 - u0) / MAX(b0, 1e-9);
		}
		const double ba = MAX(b0 + (b1 - b0) * (u0 / span), 1e-9);
		const double bb = MAX(b0 + (b1 - b0) * (u1 / span), 1e-9);
		return k * (span / (b1 - b0)) * Math::log(bb / ba);
	}

	int _tempo_index_at_tick(int64_t p_tick) const {
		int idx = 0;
		for (int i = 0; i < tempo_count; i++) {
			if (tempo_segs[i].start_tick <= p_tick) {
				idx = i;
			} else {
				break;
			}
		}
		return idx;
	}

	int _meter_index_at_tick(int64_t p_tick) const {
		int idx = 0;
		for (int i = 0; i < meter_count; i++) {
			if (meter_segs[i].start_tick <= p_tick) {
				idx = i;
			} else {
				break;
			}
		}
		return idx;
	}

	[[nodiscard]] uint64_t _seconds_to_frame(double p_seconds) const {
		const double frames = p_seconds * (double)sample_rate;
		if (frames <= 0.0) {
			return 0;
		}
		if (frames >= (double)UINT64_MAX) {
			return UINT64_MAX;
		}
		return (uint64_t)Math::floor(frames + 1e-9);
	}

	[[nodiscard]] bool _is_past_frame(int64_t p_start_tick) const {
		if (p_start_tick <= 0 || rendered_frame == 0) {
			return false;
		}
		return tick_to_frame(p_start_tick) < rendered_frame;
	}

	void _fold_elapsed() {
		while (tempo_count > 1) {
			TempoSegment &first = tempo_segs[0];
			if (first.end_tick == INT64_MAX) {
				break;
			}
			const double end_seconds = anchor_seconds + _segment_seconds(first, MAX(first.start_tick, anchor_tick), first.end_tick);
			if (_seconds_to_frame(end_seconds) >= rendered_frame) {
				break;
			}
			anchor_tick = first.end_tick;
			anchor_seconds = end_seconds;
			for (int i = 1; i < tempo_count; i++) {
				tempo_segs[i - 1] = tempo_segs[i];
			}
			tempo_count--;
		}
		int meter_keep = 0;
		for (int i = 0; i < meter_count; i++) {
			if (i == meter_count - 1 || meter_segs[i + 1].start_tick > anchor_tick) {
				if (meter_keep != i) {
					meter_segs[meter_keep] = meter_segs[i];
				}
				meter_keep++;
			}
		}
		meter_count = MAX(meter_keep, 1);
	}

public:
	void reset(float p_sample_rate, float p_bpm = 120.0f, int p_beats_per_bar = 4, int p_beat_unit = 4) {
		sample_rate = p_sample_rate > 0.0f ? p_sample_rate : 48000.0f;
		tempo_count = 0;
		meter_count = 0;
		rendered_frame = 0;
		snapshot_generation = 0;
		anchor_tick = 0;
		anchor_seconds = 0.0;
		TempoSegment seg;
		seg.start_tick = 0;
		seg.bpm_start = _clamp_bpm(p_bpm);
		seg.bpm_end = seg.bpm_start;
		seg.end_tick = INT64_MAX;
		tempo_segs[tempo_count++] = seg;
		MeterChange m;
		m.start_tick = 0;
		m.beats_per_bar = CLAMP(p_beats_per_bar, 1, 32);
		m.beat_unit = CLAMP(p_beat_unit, 1, 16);
		meter_segs[meter_count++] = m;
		paused = false;
	}

	void set_sample_rate(float p_sr) {
		if (p_sr > 0.0f) {
			sample_rate = p_sr;
		}
	}

	[[nodiscard]] float get_sample_rate() const { return sample_rate; }
	void set_paused(bool p_paused) { paused = p_paused; }
	[[nodiscard]] bool is_paused() const { return paused; }

	void set_rendered_frame(uint64_t p_frame) {
		rendered_frame = p_frame;
		_fold_elapsed();
	}

	void set_snapshot_generation(int32_t p_generation) {
		snapshot_generation = p_generation;
	}

	[[nodiscard]] int segment_count() const { return tempo_count; }
	[[nodiscard]] uint64_t get_rendered_frame() const { return rendered_frame; }

	// Close the open-ended segment at p_start_tick and append a step.
	[[nodiscard]] bool schedule_tempo_step(float p_bpm, int64_t p_start_tick) {
		if (p_start_tick < 0 || _is_past_frame(p_start_tick)) {
			return false;
		}
		_fold_elapsed();
		if (tempo_count >= MAX_TEMPO_SEGMENTS) {
			return false;
		}
		const float bpm = _clamp_bpm(p_bpm);
		if (tempo_count == 0) {
			reset(sample_rate, bpm);
			return true;
		}
		TempoSegment &last = tempo_segs[tempo_count - 1];
		if (p_start_tick < last.start_tick) {
			return false;
		}
		last.end_tick = p_start_tick;
		TempoSegment seg;
		seg.start_tick = p_start_tick;
		seg.bpm_start = bpm;
		seg.bpm_end = bpm;
		seg.end_tick = INT64_MAX;
		tempo_segs[tempo_count++] = seg;
		return true;
	}

	// Linear BPM ramp over [p_start_tick, p_start_tick + p_ramp_ticks).
	[[nodiscard]] bool schedule_tempo_ramp(float p_bpm_end, int64_t p_start_tick, int64_t p_ramp_ticks) {
		if (p_ramp_ticks <= 0 || p_start_tick < 0 || _is_past_frame(p_start_tick)) {
			return false;
		}
		_fold_elapsed();
		if (tempo_count + 1 >= MAX_TEMPO_SEGMENTS) {
			return false;
		}
		const float bpm_end = _clamp_bpm(p_bpm_end);
		if (tempo_count == 0) {
			reset(sample_rate, bpm_end);
		}
		const float bpm_start = bpm_at_tick(p_start_tick);
		TempoSegment &last = tempo_segs[tempo_count - 1];
		if (p_start_tick < last.start_tick) {
			return false;
		}
		last.end_tick = p_start_tick;
		TempoSegment ramp;
		ramp.start_tick = p_start_tick;
		ramp.bpm_start = bpm_start;
		ramp.bpm_end = bpm_end;
		ramp.end_tick = p_start_tick + p_ramp_ticks;
		tempo_segs[tempo_count++] = ramp;
		TempoSegment after;
		after.start_tick = ramp.end_tick;
		after.bpm_start = bpm_end;
		after.bpm_end = bpm_end;
		after.end_tick = INT64_MAX;
		tempo_segs[tempo_count++] = after;
		return true;
	}

	[[nodiscard]] bool schedule_meter(int p_beats_per_bar, int p_beat_unit, int64_t p_start_tick) {
		if (p_start_tick < 0 || _is_past_frame(p_start_tick)) {
			return false;
		}
		_fold_elapsed();
		if (meter_count >= MAX_METER_CHANGES) {
			return false;
		}
		if (meter_count > 0 && p_start_tick < meter_segs[meter_count - 1].start_tick) {
			return false;
		}
		MeterChange m;
		m.start_tick = p_start_tick;
		m.beats_per_bar = CLAMP(p_beats_per_bar, 1, 32);
		m.beat_unit = CLAMP(p_beat_unit, 1, 16);
		meter_segs[meter_count++] = m;
		return true;
	}

	[[nodiscard]] float bpm_at_tick(int64_t p_tick) const {
		if (tempo_count == 0) {
			return 120.0f;
		}
		const TempoSegment &seg = tempo_segs[_tempo_index_at_tick(MAX(p_tick, anchor_tick))];
		if (Math::abs(seg.bpm_end - seg.bpm_start) < 1e-9f || seg.end_tick <= seg.start_tick) {
			return seg.bpm_start;
		}
		const double span = (double)(seg.end_tick - seg.start_tick);
		const double u = CLAMP((double)(p_tick - seg.start_tick), 0.0, span);
		return (float)((double)seg.bpm_start + ((double)seg.bpm_end - (double)seg.bpm_start) * (u / span));
	}

	// Absolute sample index for a tick (from tick 0 / frame 0).
	[[nodiscard]] uint64_t tick_to_frame(int64_t p_tick) const {
		if (p_tick <= anchor_tick) {
			return _seconds_to_frame(anchor_seconds);
		}
		double seconds = anchor_seconds;
		int64_t cursor = anchor_tick;
		for (int i = 0; i < tempo_count && cursor < p_tick; i++) {
			seconds += _segment_seconds(tempo_segs[i], cursor, p_tick);
			cursor = MIN(p_tick, tempo_segs[i].end_tick);
		}
		return _seconds_to_frame(seconds);
	}

	// Inverse: largest tick whose frame ≤ p_frame (binary search from the anchor).
	[[nodiscard]] int64_t frame_to_tick(uint64_t p_frame) const {
		const uint64_t anchor_frame = _seconds_to_frame(anchor_seconds);
		if (p_frame <= anchor_frame) {
			return anchor_tick;
		}
		int64_t lo = anchor_tick;
		int64_t hi = MAX(lo + 1, (int64_t)((double)p_frame * (double)PPQN * 999.0 / (60.0 * (double)sample_rate)) + PPQN * 16);
		while (tick_to_frame(hi) < p_frame && hi < INT64_MAX / 2) {
			hi *= 2;
		}
		while (lo < hi) {
			const int64_t mid = lo + (hi - lo + 1) / 2;
			if (tick_to_frame(mid) <= p_frame) {
				lo = mid;
			} else {
				hi = mid - 1;
			}
		}
		return lo;
	}

	[[nodiscard]] Snapshot snapshot_at_frame(uint64_t p_frame) const {
		Snapshot s;
		s.frame = p_frame;
		s.tick = frame_to_tick(p_frame);
		s.bpm = bpm_at_tick(s.tick);
		const MeterChange &m = meter_count == 0 ? MeterChange{} : meter_segs[_meter_index_at_tick(s.tick)];
		s.beats_per_bar = m.beats_per_bar;
		s.beat_unit = m.beat_unit;
		const int64_t ticks_per_beat = (PPQN * 4) / MAX(1, m.beat_unit);
		const int64_t ticks_per_bar = ticks_per_beat * MAX(1, m.beats_per_bar);
		const int64_t rel = MAX((int64_t)0, s.tick - m.start_tick);
		s.bar_index = (int32_t)(rel / MAX((int64_t)1, ticks_per_bar));
		const int64_t in_bar = rel % MAX((int64_t)1, ticks_per_bar);
		s.beat_in_bar = (float)in_bar / (float)MAX((int64_t)1, ticks_per_beat);
		s.generation = snapshot_generation;
		s.sample_rate = sample_rate;
		return s;
	}

	[[nodiscard]] uint64_t next_boundary_frame(uint64_t p_from_frame, const int64_t *p_ticks, int p_count) const {
		uint64_t best = UINT64_MAX;
		for (int i = 0; i < p_count; i++) {
			const uint64_t f = tick_to_frame(p_ticks[i]);
			if (f > p_from_frame && f < best) {
				best = f;
			}
		}
		return best;
	}
};
