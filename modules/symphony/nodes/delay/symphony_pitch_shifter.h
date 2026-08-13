#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "core/math/math_funcs.h"

// PitchShifter: Real-time pitch shifting without time change.
// Uses dual read pointers advancing at modified rate, 180° out of phase.
// Raised-cosine crossfade when pointers wrap around the buffer.
//
// Algorithm:
//   pitch_ratio = 2^(semitones/12) - 1   (how much faster/slower than write)
//   Two read pointers separated by half the buffer length
//   Each pointer advances at (1 + pitch_ratio) relative to write
//   When a pointer wraps, it crossfades with the other using raised-cosine
//
// Buffer size ~100ms determines latency/quality trade-off.
// Design spec: latency < 50ms (buffer = 2048 samples at 48kHz ≈ 42ms).
class SymphonyPitchShifter : public SymphonyOperator {
private:
	const float *SYMPHONY_RESTRICT audio_in = nullptr;
	const float *SYMPHONY_RESTRICT shift_input = nullptr; // Float: semitones
	float *SYMPHONY_RESTRICT audio_out = nullptr;

	float *buffer = nullptr;
	int32_t buffer_size = 0;
	int32_t write_pos = 0;

	// Dual read pointers (fractional positions)
	float read_pos_a = 0.0f;
	float read_pos_b = 0.0f;

	// Crossfade state
	float crossfade_a = 1.0f; // Current gain for pointer A
	float crossfade_b = 0.0f; // Current gain for pointer B

	float mix_rate = 44100.0f;
	float default_shift_semitones = 0.0f;

	// Raised-cosine crossfade zone length (in samples)
	int32_t fade_length = 0;

	// Zero-shift dry bypass (C11): fade between processed and dry over 64 samples.
	float bypass_mix = 1.0f; // 1 = fully dry, 0 = fully processed
	static constexpr float BYPASS_FADE_SAMPLES = 64.0f;
	static constexpr float ZERO_SHIFT_EPS = 0.01f;

	inline float read_linear(float pos) const {
		// Wrap position into buffer bounds
		while (pos < 0.0f) pos += (float)buffer_size;
		while (pos >= (float)buffer_size) pos -= (float)buffer_size;

		int32_t idx = (int32_t)pos;
		float frac = pos - (float)idx;
		int32_t next = (idx + 1) % buffer_size;

		return buffer[idx] + frac * (buffer[next] - buffer[idx]);
	}

public:
	SymphonyPitchShifter() {}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		audio_in = (const float *)p_input_ptrs[0];
		shift_input = (const float *)p_input_ptrs[1];
		audio_out = (float *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		SYMPHONY_ASSUME_FRAMES(p_num_frames);

		if (!buffer || !audio_in) {
			for (int32_t i = 0; i < p_num_frames; i++) {
				audio_out[i] = 0.0f;
			}
			activity = 0;
			return;
		}

		float semitones = shift_input ? *shift_input : default_shift_semitones;
		semitones = CLAMP(semitones, -12.0f, 12.0f);
		const bool want_bypass = Math::abs(semitones) < ZERO_SHIFT_EPS;
		const float bypass_step = 1.0f / BYPASS_FADE_SAMPLES;

		float read_rate = Math::pow(2.0f, semitones / 12.0f);
		float peak = 0.0f;

		for (int32_t i = 0; i < p_num_frames; i++) {
			const float dry = audio_in[i];
			buffer[write_pos] = dry;

			float sample_a = read_linear(read_pos_a);
			float sample_b = read_linear(read_pos_b);
			float wet = sample_a * crossfade_a + sample_b * crossfade_b;

			if (want_bypass) {
				bypass_mix = MIN(1.0f, bypass_mix + bypass_step);
			} else {
				bypass_mix = MAX(0.0f, bypass_mix - bypass_step);
			}
			audio_out[i] = dry * bypass_mix + wet * (1.0f - bypass_mix);
			peak = MAX(peak, Math::abs(audio_out[i]));

			// Keep history advancing even in full dry bypass.
			read_pos_a += want_bypass ? 1.0f : read_rate;
			read_pos_b += want_bypass ? 1.0f : read_rate;

			if (read_pos_a >= (float)buffer_size) read_pos_a -= (float)buffer_size;
			if (read_pos_a < 0.0f) read_pos_a += (float)buffer_size;
			if (read_pos_b >= (float)buffer_size) read_pos_b -= (float)buffer_size;
			if (read_pos_b < 0.0f) read_pos_b += (float)buffer_size;

			if (!want_bypass || bypass_mix < 1.0f) {
				float dist_a = read_pos_a - (float)write_pos;
				if (dist_a < 0.0f) dist_a += (float)buffer_size;
				float dist_b = read_pos_b - (float)write_pos;
				if (dist_b < 0.0f) dist_b += (float)buffer_size;

				float fade_f = (float)fade_length;
				if (dist_a < fade_f) {
					crossfade_a = 0.5f * (1.0f + Math::cos((float)Math::PI * (1.0f - dist_a / fade_f)));
				} else if (dist_a > (float)buffer_size - fade_f) {
					crossfade_a = 0.5f * (1.0f + Math::cos((float)Math::PI * (dist_a - ((float)buffer_size - fade_f)) / fade_f));
				} else {
					crossfade_a = 1.0f;
				}

				if (dist_b < fade_f) {
					crossfade_b = 0.5f * (1.0f + Math::cos((float)Math::PI * (1.0f - dist_b / fade_f)));
				} else if (dist_b > (float)buffer_size - fade_f) {
					crossfade_b = 0.5f * (1.0f + Math::cos((float)Math::PI * (dist_b - ((float)buffer_size - fade_f)) / fade_f));
				} else {
					crossfade_b = 1.0f;
				}

				float total = crossfade_a + crossfade_b;
				if (total > 0.001f) {
					crossfade_a /= total;
					crossfade_b /= total;
				}
			} else {
				// Steady zero-shift: pointer A at unity, B muted (exact dry path above).
				crossfade_a = 1.0f;
				crossfade_b = 0.0f;
			}

			write_pos = (write_pos + 1) % buffer_size;
		}

		activity = (peak >= 1e-6f) ? 1 : 0;
	}

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		// Export write_pos, read positions, crossfade state, and buffer contents
		size_t needed = sizeof(int32_t) + sizeof(float) * 4 + sizeof(float) * buffer_size;
		if (!p_buffer) return needed;
		if (p_max_size < needed) return 0;

