#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "../../core/symphony_denormal_guard.h"
#include "core/math/math_funcs.h"

// ModalBank: N parallel two-pole resonators (modal synthesis) driven by data arrays.
// Each mode = (frequency, t60 decay time, gain). A unit impulse on the
// excitation input rings each mode as gain * R^n * sin((n+1) * theta).
//
// P10: authored base_freq/t60/gain stay in the arena. FLOAT input pitch_ratio
// (and param root_hz for music) scales frequencies; modes above 0.45·sr mute.
class SymphonyModalBank : public SymphonyOperator {
private:
	static constexpr int32_t MAX_MODES_LIMIT = 64;
	static constexpr float RATIO_RECOMPUTE_EPS = 0.0005f; // 0.05%

	const float *SYMPHONY_RESTRICT excitation = nullptr;
	const float *SYMPHONY_RESTRICT pitch_ratio_in = nullptr;
	float *SYMPHONY_RESTRICT audio_out = nullptr;

	float *state_y1 = nullptr;
	float *state_y2 = nullptr;
	float *coeff_b0 = nullptr;
	float *coeff_a1 = nullptr;
	float *coeff_a2 = nullptr;
	float *mode_gain = nullptr;

	// Authored mode tables (kept for pitch_ratio recomputes + import_state).
	float *base_freq = nullptr;
	float *base_t60 = nullptr;
	float *base_gain = nullptr;

	int32_t num_modes = 0;
	int32_t max_modes = 0;
	float mix_rate = 44100.0f;
	float root_hz = 440.0f;
	float current_ratio = 1.0f;

	void _recompute_for_ratio(float p_ratio) {
		current_ratio = CLAMP(p_ratio, 0.125f, 8.0f);
		const float nyquist_mute = 0.45f * mix_rate;
		for (int32_t m = 0; m < num_modes; m++) {
			float freq = base_freq[m] * current_ratio;
			float t60 = base_t60[m];
			float gain = base_gain[m];
			if (freq >= nyquist_mute || freq <= 0.0f || t60 <= 0.0f) {
				coeff_b0[m] = 0.0f;
				coeff_a1[m] = 0.0f;
				coeff_a2[m] = 0.0f;
				mode_gain[m] = 0.0f;
				state_y1[m] = 0.0f;
				state_y2[m] = 0.0f;
				continue;
			}
			float bandwidth = 6.9078f / ((float)Math::PI * t60);
			float R = Math::exp(-(float)Math::PI * bandwidth / mix_rate);
			float theta = 2.0f * (float)Math::PI * freq / mix_rate;
			coeff_b0[m] = Math::sin(theta);
			coeff_a1[m] = -2.0f * R * Math::cos(theta);
			coeff_a2[m] = R * R;
			mode_gain[m] = gain;
		}
	}

public:
	SymphonyModalBank() {}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		excitation = (const float *)p_input_ptrs[0];
		pitch_ratio_in = (const float *)p_input_ptrs[1];
		audio_out = (float *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		SYMPHONY_ASSUME_FRAMES(p_num_frames);

		float ratio = current_ratio;
		if (pitch_ratio_in) {
			ratio = pitch_ratio_in[0];
		}
		ratio = CLAMP(ratio, 0.125f, 8.0f);
		if (Math::abs(ratio - current_ratio) > RATIO_RECOMPUTE_EPS * MAX(current_ratio, 1e-6f)) {
			_recompute_for_ratio(ratio);
		}

		if (!excitation || num_modes == 0) {
			for (int32_t i = 0; i < p_num_frames; i++) {
				audio_out[i] = 0.0f;
			}
			activity = 0;
			return;
		}

		float excitation_peak = 0.0f;
		for (int32_t i = 0; i < p_num_frames; i++) {
			float input = excitation[i];
			excitation_peak = MAX(excitation_peak, Math::abs(input));
			float sum = 0.0f;
			for (int32_t m = 0; m < num_modes; m++) {
				float y = coeff_b0[m] * input - coeff_a1[m] * state_y1[m] - coeff_a2[m] * state_y2[m];
				state_y2[m] = state_y1[m];
				state_y1[m] = y;
				sum += y * mode_gain[m];
			}
			audio_out[i] = sum;
		}

		int32_t live_modes = 0;
		for (int32_t m = 0; m < num_modes; m++) {
			if (Math::abs(state_y1[m]) + Math::abs(state_y2[m]) < 1e-9f) {
				state_y1[m] = 0.0f;
				state_y2[m] = 0.0f;
			} else {
				symphony_flush_denormal(state_y1[m]);
				symphony_flush_denormal(state_y2[m]);
				live_modes++;
			}
		}
		activity = (live_modes == 0 && excitation_peak < 1e-9f) ? 0 : 1;
	}

