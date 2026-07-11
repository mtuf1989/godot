#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "core/math/math_funcs.h"

// Envelope Follower: extracts amplitude envelope from audio input.
// Uses per-sample peak detection with asymmetric attack/release smoothing.
// Outputs a single Float value per micro-block (the tracked envelope level).
class SymphonyEnvelopeFollower : public SymphonyOperator {
private:
	const float *SYMPHONY_RESTRICT audio_in = nullptr;
	const float *SYMPHONY_RESTRICT attack_input = nullptr;
	const float *SYMPHONY_RESTRICT release_input = nullptr;
	float *SYMPHONY_RESTRICT envelope_out = nullptr; // Single float output (FLOAT pin)

	float envelope = 0.0f; // Current envelope state
	float mix_rate = 44100.0f;
	float default_attack_ms = 1.0f;
	float default_release_ms = 50.0f;

public:
	SymphonyEnvelopeFollower(float p_mix_rate, float p_attack, float p_release)
			: mix_rate(p_mix_rate), default_attack_ms(p_attack), default_release_ms(p_release) {}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		audio_in = (const float *)p_input_ptrs[0];
		attack_input = (const float *)p_input_ptrs[1];
		release_input = (const float *)p_input_ptrs[2];
		envelope_out = (float *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		float attack_ms = attack_input ? *attack_input : default_attack_ms;
		float release_ms = release_input ? *release_input : default_release_ms;

		// Clamp to valid ranges
		attack_ms = CLAMP(attack_ms, 0.01f, 100.0f);
		release_ms = CLAMP(release_ms, 1.0f, 1000.0f);

		// Compute coefficients: coeff = 1 - exp(-1 / (time_seconds * sample_rate))
		float attack_coeff = 1.0f - Math::exp(-1.0f / (attack_ms * 0.001f * mix_rate));
		float release_coeff = 1.0f - Math::exp(-1.0f / (release_ms * 0.001f * mix_rate));

		// Per-sample peak detection with asymmetric smoothing
		for (int32_t i = 0; i < p_num_frames; i++) {
			float rectified = audio_in ? fabsf(audio_in[i]) : 0.0f;

			if (rectified > envelope) {
				envelope += attack_coeff * (rectified - envelope);
			} else {
				envelope += release_coeff * (rectified - envelope);
			}
		}

		// Output final envelope value as single Float
		*envelope_out = envelope;
	}

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		if (!p_buffer) return sizeof(float);
		if (p_max_size < sizeof(float)) return 0;
		memcpy(p_buffer, &envelope, sizeof(float));
		return sizeof(float);
	}

	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		if (p_size < sizeof(float)) return;
		memcpy(&envelope, p_buffer, sizeof(float));
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "EnvelopeFollower";
		desc.category = "Utility";
		desc.inputs.push_back({ "audio_in", SymphonyPinType::AUDIO, true });
		desc.inputs.push_back({ "attack_ms", SymphonyPinType::FLOAT, false });
		desc.inputs.push_back({ "release_ms", SymphonyPinType::FLOAT, false });
		desc.outputs.push_back({ "envelope_out", SymphonyPinType::FLOAT, false });
		desc.params.push_back({ "attack_ms", 1.0f, 0.01f, 100.0f, 0.1f });
		desc.params.push_back({ "release_ms", 50.0f, 1.0f, 1000.0f, 1.0f });
		desc.state_size = sizeof(SymphonyEnvelopeFollower);
		desc.state_align = alignof(SymphonyEnvelopeFollower);
		desc.create_fn = &SymphonyEnvelopeFollower::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		float atk = p_params.has("attack_ms") ? (float)p_params["attack_ms"] : 1.0f;
		float rel = p_params.has("release_ms") ? (float)p_params["release_ms"] : 50.0f;
		void *mem = p_arena.alloc(sizeof(SymphonyEnvelopeFollower), alignof(SymphonyEnvelopeFollower));
		return new (mem) SymphonyEnvelopeFollower(p_mix_rate, atk, rel);
	}
};
