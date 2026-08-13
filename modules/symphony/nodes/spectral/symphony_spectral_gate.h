#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "core/math/math_funcs.h"
#include "modules/symphony/thirdparty/pffft/pffft.h"

// SpectralGate: Frequency-domain noise gate for noise suppression and spectral effects.
//
// Algorithm (pass-through spectral processor — no time-stretch or pitch-shift):
//   1. Accumulate input into a ring buffer.
//   2. Every hop_size samples:
//      a. Extract fft_size samples from ring buffer.
//      b. Apply Hanning window.
//      c. Forward FFT.
//      d. For each frequency bin: compute magnitude in dB.
//      e. If magnitude_db < threshold: attenuate by reduction_db.
//      f. Inverse FFT, scale by 1/N.
//      g. Apply synthesis window.
//      h. Overlap-add to output ring buffer.
//   3. Read from output ring buffer.
//
// NOTE: PFFFT_Setup* is heap-allocated by pffft_new_setup() and cannot live in
// the arena. It will leak when the arena is freed. This is acceptable because
// graph destruction is infrequent and the setup is small (~few KB).
class SymphonySpectralGate : public SymphonyOperator {
private:
	// --- Pin pointers (bound by compiler) ---
	const float *SYMPHONY_RESTRICT audio_in = nullptr;
	const float *SYMPHONY_RESTRICT threshold_db_input = nullptr;
	const float *SYMPHONY_RESTRICT reduction_db_input = nullptr;
	float *SYMPHONY_RESTRICT audio_out = nullptr;

	// --- FFT setup (heap-allocated, see note above) ---
	PFFFT_Setup *pffft_setup = nullptr;

	// --- Arena-allocated buffers ---
	float *input_ring = nullptr;      // [fft_size * 2] circular input buffer
	float *output_ring = nullptr;     // [fft_size * 2] overlap-add accumulator
	float *window_lut = nullptr;      // [fft_size] precomputed Hanning window
	float *cola_gain = nullptr;       // [fft_size] 1 / Σ w² (COLA normalize)
	float *fft_workspace = nullptr;   // [fft_size] pffft work buffer
	float *analysis_frame = nullptr;  // [fft_size] windowed frame for FFT input
	float *fft_buffer = nullptr;      // [fft_size] FFT output (interleaved complex)
	float *ifft_buffer = nullptr;     // [fft_size] IFFT output

	// --- Parameters ---
	int32_t fft_size = 2048;
	int32_t overlap = 4;
	int32_t hop_size = 0;             // = fft_size / overlap
	float default_threshold_db = -40.0f;
	float default_reduction_db = -40.0f;

	// --- Processing state ---
	int32_t input_write_pos = 0;      // Write position in input_ring [0, fft_size*2)
	int32_t input_samples_fed = 0;    // Total input samples written (monotonic counter)
	int32_t output_read_pos = 0;      // Read position in output_ring [0, fft_size*2)
	int32_t output_write_pos = 0;     // Write position in output_ring (where OLA writes)
	int32_t hop_counter = 0;          // Counts input samples; triggers new frame at hop_size boundary
	bool primed = false;              // True once we have at least fft_size input samples

	// --- Helpers ---

