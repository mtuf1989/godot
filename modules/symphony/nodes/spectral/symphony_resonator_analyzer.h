#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "core/math/math_funcs.h"

// ResonatorAnalyzer: Zero-latency spectral analysis via a bank of independent resonators.
//
// Based on: Alexandre R.J. François, "Real-Time, Low Latency and High Temporal
// Resolution Spectrograms" (ADC 2024).
//
// Each resonator answers "how much of frequency F is present in the signal right now?"
// with per-sample updates, zero latency, and arbitrary frequency placement (log, mel,
// musical pitches — not limited to linear FFT bins).
//
// Algorithm per resonator per sample:
//   1. Multiply input by complex phasor (correlate with cos/sin at target frequency).
//   2. EWMA smooth the complex response (one-pole lowpass, no buffer needed).
//   3. Compute power (magnitude²) of smoothed response.
//   4. Second EWMA pass on power to remove residual oscillation.
//   5. Advance phasor via complex multiply (generates next cos/sin).
//
// Outputs up to MAX_BANDS float values representing per-band power levels.
// Uses struct-of-arrays layout for SIMD-friendly processing.
//
// When to use instead of FFT (PhaseVocoder/SpectralGate):
//   - You need per-sample frequency tracking (zero hop latency)
//   - You want arbitrary frequency distribution
//   - You need to detect/track frequency content for reactive audio
//   - You do NOT need full DFT output for convolution or frequency-domain filtering
class SymphonyResonatorAnalyzer : public SymphonyOperator {
public:
	static constexpr int32_t MAX_BANDS = 32;
	static constexpr int32_t RENORM_INTERVAL = 64; // Renormalize phasors every N samples

private:
	// --- Pin pointers ---
	const float *SYMPHONY_RESTRICT audio_in = nullptr;
	// Output pins: up to MAX_BANDS float outputs (power per band)
	float *SYMPHONY_RESTRICT band_outputs[MAX_BANDS] = {};

	// --- Struct-of-Arrays resonator state (arena-allocated) ---
	float *phasors_real = nullptr;      // [num_bands] rotating unit vector (real part)
	float *phasors_imag = nullptr;      // [num_bands] rotating unit vector (imag part)
	float *rot_real = nullptr;          // [num_bands] precomputed rotation factor (real)
	float *rot_imag = nullptr;          // [num_bands] precomputed rotation factor (imag)
	float *smoothed_real = nullptr;     // [num_bands] EWMA-smoothed response (real)
	float *smoothed_imag = nullptr;     // [num_bands] EWMA-smoothed response (imag)
	float *power_smoothed = nullptr;    // [num_bands] second EWMA pass on power
	float *alphas = nullptr;            // [num_bands] EWMA coefficient per band
	float *one_minus_alphas = nullptr;  // [num_bands] precomputed (1 - alpha)

	// --- Parameters ---
	int32_t num_bands = 0;
	float mix_rate = 44100.0f;
	int32_t sample_counter = 0; // For periodic phasor renormalization

	// --- Helpers ---

