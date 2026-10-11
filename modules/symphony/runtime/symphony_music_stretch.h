/**************************************************************************/
/*  symphony_music_stretch.h                                              */
/**************************************************************************/
/* Signalsmith Stretch context pool for recorded music phrases (N3).      */
/* Configure / seek / prime on main; process exclusively on audio.        */
/**************************************************************************/

#pragma once

#include "modules/symphony/thirdparty/signalsmith-stretch/signalsmith-stretch.h"

#include "core/error/error_macros.h"
#include "core/math/math_funcs.h"
#include "core/os/memory.h"
#include "core/string/ustring.h"
#include "servers/audio/audio_frame.h"

#include <atomic>
#include <cstdint>
#include <vector>

// Ownership: Free → Prepared (main) → Active (audio) → Returning (audio ack) → Free (main).
enum class SymphonyStretchOwnership : uint8_t {
	Free = 0,
	Prepared = 1,
	Active = 2,
	Returning = 3,
};

struct SymphonyMusicClipDesc {
	StringName pcm_key;
	const float *pcm = nullptr; // interleaved stereo from stereo cache
	int32_t frames = 0;
	float sample_rate = 48000.0f;
	float source_bpm = 120.0f;
	int32_t body_start = 0;
	int32_t body_end = 0; // exclusive; 0 → frames
	int32_t lead_samples = 0;
	int32_t tail_samples = 0;
	float tempo_ratio_min = 0.8f;
	float tempo_ratio_max = 1.25f;
	int32_t transpose_min_st = -6;
	int32_t transpose_max_st = 6;
};

class SymphonyStretchContext {
public:
	static constexpr int CHANNELS = 2;

	signalsmith::stretch::SignalsmithStretch<float> stretch;
	std::atomic<uint8_t> ownership{ (uint8_t)SymphonyStretchOwnership::Free };
	SymphonyMusicClipDesc clip;
	float tempo_ratio = 1.0f;
	float transpose_st = 0.0f;
	double source_pos = 0.0; // fractional source frame
	bool finished = false;
	int32_t common_latency = 0; // inputLatency + outputLatency at configure time

	// Scratch channel pointers for process() (audio thread).
	std::vector<float> in_l;
	std::vector<float> in_r;
	std::vector<float> out_l;
	std::vector<float> out_r;
	float *in_ptrs[CHANNELS] = {};
	float *out_ptrs[CHANNELS] = {};

	[[nodiscard]] SymphonyStretchOwnership get_ownership() const {
		return (SymphonyStretchOwnership)ownership.load(std::memory_order_acquire);
	}
	void set_ownership(SymphonyStretchOwnership p_state) {
		ownership.store((uint8_t)p_state, std::memory_order_release);
	}
	[[nodiscard]] bool is_free() const { return get_ownership() == SymphonyStretchOwnership::Free; }

	// Main thread: configure stretch for music rate with split computation.
	bool prepare(const SymphonyMusicClipDesc &p_clip, float p_tempo_ratio, float p_transpose_st) {
		ERR_FAIL_COND_V(get_ownership() == SymphonyStretchOwnership::Active, false);
		ERR_FAIL_COND_V(p_clip.pcm == nullptr || p_clip.frames <= 0, false);

		tempo_ratio = CLAMP(p_tempo_ratio, p_clip.tempo_ratio_min, p_clip.tempo_ratio_max);
		transpose_st = CLAMP(p_transpose_st, (float)p_clip.transpose_min_st, (float)p_clip.transpose_max_st);
		clip = p_clip;
		if (clip.body_end <= 0 || clip.body_end > clip.frames) {
			clip.body_end = clip.frames;
		}
		clip.body_start = CLAMP(clip.body_start, 0, clip.frames);
		clip.body_end = CLAMP(clip.body_end, clip.body_start, clip.frames);

		stretch.presetDefault(CHANNELS, clip.sample_rate, true /* splitComputation */);
		stretch.setTransposeSemitones(transpose_st);
		stretch.reset();

		common_latency = stretch.inputLatency() + stretch.outputLatency();
		const int pre = stretch.inputLatency();
		_ensure_scratch(MAX(pre, 64));

		// Seek: provide first inputLatency samples starting at body_start (with lead).
		const int32_t start = MAX(0, clip.body_start - clip.lead_samples);
		_fill_input_from_source(start, pre);
		stretch.seek(in_ptrs, pre, tempo_ratio);
		source_pos = (double)start + (double)pre;
		finished = false;
		set_ownership(SymphonyStretchOwnership::Prepared);
		return true;
	}

	// Audio thread: claim a Prepared context.
	bool activate() {
		uint8_t expected = (uint8_t)SymphonyStretchOwnership::Prepared;
		return ownership.compare_exchange_strong(expected, (uint8_t)SymphonyStretchOwnership::Active, std::memory_order_acq_rel, std::memory_order_acquire);
	}

	// Audio thread: mark for main-thread reclaim after playback ends.
	void request_return() {
		uint8_t expected = (uint8_t)SymphonyStretchOwnership::Active;
		ownership.compare_exchange_strong(expected, (uint8_t)SymphonyStretchOwnership::Returning, std::memory_order_release, std::memory_order_relaxed);
	}

	// Main thread: free after audio acknowledged return.
	void reclaim() {
		ERR_FAIL_COND(get_ownership() == SymphonyStretchOwnership::Active);
		set_ownership(SymphonyStretchOwnership::Free);
		clip = {};
		finished = false;
		source_pos = 0.0;
	}