		size_t offset = 0;
		memcpy(p_buffer + offset, &write_pos, sizeof(int32_t));
		offset += sizeof(int32_t);
		memcpy(p_buffer + offset, &read_pos_a, sizeof(float));
		offset += sizeof(float);
		memcpy(p_buffer + offset, &read_pos_b, sizeof(float));
		offset += sizeof(float);
		memcpy(p_buffer + offset, &crossfade_a, sizeof(float));
		offset += sizeof(float);
		memcpy(p_buffer + offset, &crossfade_b, sizeof(float));
		offset += sizeof(float);
		memcpy(p_buffer + offset, buffer, sizeof(float) * buffer_size);
		return needed;
	}

	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		size_t needed = sizeof(int32_t) + sizeof(float) * 4 + sizeof(float) * buffer_size;
		if (p_size < needed) return;

		size_t offset = 0;
		memcpy(&write_pos, p_buffer + offset, sizeof(int32_t));
		offset += sizeof(int32_t);
		memcpy(&read_pos_a, p_buffer + offset, sizeof(float));
		offset += sizeof(float);
		memcpy(&read_pos_b, p_buffer + offset, sizeof(float));
		offset += sizeof(float);
		memcpy(&crossfade_a, p_buffer + offset, sizeof(float));
		offset += sizeof(float);
		memcpy(&crossfade_b, p_buffer + offset, sizeof(float));
		offset += sizeof(float);
		memcpy(buffer, p_buffer + offset, sizeof(float) * buffer_size);
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "PitchShifter";
		desc.category = "Delay";
		desc.inputs.push_back({ "audio_in", SymphonyPinType::AUDIO, true });
		desc.inputs.push_back({ "pitch_shift", SymphonyPinType::FLOAT, false });
		desc.outputs.push_back({ "audio_out", SymphonyPinType::AUDIO, false });
		desc.params.push_back({ "pitch_shift", 0.0f, -12.0f, 12.0f, 0.01f });
		desc.params.push_back({ "buffer_ms", 85.0f, 20.0f, 200.0f, 1.0f });
		desc.state_size = sizeof(SymphonyPitchShifter);
		desc.state_align = alignof(SymphonyPitchShifter);
		desc.extra_arena_bytes = 0;
		desc.extra_arena_bytes_fn = &SymphonyPitchShifter::calculate_arena_bytes;
		desc.create_fn = &SymphonyPitchShifter::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	struct Config {
		float shift_semitones = 0.0f;
		float buffer_ms = 85.0f;
		int32_t buffer_size = 0;
	};

	[[nodiscard]] static Config resolve_config(const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		Config cfg;
		cfg.shift_semitones = p_params.has("pitch_shift") ? (float)p_params["pitch_shift"] : 0.0f;
		cfg.buffer_ms = p_params.has("buffer_ms") ? (float)p_params["buffer_ms"] : 85.0f;
		cfg.buffer_ms = CLAMP(cfg.buffer_ms, 20.0f, 200.0f);
		float rate = p_mix_rate > 1.0f ? p_mix_rate : 48000.0f;
		cfg.buffer_size = (int32_t)(cfg.buffer_ms * rate * 0.001f) + 4;
		if (cfg.buffer_size < 4) {
			cfg.buffer_size = 4;
		}
		return cfg;
	}

	static size_t calculate_arena_bytes(const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		Config cfg = resolve_config(p_params, p_mix_rate);
		return sizeof(float) * (size_t)cfg.buffer_size;
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		void *mem = p_arena.alloc(sizeof(SymphonyPitchShifter), alignof(SymphonyPitchShifter));
		if (!mem) return nullptr;
		SymphonyPitchShifter *ps = new (mem) SymphonyPitchShifter();

		Config cfg = resolve_config(p_params, p_mix_rate);
		ps->mix_rate = p_mix_rate;
		ps->default_shift_semitones = cfg.shift_semitones;

		ps->buffer_size = cfg.buffer_size;
		ps->buffer = (float *)p_arena.alloc(sizeof(float) * ps->buffer_size, 32);
		if (!ps->buffer) return nullptr;
		memset(ps->buffer, 0, sizeof(float) * ps->buffer_size);

		// Fade zone = 25% of buffer
		ps->fade_length = ps->buffer_size / 4;
		if (ps->fade_length < 16) ps->fade_length = 16;

		// Initialize dual read pointers 180° apart (half buffer distance)
		ps->write_pos = 0;
		ps->read_pos_a = (float)(ps->buffer_size / 4);         // quarter behind write
		ps->read_pos_b = (float)(ps->buffer_size * 3 / 4);     // three-quarters behind write
		ps->crossfade_a = 1.0f;
		ps->crossfade_b = 0.0f;

		return ps;
	}
};
