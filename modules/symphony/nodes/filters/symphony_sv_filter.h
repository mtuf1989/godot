#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "core/math/math_funcs.h"

// Topology-preserving state-variable filter (TPT/ZDF) with simultaneous LP/HP/BP.
// Cutoff clamped to [20 Hz, 0.45 × sample_rate]; resonance maps to Q in [0.5, 20].
// tan() is evaluated only when the block-rate cutoff changes.
class SymphonySVFilter : public SymphonyOperator {
private:
	const float *SYMPHONY_RESTRICT audio_in = nullptr;
	const float *SYMPHONY_RESTRICT cutoff_input = nullptr;
	const float *SYMPHONY_RESTRICT reso_input = nullptr;
	float *SYMPHONY_RESTRICT lp_out = nullptr;
	float *SYMPHONY_RESTRICT hp_out = nullptr;
	float *SYMPHONY_RESTRICT bp_out = nullptr;

	float ic1eq = 0.0f; // integrator state 1
	float ic2eq = 0.0f; // integrator state 2
	float mix_rate = 44100.0f;
	float default_cutoff = 1000.0f;
	float default_resonance = 0.0f;

	float cached_cutoff = -1.0f;
	float cached_resonance = -1.0f;
	float g = 0.0f;
	float k = 0.0f;
	float a1 = 0.0f;
	float a2 = 0.0f;
	float a3 = 0.0f;

	static constexpr float ACTIVITY_THRESHOLD = 1e-6f; // ≈ -120 dB
	uint8_t silent_blocks = 0;

	void update_coeffs(float p_cutoff, float p_resonance) {
		float max_cutoff = 0.45f * mix_rate;
		float cutoff = CLAMP(p_cutoff, 20.0f, max_cutoff);
		// Map UI resonance [0,1] → Q [0.5, 20]
		float reso = CLAMP(p_resonance, 0.0f, 1.0f);
		float q = 0.5f + reso * 19.5f;

		if (cutoff == cached_cutoff && reso == cached_resonance) {
			return;
		}
		cached_cutoff = cutoff;
		cached_resonance = reso;

		g = Math::tan((float)Math::PI * cutoff / mix_rate);
		k = 1.0f / q;
		a1 = 1.0f / (1.0f + g * (g + k));
		a2 = g * a1;
		a3 = g * a2;
	}

public:
	SymphonySVFilter(float p_mix_rate, float p_cutoff, float p_reso)
			: mix_rate(p_mix_rate > 1.0f ? p_mix_rate : 44100.0f), default_cutoff(p_cutoff), default_resonance(p_reso) {
		update_coeffs(default_cutoff, default_resonance);
	}

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
		update_coeffs(cutoff, resonance);

		float peak = 0.0f;
		SYMPHONY_UNROLL
		for (int32_t i = 0; i < p_num_frames; i++) {
			float v0 = audio_in ? audio_in[i] : 0.0f;
			float v3 = v0 - ic2eq;
			float v1 = a1 * ic1eq + a2 * v3;
			float v2 = ic2eq + a2 * ic1eq + a3 * v3;
			ic1eq = 2.0f * v1 - ic1eq;
			ic2eq = 2.0f * v2 - ic2eq;

			float lp = v2;
			float bp = v1;
			float hp = v0 - k * v1 - v2;

			lp_out[i] = lp;
			hp_out[i] = hp;
			bp_out[i] = bp;

			float mag = Math::abs(lp) + Math::abs(bp) + Math::abs(hp);
			if (mag > peak) {
				peak = mag;
			}
		}

		float state_mag = Math::abs(ic1eq) + Math::abs(ic2eq);
		if (peak < ACTIVITY_THRESHOLD && state_mag < ACTIVITY_THRESHOLD) {
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
		constexpr size_t needed = sizeof(float) * 2;
		if (!p_buffer) {
			return needed;
		}
		if (p_max_size < needed) {
			return 0;
		}
		memcpy(p_buffer, &ic1eq, sizeof(float));
		memcpy(p_buffer + sizeof(float), &ic2eq, sizeof(float));
		return needed;
	}

	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		constexpr size_t needed = sizeof(float) * 2;
		if (p_size < needed) {
			return;
		}
		memcpy(&ic1eq, p_buffer, sizeof(float));
		memcpy(&ic2eq, p_buffer + sizeof(float), sizeof(float));
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
		desc.silence_behavior = SilenceBehavior::STATEFUL_TAIL;
		desc.create_fn = &SymphonySVFilter::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		float cutoff = p_params.has("cutoff") ? (float)p_params["cutoff"] : 1000.0f;
		float reso = p_params.has("resonance") ? (float)p_params["resonance"] : 0.0f;
		void *mem = p_arena.alloc(sizeof(SymphonySVFilter), alignof(SymphonySVFilter));
		if (!mem) {
			return nullptr;
		}
		return new (mem) SymphonySVFilter(p_mix_rate, cutoff, reso);
	}
};
