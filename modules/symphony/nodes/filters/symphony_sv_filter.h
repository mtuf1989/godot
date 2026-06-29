#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "core/math/math_funcs.h"

// Chamberlin state-variable filter with simultaneous LP/HP/BP outputs.
// More stable than BiquadFilter under rapid cutoff modulation.
class SymphonySVFilter : public SymphonyOperator {
private:
	const float *__restrict__ audio_in = nullptr;
	const float *__restrict__ cutoff_input = nullptr;
	const float *__restrict__ reso_input = nullptr;
	float *__restrict__ lp_out = nullptr;
	float *__restrict__ hp_out = nullptr;
	float *__restrict__ bp_out = nullptr;

	float lp = 0.0f; // low-pass state
	float bp = 0.0f; // band-pass state
	float mix_rate = 44100.0f;
	float default_cutoff = 1000.0f;
	float default_resonance = 0.0f;

public:
	SymphonySVFilter(float p_mix_rate, float p_cutoff, float p_reso)
			: mix_rate(p_mix_rate), default_cutoff(p_cutoff), default_resonance(p_reso) {}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		audio_in = (const float *)p_input_ptrs[0];
		cutoff_input = (const float *)p_input_ptrs[1];
		reso_input = (const float *)p_input_ptrs[2];
		lp_out = (float *)p_output_ptrs[0];
		hp_out = (float *)p_output_ptrs[1];
		bp_out = (float *)p_output_ptrs[2];
	}

	virtual void execute(int32_t p_num_frames) override {
		SYMPHONY_ASSUME_FRAMES(p_num_frames);

		float cutoff = cutoff_input ? *cutoff_input : default_cutoff;
		float resonance = reso_input ? *reso_input : default_resonance;

		// Chamberlin SVF coefficients
		float f = 2.0f * Math::sin(Math::PI * cutoff / mix_rate);
		if (f > 1.0f) f = 1.0f; // Clamp for stability at high frequencies
		float q = 1.0f - resonance; // damping (0 = max resonance)
		if (q < 0.01f) q = 0.01f; // Prevent zero damping

		SYMPHONY_UNROLL
		for (int32_t i = 0; i < p_num_frames; i++) {
			float input = audio_in ? audio_in[i] : 0.0f;

			float hp = input - lp - q * bp;
			bp += f * hp;
			lp += f * bp;

			lp_out[i] = lp;
			hp_out[i] = hp;
			bp_out[i] = bp;
		}
	}

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		constexpr size_t needed = sizeof(float) * 2;
		if (!p_buffer) return needed;
		if (p_max_size < needed) return 0;
		memcpy(p_buffer, &lp, sizeof(float));
		memcpy(p_buffer + sizeof(float), &bp, sizeof(float));
		return needed;
	}

	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		constexpr size_t needed = sizeof(float) * 2;
		if (p_size < needed) return;
		memcpy(&lp, p_buffer, sizeof(float));
		memcpy(&bp, p_buffer + sizeof(float), sizeof(float));
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "SVFilter";
		desc.category = "Filters";
		desc.inputs.push_back({ "audio_in", SymphonyPinType::AUDIO, true });
		desc.inputs.push_back({ "cutoff", SymphonyPinType::FLOAT, false });
		desc.inputs.push_back({ "resonance", SymphonyPinType::FLOAT, false });
		desc.outputs.push_back({ "lp_out", SymphonyPinType::AUDIO, false });
		desc.outputs.push_back({ "hp_out", SymphonyPinType::AUDIO, false });
		desc.outputs.push_back({ "bp_out", SymphonyPinType::AUDIO, false });
		desc.params.push_back({ "cutoff", 1000.0f, 20.0f, 20000.0f, 1.0f });
		desc.params.push_back({ "resonance", 0.0f, 0.0f, 1.0f, 0.01f });
		desc.state_size = sizeof(SymphonySVFilter);
		desc.state_align = alignof(SymphonySVFilter);
		desc.create_fn = &SymphonySVFilter::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		float cutoff = p_params.has("cutoff") ? (float)p_params["cutoff"] : 1000.0f;
		float reso = p_params.has("resonance") ? (float)p_params["resonance"] : 0.0f;
		void *mem = p_arena.alloc(sizeof(SymphonySVFilter), alignof(SymphonySVFilter));
		return new (mem) SymphonySVFilter(p_mix_rate, cutoff, reso);
	}
};