	// Audio thread: render p_frames of stretched audio into p_out (additive).
	void process_add(AudioFrame *p_out, int p_frames) {
		if (get_ownership() != SymphonyStretchOwnership::Active || finished || !clip.pcm || p_frames <= 0) {
			return;
		}
		_ensure_scratch(p_frames);

		// Source samples needed for average tempo_ratio.
		const int in_n = MAX(1, (int)Math::floor((double)p_frames / (double)tempo_ratio + 1e-9));
		_ensure_scratch(MAX(in_n, p_frames));
		_fill_input_from_source((int32_t)source_pos, in_n);
		source_pos += (double)in_n;

		for (int i = 0; i < p_frames; i++) {
			out_l[i] = 0.0f;
			out_r[i] = 0.0f;
		}
		stretch.process(in_ptrs, in_n, out_ptrs, p_frames);

		for (int i = 0; i < p_frames; i++) {
			p_out[i].left += out_l[i];
			p_out[i].right += out_r[i];
		}

		const int32_t end = clip.body_end + clip.tail_samples;
		if ((int32_t)source_pos >= end + stretch.inputLatency()) {
			finished = true;
			request_return();
		}
	}

	[[nodiscard]] int input_latency() const { return stretch.inputLatency(); }
	[[nodiscard]] int output_latency() const { return stretch.outputLatency(); }

private:
	void _ensure_scratch(int n) {
		if ((int)in_l.size() < n) {
			in_l.resize(n);
			in_r.resize(n);
			out_l.resize(n);
			out_r.resize(n);
		}
		in_ptrs[0] = in_l.data();
		in_ptrs[1] = in_r.data();
		out_ptrs[0] = out_l.data();
		out_ptrs[1] = out_r.data();
	}

	void _fill_input_from_source(int32_t p_start, int p_count) {
		for (int i = 0; i < p_count; i++) {
			const int32_t idx = p_start + i;
			if (idx < 0 || idx >= clip.frames) {
				in_l[i] = 0.0f;
				in_r[i] = 0.0f;
			} else {
				in_l[i] = clip.pcm[idx * 2];
				in_r[i] = clip.pcm[idx * 2 + 1];
			}
		}
	}
};

// Fixed pool of stretch contexts (plan default: 8).
class SymphonyStretchPool {
public:
	static constexpr int MAX_CONTEXTS = 8;
	SymphonyStretchContext contexts[MAX_CONTEXTS];

	SymphonyStretchContext *acquire_prepared(const SymphonyMusicClipDesc &p_clip, float p_tempo_ratio, float p_transpose_st) {
		for (int i = 0; i < MAX_CONTEXTS; i++) {
			if (contexts[i].is_free()) {
				if (!contexts[i].prepare(p_clip, p_tempo_ratio, p_transpose_st)) {
					return nullptr;
				}
				return &contexts[i];
			}
		}
		return nullptr;
	}

	void reclaim_returned() {
		for (int i = 0; i < MAX_CONTEXTS; i++) {
			if (contexts[i].get_ownership() == SymphonyStretchOwnership::Returning) {
				contexts[i].reclaim();
			}
		}
	}

	[[nodiscard]] int common_latency() const {
		int max_lat = 0;
		for (int i = 0; i < MAX_CONTEXTS; i++) {
			if (contexts[i].get_ownership() == SymphonyStretchOwnership::Prepared ||
					contexts[i].get_ownership() == SymphonyStretchOwnership::Active) {
				max_lat = MAX(max_lat, contexts[i].common_latency);
			}
		}
		return max_lat;
	}
};

// Decode PCM16 WAV-like buffers into interleaved stereo float at p_target_rate
// using linear resampling. Mono sources are duplicated to stereo.
inline bool symphony_music_resample_to_stereo_float(
		const int16_t *p_pcm16, int32_t p_frames, bool p_stereo, float p_src_rate,
		float p_dst_rate, Vector<float> &r_interleaved) {
	if (!p_pcm16 || p_frames <= 0 || !(p_src_rate > 0.0f) || !(p_dst_rate > 0.0f)) {
		return false;
	}
	const double ratio = (double)p_src_rate / (double)p_dst_rate;
	const int32_t out_frames = MAX(1, (int32_t)Math::floor((double)p_frames / ratio));
	r_interleaved.resize(out_frames * 2);
	const int ch = p_stereo ? 2 : 1;
	for (int32_t i = 0; i < out_frames; i++) {
		const double src_pos = (double)i * ratio;
		const int32_t i0 = (int32_t)src_pos;
		const int32_t i1 = MIN(i0 + 1, p_frames - 1);
		const float frac = (float)(src_pos - (double)i0);
		float l0, r0, l1, r1;
		if (p_stereo) {
			l0 = (float)p_pcm16[i0 * 2] / 32768.0f;
			r0 = (float)p_pcm16[i0 * 2 + 1] / 32768.0f;
			l1 = (float)p_pcm16[i1 * 2] / 32768.0f;
			r1 = (float)p_pcm16[i1 * 2 + 1] / 32768.0f;
		} else {
			l0 = r0 = (float)p_pcm16[i0] / 32768.0f;
			l1 = r1 = (float)p_pcm16[i1] / 32768.0f;
		}
		(void)ch;
		r_interleaved.write[i * 2] = l0 + (l1 - l0) * frac;
		r_interleaved.write[i * 2 + 1] = r0 + (r1 - r0) * frac;
	}
	return true;
}
