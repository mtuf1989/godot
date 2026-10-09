/**************************************************************************/
/*  symphony_music_transport.h                                            */
/**************************************************************************/
/* Musical tick ↔ sample mapping for procedural music (N1).               */
/* 960 PPQN ticks; tempo steps and linear BPM ramps; meter at bar origin. */
/**************************************************************************/

#pragma once

#include "core/math/math_funcs.h"
#include "core/templates/local_vector.h"

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
		uint32_t _pad = 0; // SeqLock 4-byte alignment
	};
	static_assert(sizeof(Snapshot) % 4 == 0, "Snapshot must be SeqLock-compatible");

private:
	float sample_rate = 48000.0f;
	LocalVector<TempoSegment> tempo;
	LocalVector<MeterChange> meters;
	bool paused = false;

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
		// bpm(u) = b0 + (b1-b0)*u/span; ∫ du/bpm = (span/(b1-b0)) * ln(bpm)
		const double ba = MAX(b0 + (b1 - b0) * (u0 / span), 1e-9);
		const double bb = MAX(b0 + (b1 - b0) * (u1 / span), 1e-9);
		return k * (span / (b1 - b0)) * Math::log(bb / ba);
	}

	int _tempo_index_at_tick(int64_t p_tick) const {
		int idx = 0;
		for (int i = 0; i < (int)tempo.size(); i++) {
			if (tempo[i].start_tick <= p_tick) {
				idx = i;
			} else {
				break;
			}
		}
		return idx;
	}

	int _meter_index_at_tick(int64_t p_tick) const {
		int idx = 0;
		for (int i = 0; i < (int)meters.size(); i++) {
			if (meters[i].start_tick <= p_tick) {
				idx = i;
			} else {
				break;
			}
		}
		return idx;
	}