	// Process one full STFT analysis-gate-resynthesize frame
	void process_frame(float p_threshold_db, float p_reduction_db) {
		const float reduction_linear = Math::pow(10.0f, p_reduction_db / 20.0f);
		const int32_t N = fft_size;
		const int32_t ring_in_size = N * 2;

		// --- Step 1: Extract fft_size samples from input ring (most recent) ---
		// The most recent fft_size samples end at input_write_pos.
		int32_t read_start = (input_write_pos - N + ring_in_size) % ring_in_size;

		// Copy with wraparound handling and apply analysis window
		for (int32_t i = 0; i < N; i++) {
			int32_t idx = (read_start + i) % ring_in_size;
			analysis_frame[i] = input_ring[idx] * window_lut[i];
		}

		// --- Step 2: Forward FFT ---
		pffft_transform_ordered(pffft_setup, analysis_frame, fft_buffer, fft_workspace, PFFFT_FORWARD);

		// --- Step 3: Spectral gating ---
		// pffft ordered output for PFFFT_REAL of size N:
		//   fft_buffer[0] = DC (real), fft_buffer[1] = Nyquist (real)
		//   fft_buffer[2k], fft_buffer[2k+1] = real, imag of bin k (k=1..N/2-1)

		// DC bin (index 0)
		{
			float mag = Math::abs(fft_buffer[0]);
			float mag_db = (mag > 1e-10f) ? 20.0f * Math::log(mag) / Math::log(10.0f) : -200.0f;
			if (mag_db < p_threshold_db) {
				fft_buffer[0] *= reduction_linear;
			}
		}

		// Nyquist bin (index 1)
		{
			float mag = Math::abs(fft_buffer[1]);
			float mag_db = (mag > 1e-10f) ? 20.0f * Math::log(mag) / Math::log(10.0f) : -200.0f;
			if (mag_db < p_threshold_db) {
				fft_buffer[1] *= reduction_linear;
			}
		}

		// Bins 1..N/2-1
		int32_t half_n = N / 2;
		for (int32_t k = 1; k < half_n; k++) {
			float re = fft_buffer[2 * k];
			float im = fft_buffer[2 * k + 1];
			float mag_sq = re * re + im * im;
			// Denormal-safe sqrt: skip expensive sqrt for near-zero bins (silence).
			// On x86 without FTZ/DAZ, sqrt of denormals incurs 10-100x penalty.
			float mag = mag_sq > 1e-30f ? Math::sqrt(mag_sq) : 0.0f;
			float mag_db = (mag > 1e-10f) ? 20.0f * Math::log(mag) / Math::log(10.0f) : -200.0f;
			if (mag_db < p_threshold_db) {
				fft_buffer[2 * k] *= reduction_linear;
				fft_buffer[2 * k + 1] *= reduction_linear;
			}
		}

		// --- Step 4: Inverse FFT ---
		pffft_transform_ordered(pffft_setup, fft_buffer, ifft_buffer, fft_workspace, PFFFT_BACKWARD);

		// --- Step 5: Scale by 1/N, synthesis window, and COLA gain ---
		const float inv_fft_size = 1.0f / (float)N;
		for (int32_t i = 0; i < N; i++) {
			float g = cola_gain ? cola_gain[i] : 1.0f;
			ifft_buffer[i] *= inv_fft_size * window_lut[i] * g;
		}

		// --- Step 6: Overlap-add into output ring buffer ---
		int32_t ring_out_size = N * 2;
		for (int32_t i = 0; i < N; i++) {
			int32_t idx = (output_write_pos + i) % ring_out_size;
			output_ring[idx] += ifft_buffer[i];
		}

		// Advance output write position by hop_size
		output_write_pos = (output_write_pos + hop_size) % ring_out_size;
	}

public:
	SymphonySpectralGate() {}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		audio_in = (const float *)p_input_ptrs[0];
		threshold_db_input = (const float *)p_input_ptrs[1];
		reduction_db_input = (const float *)p_input_ptrs[2];
		audio_out = (float *)p_output_ptrs[0];
	}

	virtual void cleanup() override {
		if (pffft_setup) {
			pffft_destroy_setup(pffft_setup);
			pffft_setup = nullptr;
		}
	}

	virtual void execute(int32_t p_num_frames) override {
		SYMPHONY_ASSUME_FRAMES(p_num_frames);

		// Read control-rate parameters (once per micro-block)
		float threshold_db = threshold_db_input ? *threshold_db_input : default_threshold_db;
		float reduction_db = reduction_db_input ? *reduction_db_input : default_reduction_db;

		threshold_db = CLAMP(threshold_db, -96.0f, 0.0f);
		reduction_db = CLAMP(reduction_db, -96.0f, 0.0f);

		const int32_t ring_in_size = fft_size * 2;
		const int32_t ring_out_size = fft_size * 2;

		for (int32_t i = 0; i < p_num_frames; i++) {
			// --- Write input sample to input ring buffer ---
			float in_sample = audio_in ? audio_in[i] : 0.0f;
			input_ring[input_write_pos] = in_sample;
			input_write_pos = (input_write_pos + 1) % ring_in_size;
			input_samples_fed++;

			// Check if we have enough input to start processing
			if (!primed) {
				if (input_samples_fed >= fft_size) {
					primed = true;
				}
			}

			// --- Check if we need a new analysis/synthesis frame ---
			hop_counter++;
			if (primed && hop_counter >= hop_size) {
				hop_counter = 0;

				// Process one STFT frame
				process_frame(threshold_db, reduction_db);
			}

			// --- Read output from overlap-add buffer ---
			if (primed) {
				audio_out[i] = output_ring[output_read_pos];
				// Clear the sample after reading (for next overlap-add pass)
				output_ring[output_read_pos] = 0.0f;
				output_read_pos = (output_read_pos + 1) % ring_out_size;
			} else {
				audio_out[i] = 0.0f;
			}
		}
	}

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		struct StateHeader {
			int32_t input_write_pos;
			int32_t input_samples_fed;
			int32_t output_read_pos;
			int32_t output_write_pos;
			int32_t hop_counter;
			int32_t primed;
		};

		size_t needed = sizeof(StateHeader);
		if (!p_buffer) {
			return needed;
		}
		if (p_max_size < needed) {
			return 0;
		}

		StateHeader hdr;
		hdr.input_write_pos = input_write_pos;
		hdr.input_samples_fed = input_samples_fed;
		hdr.output_read_pos = output_read_pos;
		hdr.output_write_pos = output_write_pos;
		hdr.hop_counter = hop_counter;
		hdr.primed = primed ? 1 : 0;

		memcpy(p_buffer, &hdr, sizeof(StateHeader));
		return sizeof(StateHeader);
	}

	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		struct StateHeader {
			int32_t input_write_pos;
			int32_t input_samples_fed;
			int32_t output_read_pos;
			int32_t output_write_pos;
			int32_t hop_counter;
			int32_t primed;
		};

		if (p_size < sizeof(StateHeader)) {
			return;
		}

		StateHeader hdr;
		memcpy(&hdr, p_buffer, sizeof(StateHeader));

		input_write_pos = hdr.input_write_pos;
		input_samples_fed = hdr.input_samples_fed;
		output_read_pos = hdr.output_read_pos;
		output_write_pos = hdr.output_write_pos;
		hop_counter = hdr.hop_counter;
		primed = (hdr.primed != 0);
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "SpectralGate";
		desc.category = "Spectral";
		desc.inputs.push_back({ "audio_in", SymphonyPinType::AUDIO, false });
		desc.inputs.push_back({ "threshold_db", SymphonyPinType::FLOAT, false });
		desc.inputs.push_back({ "reduction_db", SymphonyPinType::FLOAT, false });
		desc.outputs.push_back({ "audio_out", SymphonyPinType::AUDIO, false });
		desc.params.push_back({ "fft_size", 2048.0f, 256.0f, 8192.0f, 1.0f });
		desc.params.push_back({ "overlap", 4.0f, 2.0f, 8.0f, 1.0f });
		desc.params.push_back({ "threshold_db", -40.0f, -96.0f, 0.0f, 0.1f });
		desc.params.push_back({ "reduction_db", -40.0f, -96.0f, 0.0f, 0.1f });
		desc.state_size = sizeof(SymphonySpectralGate);
		desc.state_align = alignof(SymphonySpectralGate);
		// Max fft_size=8192: input_ring 2N + output_ring 2N + window/cola/workspace/
		// analysis/fft/ifft (6×N) = 10N floats, plus alignment slack.
		desc.extra_arena_bytes = sizeof(float) * 81920 + 320;
		desc.cost_per_sample = 4.0f;
		desc.extra_cost_fn = &SymphonySpectralGate::extra_cost;
		desc.create_fn = &SymphonySpectralGate::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	[[nodiscard]] static float extra_cost(const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		(void)p_mix_rate;
		int32_t fft_sz = p_params.has("fft_size") ? (int32_t)(float)p_params["fft_size"] : 2048;
		fft_sz = CLAMP(fft_sz, 256, 8192);
		fft_sz = 1 << (int32_t)Math::floor(Math::log((float)fft_sz) / Math::log(2.0f));
		int32_t overlap = p_params.has("overlap") ? (int32_t)(float)p_params["overlap"] : 4;
		overlap = CLAMP(overlap, 2, 8);
		const int32_t hop = MAX(1, fft_sz / overlap);
		const float hops_in_block = (float)SYMPHONY_MICRO_BLOCK_SIZE / (float)hop;
		const float log2n = Math::log((float)fft_sz) / Math::log(2.0f);
		// Slightly cheaper than PhaseVocoder (no pitch remap / phase accumulate).
		return hops_in_block * (float)fft_sz * log2n * 0.7f;
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		void *mem = p_arena.alloc(sizeof(SymphonySpectralGate), alignof(SymphonySpectralGate));
		if (!mem) {
			return nullptr;
		}
		SymphonySpectralGate *sg = new (mem) SymphonySpectralGate();

		// --- Read parameters ---
		int32_t fft_sz = p_params.has("fft_size") ? (int32_t)(float)p_params["fft_size"] : 2048;
		// Clamp to valid power-of-2 range for pffft (minimum 32, we allow 256-8192)
		fft_sz = CLAMP(fft_sz, 256, 8192);
		// Round down to nearest power of 2
		fft_sz = 1 << (int32_t)Math::floor(Math::log((float)fft_sz) / Math::log(2.0f));
		sg->fft_size = fft_sz;

		sg->overlap = p_params.has("overlap") ? (int32_t)(float)p_params["overlap"] : 4;
		sg->overlap = CLAMP(sg->overlap, 2, 8);
		sg->hop_size = sg->fft_size / sg->overlap;

		sg->default_threshold_db = p_params.has("threshold_db") ? (float)p_params["threshold_db"] : -40.0f;
		sg->default_threshold_db = CLAMP(sg->default_threshold_db, -96.0f, 0.0f);
		sg->default_reduction_db = p_params.has("reduction_db") ? (float)p_params["reduction_db"] : -40.0f;
		sg->default_reduction_db = CLAMP(sg->default_reduction_db, -96.0f, 0.0f);

		// --- Create PFFFT setup (heap-allocated — small leak on arena free is acceptable) ---
		sg->pffft_setup = pffft_new_setup(sg->fft_size, PFFFT_REAL);
		if (!sg->pffft_setup) {
			return nullptr;
		}

		// --- Allocate arena buffers ---
		const int32_t N = sg->fft_size;

		sg->input_ring = (float *)p_arena.alloc(sizeof(float) * N * 2, 32);
		sg->output_ring = (float *)p_arena.alloc(sizeof(float) * N * 2, 32);
		sg->window_lut = (float *)p_arena.alloc(sizeof(float) * N, 32);
		sg->cola_gain = (float *)p_arena.alloc(sizeof(float) * N, 32);
		sg->fft_workspace = (float *)p_arena.alloc(sizeof(float) * N, 32);
		sg->analysis_frame = (float *)p_arena.alloc(sizeof(float) * N, 32);
		sg->fft_buffer = (float *)p_arena.alloc(sizeof(float) * N, 32);
		sg->ifft_buffer = (float *)p_arena.alloc(sizeof(float) * N, 32);

		// Verify all allocations succeeded
		if (!sg->input_ring || !sg->output_ring || !sg->window_lut || !sg->cola_gain ||
				!sg->fft_workspace || !sg->analysis_frame || !sg->fft_buffer ||
				!sg->ifft_buffer) {
			pffft_destroy_setup(sg->pffft_setup);
			sg->pffft_setup = nullptr;
			return nullptr;
		}

		// --- Zero all buffers ---
		memset(sg->input_ring, 0, sizeof(float) * N * 2);
		memset(sg->output_ring, 0, sizeof(float) * N * 2);
		memset(sg->fft_workspace, 0, sizeof(float) * N);
		memset(sg->analysis_frame, 0, sizeof(float) * N);
		memset(sg->fft_buffer, 0, sizeof(float) * N);
		memset(sg->ifft_buffer, 0, sizeof(float) * N);

		// --- Precompute Hanning window ---
		for (int32_t i = 0; i < N; i++) {
			float phase = (float)i / (float)(N - 1);
			sg->window_lut[i] = 0.5f * (1.0f - Math::cos(Math::TAU * phase));
		}

		// COLA normalization: Σ w² over hop-aligned overlaps (same as PhaseVocoder).
		for (int32_t i = 0; i < N; i++) {
			float sum = 0.0f;
			for (int32_t k = 0; k < sg->overlap; k++) {
				int32_t idx = (i + k * sg->hop_size) % N;
				sum += sg->window_lut[idx] * sg->window_lut[idx];
			}
			sg->cola_gain[i] = (sum > 1e-12f) ? (1.0f / sum) : 0.0f;
		}

		// --- Initialize processing state ---
		sg->input_write_pos = 0;
		sg->input_samples_fed = 0;
		sg->output_read_pos = 0;
		sg->output_write_pos = 0;
		sg->hop_counter = 0;
		sg->primed = false;

		return sg;
	}
};
