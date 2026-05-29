#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "core/math/math_funcs.h"

// Dynamic range compressor.
// Mode 0 = Peak-based envelope follower (implemented).
// Mode 1 = RMS-based (TODO: requires ring buffer in arena, documented for future).
class SymphonyCompressor : public SymphonyOperator {
private:
	const float *input = nullptr;
	float *output = nullptr;

	float threshold_lin = 0.5f; // Linear threshold
	float ratio = 4.0f;
	float attack_coeff = 0.0f;
	float release_coeff = 0.0f;
	float makeup_lin = 1.0f;
	int32_t mode = 0; // 0=peak, 1=RMS (future)

	// Envelope state
	float envelope = 0.0f;

public:
	SymphonyCompressor(float p_mix_rate, float p_threshold_db, float p_ratio,
			float p_attack_ms, float p_release_ms, float p_makeup_db, int32_t p_mode)
			: ratio(p_ratio), mode(p_mode) {
		threshold_lin = powf(10.0f, p_threshold_db / 20.0f);
		makeup_lin = powf(10.0f, p_makeup_db / 20.0f);
		// Time constants: coeff = exp(-1 / (time_in_seconds * sample_rate))
		attack_coeff = expf(-1.0f / (p_attack_ms * 0.001f * p_mix_rate));
		release_coeff = expf(-1.0f / (p_release_ms * 0.001f * p_mix_rate));
	}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		input = (const float *)p_input_ptrs[0];
		output = (float *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		// Peak-based compression
		for (int32_t i = 0; i < p_num_frames; i++) {
			float abs_in = fabsf(input[i]);

			// Envelope follower (attack/release)
			if (abs_in > envelope) {
				envelope = attack_coeff * envelope + (1.0f - attack_coeff) * abs_in;
			} else {
				envelope = release_coeff * envelope + (1.0f - release_coeff) * abs_in;
			}

			// Gain computation
			float gain = 1.0f;
			if (envelope > threshold_lin) {
				// Compress: reduce gain above threshold
				float over = envelope / threshold_lin;
				float compressed = powf(over, 1.0f / ratio - 1.0f);
				gain = compressed;
			}

			output[i] = input[i] * gain * makeup_lin;
		}
	}

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		if (!p_buffer) return sizeof(float);
		if (p_max_size >= sizeof(float)) memcpy(p_buffer, &envelope, sizeof(float));
		return sizeof(float);
	}
	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		if (p_size >= sizeof(float)) memcpy(&envelope, p_buffer, sizeof(float));
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "Compressor";
		desc.category = "Envelopes";
		desc.inputs.push_back({ "input", SymphonyPinType::AUDIO, true });
		desc.outputs.push_back({ "output", SymphonyPinType::AUDIO, false });
		desc.params.push_back({ "threshold_db", -12.0f, -60.0f, 0.0f, 0.5f });
		desc.params.push_back({ "ratio", 4.0f, 1.0f, 20.0f, 0.1f });
		desc.params.push_back({ "attack_ms", 10.0f, 0.1f, 200.0f, 0.1f });
		desc.params.push_back({ "release_ms", 100.0f, 10.0f, 2000.0f, 1.0f });
		desc.params.push_back({ "makeup_db", 0.0f, 0.0f, 24.0f, 0.5f });
		desc.params.push_back({ "mode", 0.0f, 0.0f, 1.0f, 1.0f }); // 0=Peak, 1=RMS(future)
		desc.state_size = sizeof(SymphonyCompressor);
		desc.state_align = alignof(SymphonyCompressor);
		desc.create_fn = &SymphonyCompressor::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		float th = -12.0f, r = 4.0f, a = 10.0f, rel = 100.0f, mk = 0.0f;
		int32_t m = 0;
		if (p_params.has("threshold_db")) th = p_params["threshold_db"];
		if (p_params.has("ratio")) r = p_params["ratio"];
		if (p_params.has("attack_ms")) a = p_params["attack_ms"];
		if (p_params.has("release_ms")) rel = p_params["release_ms"];
		if (p_params.has("makeup_db")) mk = p_params["makeup_db"];
		if (p_params.has("mode")) m = (int32_t)(float)p_params["mode"];
		void *mem = p_arena.alloc(sizeof(SymphonyCompressor), alignof(SymphonyCompressor));
		return new (mem) SymphonyCompressor(p_mix_rate, th, r, a, rel, mk, m);
	}
};
