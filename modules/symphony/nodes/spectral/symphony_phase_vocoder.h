#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "../../core/symphony_runtime_metrics.h"
#include "core/math/math_funcs.h"
#include "pffft.h"

#include <cstdint>

// PhaseVocoder: Real-time time-stretching and pitch-shifting via STFT.
//
// Algorithm:
//   1. Accumulate input into a ring buffer.
//   2. Every synthesis hop, perform analysis at time-stretched position:
//      - Window → FFT → magnitude/phase → phase unwrap → pitch shift →
//        phase accumulate → IFFT → window → overlap-add.
//   3. Read output from the overlap-add buffer at normal rate.
//
// Time-stretch ratio controls output duration relative to input:
//   time_stretch = 2.0 → output is 2× longer (half-speed playback).
//   time_stretch = 0.5 → output is half as long (double-speed playback).
//   time_stretch = 1.0 → normal speed.
// This matches the DAW convention (Ableton, Pro Tools, etc.).
//
// Pitch-shift (in semitones) re-maps frequency bins before resynthesis.
//
// NOTE: PFFFT_Setup* is heap-allocated by pffft_new_setup() and cannot live in
// the arena. It will leak when the arena is freed. This is acceptable because
// graph destruction is infrequent and the setup is small (~few KB).
class SymphonyPhaseVocoder : public SymphonyOperator {
private:
	// --- Pin pointers (bound by compiler) ---
	const float *SYMPHONY_RESTRICT audio_in = nullptr;
	const float *SYMPHONY_RESTRICT time_stretch_input = nullptr;
	const float *SYMPHONY_RESTRICT pitch_shift_input = nullptr;
	float *SYMPHONY_RESTRICT audio_out = nullptr;

	// --- FFT setup (heap-allocated, see note above) ---
	PFFFT_Setup *pffft_setup = nullptr;

	// --- Arena-allocated buffers ---
	float *input_ring = nullptr;      // [fft_size * 2] circular input buffer (double for wraparound-free reads)
	float *output_ring = nullptr;     // [fft_size * 2] overlap-add accumulator
	float *window_lut = nullptr; // [fft_size] precomputed Hanning window
	float *cola_gain = nullptr; // [fft_size] 1 / Σ w² (COLA normalize)
	float *fft_workspace = nullptr; // [fft_size] pffft work buffer
	float *analysis_frame = nullptr;  // [fft_size] windowed frame for FFT input
	float *fft_buffer = nullptr;      // [fft_size] FFT output (interleaved complex)
	float *ifft_buffer = nullptr;     // [fft_size] IFFT output
	float *prev_phase = nullptr;      // [fft_size/2 + 1] previous analysis phases
	float *synth_phase = nullptr;     // [fft_size/2 + 1] accumulated synthesis phases
	float *magnitude_buf = nullptr;   // [fft_size/2 + 1] current frame magnitudes
	float *shifted_mag = nullptr;     // [fft_size/2 + 1] pitch-shifted magnitudes
	float *shifted_phase = nullptr;   // [fft_size/2 + 1] pitch-shifted unwrapped deltas

	// --- Parameters ---
	int32_t fft_size = 2048;
	int32_t overlap = 4;
	int32_t hop_size = 0;             // = fft_size / overlap (analysis & synthesis hop)
	int32_t num_bins = 0;             // = fft_size / 2 + 1
	float mix_rate = 44100.0f;
	float default_time_stretch = 1.0f;
	float default_pitch_shift = 0.0f;

	// --- Processing state (absolute 64-bit timeline positions, plan §9) ---
	uint64_t input_abs_write = 0; // Total samples written (never wraps)
	uint64_t analysis_abs = 0; // Absolute analysis frame start
	int32_t input_write_pos = 0; // input_abs_write % ring_size (cached)
	int32_t output_read_pos = 0;
	int32_t output_write_pos = 0;
	int32_t synth_counter = 0;
	bool primed = false;

	// --- Helpers ---

	// Wrap phase to [-PI, PI]
	inline float wrap_phase(float p_phase) const {
		// Fast wrapping using fmod-like approach
		while (p_phase > Math::PI) {
			p_phase -= Math::TAU;
		}
		while (p_phase < -Math::PI) {
			p_phase += Math::TAU;
		}
		return p_phase;
	}