	void compute_coefficients(const float *frequencies, const float *t60s, const float *gains, int32_t p_num_modes) {
		num_modes = (p_num_modes > max_modes) ? max_modes : p_num_modes;
		for (int32_t m = 0; m < num_modes; m++) {
			base_freq[m] = frequencies[m];
			base_t60[m] = t60s[m];
			base_gain[m] = gains[m];
		}
		_recompute_for_ratio(current_ratio);
	}

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		// y1/y2 + current_ratio (base tables are structural, not voice state).
		size_t needed = sizeof(int32_t) + sizeof(float) + sizeof(float) * max_modes * 2;
		if (!p_buffer) {
			return needed;
		}
		if (p_max_size < needed) {
			return 0;
		}
		size_t offset = 0;
		memcpy(p_buffer + offset, &num_modes, sizeof(int32_t));
		offset += sizeof(int32_t);
		memcpy(p_buffer + offset, &current_ratio, sizeof(float));
		offset += sizeof(float);
		memcpy(p_buffer + offset, state_y1, sizeof(float) * max_modes);
		offset += sizeof(float) * max_modes;
		memcpy(p_buffer + offset, state_y2, sizeof(float) * max_modes);
		offset += sizeof(float) * max_modes;
		return offset;
	}

	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		size_t needed = sizeof(int32_t) + sizeof(float) + sizeof(float) * max_modes * 2;
		if (p_size < needed) {
			return;
		}
		size_t offset = 0;
		int32_t imported_modes = 0;
		memcpy(&imported_modes, p_buffer + offset, sizeof(int32_t));
		offset += sizeof(int32_t);
		float imported_ratio = 1.0f;
		memcpy(&imported_ratio, p_buffer + offset, sizeof(float));
		offset += sizeof(float);
		if (imported_modes <= max_modes) {
			num_modes = imported_modes;
		}
		memcpy(state_y1, p_buffer + offset, sizeof(float) * max_modes);
		offset += sizeof(float) * max_modes;
		memcpy(state_y2, p_buffer + offset, sizeof(float) * max_modes);
		// Recompute coeffs for the imported ratio against authored base tables.
		_recompute_for_ratio(imported_ratio);
	}

	virtual void clear_large_buffers() override {
		if (state_y1 && state_y2 && max_modes > 0) {
			memset(state_y1, 0, sizeof(float) * max_modes);
			memset(state_y2, 0, sizeof(float) * max_modes);
		}
	}

	static size_t export_bytes_for_max_modes(int32_t p_max_modes) {
		return sizeof(int32_t) + sizeof(float) + sizeof(float) * (size_t)p_max_modes * 2;
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "ModalBank";
		desc.category = "Synthesis";
		desc.inputs.push_back({ "excitation", SymphonyPinType::AUDIO, true });
		desc.inputs.push_back({ "pitch_ratio", SymphonyPinType::FLOAT, false });
		desc.outputs.push_back({ "audio_out", SymphonyPinType::AUDIO, false });
		desc.params.push_back({ "max_modes", 20.0f, 1.0f, 64.0f, 1.0f });
		desc.params.push_back({ "num_modes", 20.0f, 1.0f, 64.0f, 1.0f });
		desc.params.push_back({ "root_hz", 440.0f, 1.0f, 20000.0f, 0.1f });
		desc.state_size = sizeof(SymphonyModalBank);
		desc.state_align = alignof(SymphonyModalBank);
		// 9 arrays of max_modes floats: state×2, coeff×3, gain, base_freq/t60/gain.
		desc.extra_arena_bytes = sizeof(float) * 64 * 9 + 64;
		desc.export_state_bytes = export_bytes_for_max_modes(MAX_MODES_LIMIT);
		desc.resettable = true;
		desc.create_fn = &SymphonyModalBank::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		int32_t max_modes = p_params.has("max_modes") ? (int)(float)p_params["max_modes"] : 20;
		int32_t num_modes_param = p_params.has("num_modes") ? (int)(float)p_params["num_modes"] : max_modes;
		if (max_modes < 1) {
			max_modes = 1;
		}
		if (max_modes > MAX_MODES_LIMIT) {
			max_modes = MAX_MODES_LIMIT;
		}
		if (num_modes_param > max_modes) {
			num_modes_param = max_modes;
		}

		void *mem = p_arena.alloc(sizeof(SymphonyModalBank), alignof(SymphonyModalBank));
		if (!mem) {
			return nullptr;
		}
		SymphonyModalBank *bank = new (mem) SymphonyModalBank();
		bank->max_modes = max_modes;
		bank->mix_rate = p_mix_rate;
		bank->root_hz = p_params.has("root_hz") ? (float)p_params["root_hz"] : 440.0f;
		if (bank->root_hz < 1.0f) {
			bank->root_hz = 1.0f;
		}

		bank->state_y1 = (float *)p_arena.alloc(sizeof(float) * max_modes, alignof(float));
		bank->state_y2 = (float *)p_arena.alloc(sizeof(float) * max_modes, alignof(float));
		bank->coeff_b0 = (float *)p_arena.alloc(sizeof(float) * max_modes, alignof(float));
		bank->coeff_a1 = (float *)p_arena.alloc(sizeof(float) * max_modes, alignof(float));
		bank->coeff_a2 = (float *)p_arena.alloc(sizeof(float) * max_modes, alignof(float));
		bank->mode_gain = (float *)p_arena.alloc(sizeof(float) * max_modes, alignof(float));
		bank->base_freq = (float *)p_arena.alloc(sizeof(float) * max_modes, alignof(float));
		bank->base_t60 = (float *)p_arena.alloc(sizeof(float) * max_modes, alignof(float));
		bank->base_gain = (float *)p_arena.alloc(sizeof(float) * max_modes, alignof(float));

		memset(bank->state_y1, 0, sizeof(float) * max_modes);
		memset(bank->state_y2, 0, sizeof(float) * max_modes);
		memset(bank->base_freq, 0, sizeof(float) * max_modes);
		memset(bank->base_t60, 0, sizeof(float) * max_modes);
		memset(bank->base_gain, 0, sizeof(float) * max_modes);

		if (p_params.has("frequencies") && p_params.has("decay_times") && p_params.has("gains")) {
			PackedFloat32Array freqs = p_params["frequencies"];
			PackedFloat32Array decays = p_params["decay_times"];
			PackedFloat32Array gains = p_params["gains"];
			int32_t actual_modes = MIN(num_modes_param, (int32_t)freqs.size());
			actual_modes = MIN(actual_modes, (int32_t)decays.size());
			actual_modes = MIN(actual_modes, (int32_t)gains.size());
			bank->compute_coefficients(freqs.ptr(), decays.ptr(), gains.ptr(), actual_modes);
		} else {
			bank->num_modes = 0;
			memset(bank->coeff_b0, 0, sizeof(float) * max_modes);
			memset(bank->coeff_a1, 0, sizeof(float) * max_modes);
			memset(bank->coeff_a2, 0, sizeof(float) * max_modes);
			memset(bank->mode_gain, 0, sizeof(float) * max_modes);
		}
		return bank;
	}
};