	// Compute EWMA alpha for a given frequency.
	// Lower frequencies need longer observation (smaller alpha) to converge.
	// K controls the tradeoff: K=1 general, K=3-5 for bass (<100Hz).
	static float compute_alpha(float p_frequency, float p_sample_rate, float p_responsiveness) {
		// Time constant inversely proportional to frequency.
		// Responsiveness maps to K: 1.0 = fast (K=0.5), 0.0 = smooth (K=5.0)
		float K = 5.0f - 4.5f * CLAMP(p_responsiveness, 0.0f, 1.0f);
		float time_constant_samples = (K * p_sample_rate) / (p_frequency * logf(1.0f + p_frequency));
		// Clamp to prevent division by zero or absurdly small values
		time_constant_samples = MAX(time_constant_samples, 1.0f);
		return 1.0f / time_constant_samples;
	}

public:
	SymphonyResonatorAnalyzer() {}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		audio_in = (const float *)p_input_ptrs[0];
		for (int32_t i = 0; i < num_bands; i++) {
			band_outputs[i] = (float *)p_output_ptrs[i];
		}
	}

	virtual void execute(int32_t p_num_frames) override {
		SYMPHONY_ASSUME_FRAMES(p_num_frames);

		if (!audio_in || num_bands == 0) {
			for (int32_t b = 0; b < num_bands; b++) {
				if (band_outputs[b]) {
					*band_outputs[b] = 0.0f;
				}
			}
			return;
		}

		for (int32_t i = 0; i < p_num_frames; i++) {
			float input = audio_in[i];

			// Process all resonators for this sample
			SYMPHONY_UNROLL
			for (int32_t b = 0; b < num_bands; b++) {
				// 1. Correlate: multiply input by phasor (complex demodulation)
				float resp_r = input * phasors_real[b];
				float resp_i = input * phasors_imag[b];

				// 2. EWMA smooth the complex response
				smoothed_real[b] = alphas[b] * resp_r + one_minus_alphas[b] * smoothed_real[b];
				smoothed_imag[b] = alphas[b] * resp_i + one_minus_alphas[b] * smoothed_imag[b];

				// 3. Power (magnitude²) — skip sqrt for efficiency
				float pwr = smoothed_real[b] * smoothed_real[b] + smoothed_imag[b] * smoothed_imag[b];

				// 4. Second EWMA pass to remove residual oscillation
				power_smoothed[b] = alphas[b] * pwr + one_minus_alphas[b] * power_smoothed[b];

				// 5. Advance phasor (complex multiply)
				float nr = phasors_real[b] * rot_real[b] - phasors_imag[b] * rot_imag[b];
				float ni = phasors_real[b] * rot_imag[b] + phasors_imag[b] * rot_real[b];
				phasors_real[b] = nr;
				phasors_imag[b] = ni;
			}

			// 6. Periodic phasor renormalization (prevents float drift)
			sample_counter++;
			if (sample_counter >= RENORM_INTERVAL) {
				sample_counter = 0;
				for (int32_t b = 0; b < num_bands; b++) {
					float mag_sq = phasors_real[b] * phasors_real[b] + phasors_imag[b] * phasors_imag[b];
					// Fast reciprocal sqrt approximation (1/sqrt(x))
					float inv_mag = 1.0f / sqrtf(mag_sq);
					phasors_real[b] *= inv_mag;
					phasors_imag[b] *= inv_mag;
				}
			}
		}

		// Write final power values to output pins (control-rate: one value per block)
		for (int32_t b = 0; b < num_bands; b++) {
			if (band_outputs[b]) {
				*band_outputs[b] = power_smoothed[b];
			}
		}
	}

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		// Export: phasor state + smoothed state + power_smoothed + sample_counter
		size_t per_band = sizeof(float) * 5; // phasor_r, phasor_i, smooth_r, smooth_i, power
		size_t needed = sizeof(int32_t) + sizeof(int32_t) + per_band * num_bands;
		if (!p_buffer) {
			return needed;
		}
		if (p_max_size < needed) {
			return 0;
		}

		size_t offset = 0;
		memcpy(p_buffer + offset, &num_bands, sizeof(int32_t));
		offset += sizeof(int32_t);
		memcpy(p_buffer + offset, &sample_counter, sizeof(int32_t));
		offset += sizeof(int32_t);

		for (int32_t b = 0; b < num_bands; b++) {
			memcpy(p_buffer + offset, &phasors_real[b], sizeof(float));
			offset += sizeof(float);
			memcpy(p_buffer + offset, &phasors_imag[b], sizeof(float));
			offset += sizeof(float);
			memcpy(p_buffer + offset, &smoothed_real[b], sizeof(float));
			offset += sizeof(float);
			memcpy(p_buffer + offset, &smoothed_imag[b], sizeof(float));
			offset += sizeof(float);
			memcpy(p_buffer + offset, &power_smoothed[b], sizeof(float));
			offset += sizeof(float);
		}
		return offset;
	}

	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		size_t per_band = sizeof(float) * 5;
		size_t header = sizeof(int32_t) * 2;
		if (p_size < header) {
			return;
		}

		int32_t imported_bands = 0;
		size_t offset = 0;
		memcpy(&imported_bands, p_buffer + offset, sizeof(int32_t));
		offset += sizeof(int32_t);
		memcpy(&sample_counter, p_buffer + offset, sizeof(int32_t));
		offset += sizeof(int32_t);

		int32_t bands_to_restore = MIN(imported_bands, num_bands);
		if (p_size < header + per_band * bands_to_restore) {
			return;
		}

		for (int32_t b = 0; b < bands_to_restore; b++) {
			memcpy(&phasors_real[b], p_buffer + offset, sizeof(float));
			offset += sizeof(float);
			memcpy(&phasors_imag[b], p_buffer + offset, sizeof(float));
			offset += sizeof(float);
			memcpy(&smoothed_real[b], p_buffer + offset, sizeof(float));
			offset += sizeof(float);
			memcpy(&smoothed_imag[b], p_buffer + offset, sizeof(float));
			offset += sizeof(float);
			memcpy(&power_smoothed[b], p_buffer + offset, sizeof(float));
			offset += sizeof(float);
		}
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "ResonatorAnalyzer";
		desc.category = "Spectral";
		desc.inputs.push_back({ "audio_in", SymphonyPinType::AUDIO, true });
		// Dynamic outputs: up to MAX_BANDS float outputs named band_0..band_N
		for (int32_t i = 0; i < MAX_BANDS; i++) {
			String name = "band_" + String::num_int64(i);
			desc.outputs.push_back({ StringName(name), SymphonyPinType::FLOAT, false });
		}
		desc.params.push_back({ "num_bands", 8.0f, 1.0f, (float)MAX_BANDS, 1.0f });
		desc.params.push_back({ "responsiveness", 0.5f, 0.0f, 1.0f, 0.01f });
		desc.state_size = sizeof(SymphonyResonatorAnalyzer);
		desc.state_align = alignof(SymphonyResonatorAnalyzer);
		// 9 arrays × MAX_BANDS floats = 9 × 32 × 4 = 1152 bytes + alignment padding
		desc.extra_arena_bytes = sizeof(float) * MAX_BANDS * 9 + 128;
		desc.create_fn = &SymphonyResonatorAnalyzer::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		void *mem = p_arena.alloc(sizeof(SymphonyResonatorAnalyzer), alignof(SymphonyResonatorAnalyzer));
		if (!mem) {
			return nullptr;
		}
		SymphonyResonatorAnalyzer *ra = new (mem) SymphonyResonatorAnalyzer();

		ra->mix_rate = p_mix_rate;
		ra->num_bands = p_params.has("num_bands") ? (int32_t)(float)p_params["num_bands"] : 8;
		ra->num_bands = CLAMP(ra->num_bands, 1, MAX_BANDS);

		float responsiveness = p_params.has("responsiveness") ? (float)p_params["responsiveness"] : 0.5f;
		responsiveness = CLAMP(responsiveness, 0.0f, 1.0f);

		// --- Allocate SoA arrays ---
		ra->phasors_real = (float *)p_arena.alloc(sizeof(float) * MAX_BANDS, 32);
		ra->phasors_imag = (float *)p_arena.alloc(sizeof(float) * MAX_BANDS, 32);
		ra->rot_real = (float *)p_arena.alloc(sizeof(float) * MAX_BANDS, 32);
		ra->rot_imag = (float *)p_arena.alloc(sizeof(float) * MAX_BANDS, 32);
		ra->smoothed_real = (float *)p_arena.alloc(sizeof(float) * MAX_BANDS, 32);
		ra->smoothed_imag = (float *)p_arena.alloc(sizeof(float) * MAX_BANDS, 32);
		ra->power_smoothed = (float *)p_arena.alloc(sizeof(float) * MAX_BANDS, 32);
		ra->alphas = (float *)p_arena.alloc(sizeof(float) * MAX_BANDS, 32);
		ra->one_minus_alphas = (float *)p_arena.alloc(sizeof(float) * MAX_BANDS, 32);

		if (!ra->phasors_real || !ra->phasors_imag || !ra->rot_real || !ra->rot_imag ||
				!ra->smoothed_real || !ra->smoothed_imag || !ra->power_smoothed ||
				!ra->alphas || !ra->one_minus_alphas) {
			return nullptr;
		}

		// --- Zero all state ---
		memset(ra->phasors_real, 0, sizeof(float) * MAX_BANDS);
		memset(ra->phasors_imag, 0, sizeof(float) * MAX_BANDS);
		memset(ra->rot_real, 0, sizeof(float) * MAX_BANDS);
		memset(ra->rot_imag, 0, sizeof(float) * MAX_BANDS);
		memset(ra->smoothed_real, 0, sizeof(float) * MAX_BANDS);
		memset(ra->smoothed_imag, 0, sizeof(float) * MAX_BANDS);
		memset(ra->power_smoothed, 0, sizeof(float) * MAX_BANDS);
		memset(ra->alphas, 0, sizeof(float) * MAX_BANDS);
		memset(ra->one_minus_alphas, 0, sizeof(float) * MAX_BANDS);

		// --- Initialize resonators from frequency data ---
		// Frequencies can be provided as a PackedFloat32Array param, or default to log-spaced.
		PackedFloat32Array frequencies;
		if (p_params.has("frequencies")) {
			frequencies = p_params["frequencies"];
		}

		for (int32_t b = 0; b < ra->num_bands; b++) {
			float freq;
			if (b < (int32_t)frequencies.size()) {
				freq = frequencies[b];
			} else {
				// Default: log-spaced from 80Hz to 12kHz
				float t = (float)b / (float)(ra->num_bands - 1 + (ra->num_bands == 1 ? 1 : 0));
				freq = 80.0f * powf(150.0f, t); // 80 * 150^t covers 80Hz–12kHz
			}
			freq = CLAMP(freq, 20.0f, p_mix_rate * 0.45f); // Stay below Nyquist

			// Phasor rotation factor
			float angle_increment = Math::TAU * freq / p_mix_rate;
			ra->rot_real[b] = cosf(angle_increment);
			ra->rot_imag[b] = sinf(angle_increment);

			// Initial phasor state (pointing right on unit circle)
			ra->phasors_real[b] = 1.0f;
			ra->phasors_imag[b] = 0.0f;

			// EWMA alpha
			ra->alphas[b] = compute_alpha(freq, p_mix_rate, responsiveness);
			ra->one_minus_alphas[b] = 1.0f - ra->alphas[b];
		}

		ra->sample_counter = 0;
		return ra;
	}
};