public:
	void reset(float p_sample_rate, float p_bpm = 120.0f, int p_beats_per_bar = 4, int p_beat_unit = 4) {
		sample_rate = p_sample_rate > 0.0f ? p_sample_rate : 48000.0f;
		tempo.clear();
		meters.clear();
		TempoSegment seg;
		seg.start_tick = 0;
		seg.bpm_start = _clamp_bpm(p_bpm);
		seg.bpm_end = seg.bpm_start;
		seg.end_tick = INT64_MAX;
		tempo.push_back(seg);
		MeterChange m;
		m.start_tick = 0;
		m.beats_per_bar = CLAMP(p_beats_per_bar, 1, 32);
		m.beat_unit = CLAMP(p_beat_unit, 1, 16);
		meters.push_back(m);
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

	// Close the open-ended segment at p_start_tick and append a step.
	bool schedule_tempo_step(float p_bpm, int64_t p_start_tick) {
		if (tempo.size() >= MAX_TEMPO_SEGMENTS || p_start_tick < 0) {
			return false;
		}
		const float bpm = _clamp_bpm(p_bpm);
		if (tempo.is_empty()) {
			reset(sample_rate, bpm);
			return true;
		}
		if (p_start_tick <= tempo[tempo.size() - 1].start_tick && tempo.size() > 1) {
			return false; // must be non-decreasing
		}
		TempoSegment &last = tempo[tempo.size() - 1];
		if (p_start_tick < last.start_tick) {
			return false;
		}
		last.end_tick = p_start_tick;
		TempoSegment seg;
		seg.start_tick = p_start_tick;
		seg.bpm_start = bpm;
		seg.bpm_end = bpm;
		seg.end_tick = INT64_MAX;
		tempo.push_back(seg);
		return true;
	}

	// Linear BPM ramp over [p_start_tick, p_start_tick + p_ramp_ticks).
	bool schedule_tempo_ramp(float p_bpm_end, int64_t p_start_tick, int64_t p_ramp_ticks) {
		if (tempo.size() + 1 >= MAX_TEMPO_SEGMENTS || p_ramp_ticks <= 0 || p_start_tick < 0) {
			return false;
		}
		const float bpm_end = _clamp_bpm(p_bpm_end);
		if (tempo.is_empty()) {
			reset(sample_rate, bpm_end);
		}
		const float bpm_start = bpm_at_tick(p_start_tick);
		TempoSegment &last = tempo[tempo.size() - 1];
		if (p_start_tick < last.start_tick) {
			return false;
		}
		last.end_tick = p_start_tick;
		TempoSegment ramp;
		ramp.start_tick = p_start_tick;
		ramp.bpm_start = bpm_start;
		ramp.bpm_end = bpm_end;
		ramp.end_tick = p_start_tick + p_ramp_ticks;
		tempo.push_back(ramp);
		TempoSegment after;
		after.start_tick = ramp.end_tick;
		after.bpm_start = bpm_end;
		after.bpm_end = bpm_end;
		after.end_tick = INT64_MAX;
		tempo.push_back(after);
		return true;
	}

	bool schedule_meter(int p_beats_per_bar, int p_beat_unit, int64_t p_start_tick) {
		if (meters.size() >= MAX_METER_CHANGES || p_start_tick < 0) {
			return false;
		}
		if (!meters.is_empty() && p_start_tick < meters[meters.size() - 1].start_tick) {
			return false;
		}
		MeterChange m;
		m.start_tick = p_start_tick;
		m.beats_per_bar = CLAMP(p_beats_per_bar, 1, 32);
		m.beat_unit = CLAMP(p_beat_unit, 1, 16);
		meters.push_back(m);
		return true;
	}

	[[nodiscard]] float bpm_at_tick(int64_t p_tick) const {
		if (tempo.is_empty()) {
			return 120.0f;
		}
		const TempoSegment &seg = tempo[_tempo_index_at_tick(p_tick)];
		if (Math::abs(seg.bpm_end - seg.bpm_start) < 1e-9f || seg.end_tick <= seg.start_tick) {
			return seg.bpm_start;
		}
		const double span = (double)(seg.end_tick - seg.start_tick);
		const double u = CLAMP((double)(p_tick - seg.start_tick), 0.0, span);
		return (float)((double)seg.bpm_start + ((double)seg.bpm_end - (double)seg.bpm_start) * (u / span));
	}

	// Absolute sample index for a tick (from tick 0 / frame 0).
	[[nodiscard]] uint64_t tick_to_frame(int64_t p_tick) const {
		if (p_tick <= 0) {
			return 0;
		}
		double seconds = 0.0;
		int64_t cursor = 0;
		for (int i = 0; i < (int)tempo.size() && cursor < p_tick; i++) {
			seconds += _segment_seconds(tempo[i], cursor, p_tick);
			cursor = MIN(p_tick, tempo[i].end_tick);
		}
		const double frames = seconds * (double)sample_rate;
		if (frames <= 0.0) {
			return 0;
		}
		if (frames >= (double)UINT64_MAX) {
			return UINT64_MAX;
		}
		return (uint64_t)Math::floor(frames + 1e-9);
	}

	// Inverse: largest tick whose frame ≤ p_frame (binary search).
	[[nodiscard]] int64_t frame_to_tick(uint64_t p_frame) const {
		if (p_frame == 0) {
			return 0;
		}
		int64_t lo = 0;
		int64_t hi = MAX((int64_t)1, (int64_t)((double)p_frame * (double)PPQN * 999.0 / (60.0 * (double)sample_rate)) + PPQN * 16);
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
		const MeterChange &m = meters.is_empty() ? MeterChange{} : meters[_meter_index_at_tick(s.tick)];
		s.beats_per_bar = m.beats_per_bar;
		s.beat_unit = m.beat_unit;
		// Beat length in ticks: PPQN * (4/beat_unit) for the beat unit.
		const int64_t ticks_per_beat = (PPQN * 4) / MAX(1, m.beat_unit);
		const int64_t ticks_per_bar = ticks_per_beat * MAX(1, m.beats_per_bar);
		const int64_t rel = MAX((int64_t)0, s.tick - m.start_tick);
		s.bar_index = (int32_t)(rel / MAX((int64_t)1, ticks_per_bar));
		const int64_t in_bar = rel % MAX((int64_t)1, ticks_per_bar);
		s.beat_in_bar = (float)in_bar / (float)MAX((int64_t)1, ticks_per_beat);
		return s;
	}

	// Next event-boundary frame after p_from_frame among candidate ticks (sorted).
	// Returns UINT64_MAX if none in range.
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
