#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "core/math/math_funcs.h"

// Ring-buffer delay line with cubic Hermite interpolation for fractional delays.
// Arena-allocated buffer. Supports modulated delay time (chorus/flanger).
class SymphonyDelayLine : public SymphonyOperator {
private:
	const float *SYMPHONY_RESTRICT audio_in = nullptr;
	const float *SYMPHONY_RESTRICT delay_input = nullptr; // Float: delay time in ms
	float *SYMPHONY_RESTRICT audio_out = nullptr;

	float *buffer = nullptr;
	int32_t buffer_size = 0; // in samples
	int32_t write_pos = 0;
	float mix_rate = 44100.0f;
	float default_delay_ms = 10.0f;
	float ring_energy = 0.0f; // Incremental Σ x² for silence activity
	uint8_t silent_blocks = 0;
	static constexpr float ACTIVITY_THRESHOLD = 1e-12f; // energy ≈ (-120 dB)² scale

	inline float read_hermite(float delay_samples) const {
		float read_pos = (float)write_pos - delay_samples;
		if (read_pos < 0.0f) read_pos += (float)buffer_size;

		int32_t idx = (int32_t)read_pos;
		float frac = read_pos - (float)idx;

		int32_t i0 = (idx - 1 + buffer_size) % buffer_size;
		int32_t i1 = idx % buffer_size;
		int32_t i2 = (idx + 1) % buffer_size;
		int32_t i3 = (idx + 2) % buffer_size;

		float y0 = buffer[i0], y1 = buffer[i1], y2 = buffer[i2], y3 = buffer[i3];

		// Cubic Hermite interpolation
		float c0 = y1;
		float c1 = 0.5f * (y2 - y0);
		float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
		float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

		return ((c3 * frac + c2) * frac + c1) * frac + c0;
	}

public:
	SymphonyDelayLine() {}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		audio_in = (const float *)p_input_ptrs[0];
		delay_input = (const float *)p_input_ptrs[1];
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

		float delay_ms = delay_input ? *delay_input : default_delay_ms;
		float delay_samples = delay_ms * mix_rate * 0.001f;
		if (delay_samples < 1.0f) delay_samples = 1.0f;
		if (delay_samples > (float)(buffer_size - 2)) delay_samples = (float)(buffer_size - 2);

		for (int32_t i = 0; i < p_num_frames; i++) {
			float in_s = audio_in[i];
			float old = buffer[write_pos];
			ring_energy += in_s * in_s - old * old;
			if (ring_energy < 0.0f) {
				ring_energy = 0.0f;
			}
			buffer[write_pos] = in_s;
			audio_out[i] = read_hermite(delay_samples);
			write_pos = (write_pos + 1) % buffer_size;
		}

		if (ring_energy < ACTIVITY_THRESHOLD) {
			if (silent_blocks < 255) {
				silent_blocks++;
			}
			activity = (silent_blocks >= 2) ? 0 : 1;
		} else {
			silent_blocks = 0;
			activity = 1;
		}
	}

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		size_t needed = sizeof(int32_t) + sizeof(float) * buffer_size;
		if (!p_buffer) return needed;
		if (p_max_size < needed) return 0;
		memcpy(p_buffer, &write_pos, sizeof(int32_t));
		memcpy(p_buffer + sizeof(int32_t), buffer, sizeof(float) * buffer_size);
		return needed;
	}

	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		size_t needed = sizeof(int32_t) + sizeof(float) * buffer_size;
		if (p_size < needed) return;
		memcpy(&write_pos, p_buffer, sizeof(int32_t));
		memcpy(buffer, p_buffer + sizeof(int32_t), sizeof(float) * buffer_size);
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "DelayLine";
		desc.category = "Delay";
		desc.inputs.push_back({ "audio_in", SymphonyPinType::AUDIO, true });
		desc.inputs.push_back({ "delay_time", SymphonyPinType::FLOAT, false });
		desc.outputs.push_back({ "audio_out", SymphonyPinType::AUDIO, false });
		desc.params.push_back({ "max_delay_ms", 1000.0f, 1.0f, 2000.0f, 1.0f });
		desc.params.push_back({ "delay_ms", 10.0f, 0.0f, 2000.0f, 0.1f });
		desc.state_size = sizeof(SymphonyDelayLine);
		desc.state_align = alignof(SymphonyDelayLine);
		desc.extra_arena_bytes = 0;
		desc.extra_arena_bytes_fn = &SymphonyDelayLine::calculate_arena_bytes;
		desc.create_fn = &SymphonyDelayLine::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	struct Config {
		float max_ms = 1000.0f;
		float delay_ms = 10.0f;
		int32_t buffer_size = 0;
	};

	[[nodiscard]] static Config resolve_config(const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		Config cfg;
		cfg.max_ms = p_params.has("max_delay_ms") ? (float)p_params["max_delay_ms"] : 1000.0f;
		if (cfg.max_ms > 2000.0f) {
			cfg.max_ms = 2000.0f;
		}
		if (cfg.max_ms < 1.0f) {
			cfg.max_ms = 1.0f;
		}
		cfg.delay_ms = p_params.has("delay_ms") ? (float)p_params["delay_ms"] : 10.0f;
		float rate = p_mix_rate > 1.0f ? p_mix_rate : 48000.0f;
		cfg.buffer_size = (int32_t)(cfg.max_ms * rate * 0.001f) + 4; // +4 for interpolation
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
		void *mem = p_arena.alloc(sizeof(SymphonyDelayLine), alignof(SymphonyDelayLine));
		if (!mem) {
			return nullptr;
		}
		SymphonyDelayLine *dl = new (mem) SymphonyDelayLine();

		Config cfg = resolve_config(p_params, p_mix_rate);
		dl->mix_rate = p_mix_rate;
		dl->default_delay_ms = cfg.delay_ms;
		dl->buffer_size = cfg.buffer_size;
		dl->buffer = (float *)p_arena.alloc(sizeof(float) * dl->buffer_size, 32);
		if (!dl->buffer) {
			return nullptr;
		}
		dl->write_pos = 0;

		return dl;
	}
};