	// Process one full STFT analysis-modify-resynthesize frame
	void process_frame(float p_time_stretch, float p_pitch_shift) {
		if (!pffft_setup) {
			return;
		}
		const float pitch_ratio = Math::pow(2.0f, p_pitch_shift / 12.0f);
		const float expected_phase_advance = Math::TAU * (float)hop_size / (float)fft_size;
		const int32_t ring_size = fft_size * 2;

		// Clamp analysis window into the valid ring span [write-ring_size, write).
		const uint64_t earliest = (input_abs_write > (uint64_t)ring_size) ? (input_abs_write - (uint64_t)ring_size) : 0;
		if (analysis_abs < earliest) {
			analysis_abs = earliest;
		}
		if (analysis_abs + (uint64_t)fft_size > input_abs_write) {
			// Incomplete FFT window — skip this hop (do not read unwritten/overwritten data).
			symphony_note_spectral_underflow();
			return;
		}

		const int32_t read_start = (int32_t)(analysis_abs % (uint64_t)ring_size);
		analysis_abs += (uint64_t)MAX(1, (int32_t)Math::round((1.0f / p_time_stretch) * (float)hop_size));

		for (int32_t i = 0; i < fft_size; i++) {
			int32_t idx = (read_start + i) % ring_size;
			analysis_frame[i] = input_ring[idx] * window_lut[i];
		}

		pffft_transform_ordered(pffft_setup, analysis_frame, fft_buffer, fft_workspace, PFFFT_FORWARD);

		// --- Step 4: Convert to magnitude/phase and compute unwrapped deltas ---
		// pffft ordered output for PFFFT_REAL:
		//   fft_buffer[0] = DC (real), fft_buffer[1] = Nyquist (real)
		//   fft_buffer[2k], fft_buffer[2k+1] = real, imag of bin k (k=1..N/2-1)
		//
		// We store unwrapped phase deltas in ifft_buffer (reused as temp, overwritten in step 8).
		// This avoids the aliasing problem with shifted_phase.
		float *unwrapped_delta = ifft_buffer; // Safe: ifft_buffer is overwritten in step 8.

		// DC bin (index 0)
		magnitude_buf[0] = Math::abs(fft_buffer[0]);
		{
			float current_phase_dc = (fft_buffer[0] >= 0.0f) ? 0.0f : Math::PI;
			float delta = current_phase_dc - prev_phase[0]; // expected advance for DC = 0
			unwrapped_delta[0] = wrap_phase(delta);
			prev_phase[0] = current_phase_dc;
		}

		// Nyquist bin (index N/2)
		magnitude_buf[num_bins - 1] = Math::abs(fft_buffer[1]);
		{
			float current_phase_nyquist = (fft_buffer[1] >= 0.0f) ? 0.0f : Math::PI;
			float expected_nyquist = (float)(num_bins - 1) * expected_phase_advance;
			float delta = current_phase_nyquist - prev_phase[num_bins - 1] - expected_nyquist;
			unwrapped_delta[num_bins - 1] = wrap_phase(delta);
			prev_phase[num_bins - 1] = current_phase_nyquist;
		}

		// Bins 1..N/2-1
		for (int32_t k = 1; k < num_bins - 1; k++) {
			float re = fft_buffer[2 * k];
			float im = fft_buffer[2 * k + 1];
			float mag_sq = re * re + im * im;
			// Denormal-safe sqrt: skip for near-zero bins to avoid x86 penalty.
			magnitude_buf[k] = mag_sq > 1e-30f ? Math::sqrt(mag_sq) : 0.0f;
			float phase = Math::atan2(im, re);

			float expected = (float)k * expected_phase_advance;
			float delta = phase - prev_phase[k] - expected;
			unwrapped_delta[k] = wrap_phase(delta);
			prev_phase[k] = phase;
		}

		// --- Step 5: Pitch shifting (bin remapping) ---
		// Clear shifted buffers
		for (int32_t k = 0; k < num_bins; k++) {
			shifted_mag[k] = 0.0f;
			shifted_phase[k] = 0.0f;
		}

		if (Math::abs(pitch_ratio - 1.0f) < 0.001f) {
			// No pitch shift — copy directly
			for (int32_t k = 0; k < num_bins; k++) {
				shifted_mag[k] = magnitude_buf[k];
				shifted_phase[k] = unwrapped_delta[k];
			}
		} else {
			// Pitch shift: bin k maps to destination bin k*pitch_ratio
			for (int32_t k = 0; k < num_bins; k++) {
				float dest_bin_f = (float)k * pitch_ratio;
				int32_t dest_bin = (int32_t)(dest_bin_f + 0.5f);
				if (dest_bin >= 0 && dest_bin < num_bins) {
					shifted_mag[dest_bin] += magnitude_buf[k];
					shifted_phase[dest_bin] = unwrapped_delta[k] * pitch_ratio;
				}
			}
		}

		// --- Step 6: Phase accumulation for synthesis ---
		for (int32_t k = 0; k < num_bins; k++) {
			float expected = (float)k * expected_phase_advance;
			synth_phase[k] += expected + shifted_phase[k];
			// Keep synth_phase bounded to avoid float precision loss over time
			synth_phase[k] = wrap_phase(synth_phase[k]);
		}

		// --- Step 7: Convert back to interleaved complex ---
		// DC
		fft_buffer[0] = shifted_mag[0] * Math::cos(synth_phase[0]);
		// Nyquist
		fft_buffer[1] = shifted_mag[num_bins - 1] * Math::cos(synth_phase[num_bins - 1]);
		// Bins 1..N/2-1
		for (int32_t k = 1; k < num_bins - 1; k++) {
			fft_buffer[2 * k] = shifted_mag[k] * Math::cos(synth_phase[k]);
			fft_buffer[2 * k + 1] = shifted_mag[k] * Math::sin(synth_phase[k]);
		}

		// --- Step 8: Inverse FFT ---
		pffft_transform_ordered(pffft_setup, fft_buffer, ifft_buffer, fft_workspace, PFFFT_BACKWARD);

		// --- Step 9: Scale by 1/N, synthesis window, and COLA gain ---
		const float inv_fft_size = 1.0f / (float)fft_size;
		for (int32_t i = 0; i < fft_size; i++) {
			float g = cola_gain ? cola_gain[i] : 1.0f;
			ifft_buffer[i] *= inv_fft_size * window_lut[i] * g;
		}

		// --- Step 10: Overlap-add into output ring buffer ---
		int32_t ring_out_size = fft_size * 2;
		for (int32_t i = 0; i < fft_size; i++) {
			int32_t idx = (output_write_pos + i) % ring_out_size;
			output_ring[idx] += ifft_buffer[i];
		}

		// Advance output write position by hop_size
		output_write_pos = (output_write_pos + hop_size) % ring_out_size;
	}

public:
	SymphonyPhaseVocoder() {}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		audio_in = (const float *)p_input_ptrs[0];
		time_stretch_input = (const float *)p_input_ptrs[1];
		pitch_shift_input = (const float *)p_input_ptrs[2];
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
		float time_stretch = time_stretch_input ? *time_stretch_input : default_time_stretch;
		float pitch_shift = pitch_shift_input ? *pitch_shift_input : default_pitch_shift;

		time_stretch = CLAMP(time_stretch, 0.5f, 2.0f);
		pitch_shift = CLAMP(pitch_shift, -12.0f, 12.0f);

		const int32_t ring_in_size = fft_size * 2;
		const int32_t ring_out_size = fft_size * 2;

		for (int32_t i = 0; i < p_num_frames; i++) {
			float in_sample = audio_in ? audio_in[i] : 0.0f;
			input_ring[input_write_pos] = in_sample;
			input_write_pos = (input_write_pos + 1) % ring_in_size;
			input_abs_write++;

			if (!primed && input_abs_write >= (uint64_t)fft_size) {
				primed = true;
				analysis_abs = input_abs_write - (uint64_t)fft_size;
			}

			synth_counter++;
			if (primed && synth_counter >= hop_size) {
				synth_counter = 0;
				process_frame(time_stretch, pitch_shift);
			}

			if (primed) {
				audio_out[i] = output_ring[output_read_pos];
				output_ring[output_read_pos] = 0.0f;
				output_read_pos = (output_read_pos + 1) % ring_out_size;
			} else {
				audio_out[i] = 0.0f;
			}
		}
	}

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		struct StateHeader {
			uint64_t input_abs_write;
			uint64_t analysis_abs;
			int32_t input_write_pos;
			int32_t output_read_pos;
			int32_t output_write_pos;
			int32_t synth_counter;
			int32_t primed;
		};

		size_t phase_bytes = sizeof(float) * num_bins;
		size_t needed = sizeof(StateHeader) + phase_bytes * 2;
		if (!p_buffer) {
			return needed;
		}
		if (p_max_size < needed) {
			return 0;
		}

		StateHeader hdr;
		hdr.input_abs_write = input_abs_write;
		hdr.analysis_abs = analysis_abs;
		hdr.input_write_pos = input_write_pos;
		hdr.output_read_pos = output_read_pos;
		hdr.output_write_pos = output_write_pos;
		hdr.synth_counter = synth_counter;
		hdr.primed = primed ? 1 : 0;

		size_t offset = 0;
		memcpy(p_buffer + offset, &hdr, sizeof(StateHeader));
		offset += sizeof(StateHeader);
		memcpy(p_buffer + offset, prev_phase, phase_bytes);
		offset += phase_bytes;
		memcpy(p_buffer + offset, synth_phase, phase_bytes);
		offset += phase_bytes;

		return offset;
	}

	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		struct StateHeader {
			uint64_t input_abs_write;
			uint64_t analysis_abs;
			int32_t input_write_pos;
			int32_t output_read_pos;
			int32_t output_write_pos;
			int32_t synth_counter;
			int32_t primed;
		};

		size_t phase_bytes = sizeof(float) * num_bins;
		size_t needed = sizeof(StateHeader) + phase_bytes * 2;
		if (p_size < needed) {
			return;
		}

		StateHeader hdr;
		memcpy(&hdr, p_buffer, sizeof(StateHeader));
		input_abs_write = hdr.input_abs_write;
		analysis_abs = hdr.analysis_abs;
		input_write_pos = hdr.input_write_pos;
		output_read_pos = hdr.output_read_pos;
		output_write_pos = hdr.output_write_pos;
		synth_counter = hdr.synth_counter;
		primed = hdr.primed != 0;

		size_t offset = sizeof(StateHeader);
		memcpy(prev_phase, p_buffer + offset, phase_bytes);
		offset += phase_bytes;
		memcpy(synth_phase, p_buffer + offset, phase_bytes);
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "PhaseVocoder";
		desc.category = "Spectral";
		desc.inputs.push_back({ "audio_in", SymphonyPinType::AUDIO, false });
		desc.inputs.push_back({ "time_stretch", SymphonyPinType::FLOAT, false });
		desc.inputs.push_back({ "pitch_shift", SymphonyPinType::FLOAT, false });
		desc.outputs.push_back({ "audio_out", SymphonyPinType::AUDIO, false });
		desc.params.push_back({ "fft_size", 2048.0f, 256.0f, 8192.0f, 1.0f });
		desc.params.push_back({ "overlap", 4.0f, 2.0f, 8.0f, 1.0f });
		desc.params.push_back({ "time_stretch", 1.0f, 0.5f, 2.0f, 0.01f });
		desc.params.push_back({ "pitch_shift", 0.0f, -12.0f, 12.0f, 0.01f });
		desc.params.push_back({ "seed", 1.0f, 1.0f, 999999.0f, 1.0f });
		desc.state_size = sizeof(SymphonyPhaseVocoder);
		desc.state_align = alignof(SymphonyPhaseVocoder);
		// At max fft_size=8192: allocates 7 buffers of N floats (input_ring×2, output_ring×2,
		// window_lut, fft_workspace, analysis_frame, fft_buffer, ifft_buffer)
		// + 5 buffers of (N/2+1) floats (prev_phase, synth_phase, magnitude_buf, shifted_mag, shifted_phase).
		// Total: 7*8192 + 5*4097 = 77829 floats + cola_gain N. Plus alignment overhead.
		desc.extra_arena_bytes = sizeof(float) * (77829 + 8192) + 512;
		desc.cost_per_sample = 48.0f; // Spectral: roughly N·logN class work
		desc.create_fn = &SymphonyPhaseVocoder::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		void *mem = p_arena.alloc(sizeof(SymphonyPhaseVocoder), alignof(SymphonyPhaseVocoder));
		if (!mem) {
			return nullptr;
		}
		SymphonyPhaseVocoder *pv = new (mem) SymphonyPhaseVocoder();

		// --- Read parameters ---
		pv->mix_rate = p_mix_rate;

		int32_t fft_sz = p_params.has("fft_size") ? (int32_t)(float)p_params["fft_size"] : 2048;
		// Clamp to valid power-of-2 range for pffft (minimum 32, we allow 256-8192)
		fft_sz = CLAMP(fft_sz, 256, 8192);
		// Round down to nearest power of 2
		fft_sz = 1 << (int32_t)Math::floor(Math::log((float)fft_sz) / Math::log(2.0f));
		pv->fft_size = fft_sz;

		pv->overlap = p_params.has("overlap") ? (int32_t)(float)p_params["overlap"] : 4;
		pv->overlap = CLAMP(pv->overlap, 2, 8);
		pv->hop_size = pv->fft_size / pv->overlap;
		pv->num_bins = pv->fft_size / 2 + 1;

		pv->default_time_stretch = p_params.has("time_stretch") ? (float)p_params["time_stretch"] : 1.0f;
		pv->default_time_stretch = CLAMP(pv->default_time_stretch, 0.5f, 2.0f);
		pv->default_pitch_shift = p_params.has("pitch_shift") ? (float)p_params["pitch_shift"] : 0.0f;
		pv->default_pitch_shift = CLAMP(pv->default_pitch_shift, -12.0f, 12.0f);

		pv->pffft_setup = pffft_new_setup(pv->fft_size, PFFFT_REAL);
		if (!pv->pffft_setup) {
			return nullptr;
		}

		const int32_t N = pv->fft_size;
		const int32_t bins = pv->num_bins;

		pv->input_ring = (float *)p_arena.alloc(sizeof(float) * N * 2, 32);
		pv->output_ring = (float *)p_arena.alloc(sizeof(float) * N * 2, 32);
		pv->window_lut = (float *)p_arena.alloc(sizeof(float) * N, 32);
		pv->cola_gain = (float *)p_arena.alloc(sizeof(float) * N, 32);
		pv->fft_workspace = (float *)p_arena.alloc(sizeof(float) * N, 32);
		pv->analysis_frame = (float *)p_arena.alloc(sizeof(float) * N, 32);
		pv->fft_buffer = (float *)p_arena.alloc(sizeof(float) * N, 32);
		pv->ifft_buffer = (float *)p_arena.alloc(sizeof(float) * N, 32);
		pv->prev_phase = (float *)p_arena.alloc(sizeof(float) * bins, 32);
		pv->synth_phase = (float *)p_arena.alloc(sizeof(float) * bins, 32);
		pv->magnitude_buf = (float *)p_arena.alloc(sizeof(float) * bins, 32);
		pv->shifted_mag = (float *)p_arena.alloc(sizeof(float) * bins, 32);
		pv->shifted_phase = (float *)p_arena.alloc(sizeof(float) * bins, 32);

		if (!pv->input_ring || !pv->output_ring || !pv->window_lut || !pv->cola_gain ||
				!pv->fft_workspace || !pv->analysis_frame || !pv->fft_buffer ||
				!pv->ifft_buffer || !pv->prev_phase || !pv->synth_phase ||
				!pv->magnitude_buf || !pv->shifted_mag || !pv->shifted_phase) {
			pffft_destroy_setup(pv->pffft_setup);
			pv->pffft_setup = nullptr;
			return nullptr;
		}

		memset(pv->input_ring, 0, sizeof(float) * N * 2);
		memset(pv->output_ring, 0, sizeof(float) * N * 2);
		memset(pv->fft_workspace, 0, sizeof(float) * N);
		memset(pv->analysis_frame, 0, sizeof(float) * N);
		memset(pv->fft_buffer, 0, sizeof(float) * N);
		memset(pv->ifft_buffer, 0, sizeof(float) * N);
		memset(pv->prev_phase, 0, sizeof(float) * bins);
		memset(pv->synth_phase, 0, sizeof(float) * bins);
		memset(pv->magnitude_buf, 0, sizeof(float) * bins);
		memset(pv->shifted_mag, 0, sizeof(float) * bins);
		memset(pv->shifted_phase, 0, sizeof(float) * bins);

		for (int32_t i = 0; i < N; i++) {
			float phase = (float)i / (float)(N - 1);
			pv->window_lut[i] = 0.5f * (1.0f - Math::cos(Math::TAU * phase));
		}

		// COLA normalization: Σ w² over hop-aligned overlaps.
		for (int32_t i = 0; i < N; i++) {
			float sum = 0.0f;
			for (int32_t k = 0; k < pv->overlap; k++) {
				int32_t idx = (i + k * pv->hop_size) % N;
				sum += pv->window_lut[idx] * pv->window_lut[idx];
			}
			pv->cola_gain[i] = (sum > 1e-12f) ? (1.0f / sum) : 0.0f;
		}

		pv->input_abs_write = 0;
		pv->analysis_abs = 0;
		pv->input_write_pos = 0;
		pv->output_read_pos = 0;
		pv->output_write_pos = 0;
		pv->synth_counter = 0;
		pv->primed = false;

		return pv;
	}
};
