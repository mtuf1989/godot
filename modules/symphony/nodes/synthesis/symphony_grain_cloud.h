#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "core/math/math_funcs.h"

// GrainCloud: Internal micro-scheduler managing overlapping grains.
// Supports both live audio input granulation and source buffer mode.
//
// Each grain is a windowed (Hanning) segment of the source, with:
// - configurable size (10-200ms)
// - stochastic scheduling (Poisson-like based on density)
// - per-grain pitch randomization
// - overlap-add output
//
// Source modes:
// - Live input: granulates the audio_in signal via an internal capture buffer
// - Buffer mode: reads from an internal circular buffer filled by audio_in
//   Position parameter scans through the buffer for time-stretch effects
//
// Max 8 concurrent grains to bound CPU usage.
class SymphonyGrainCloud : public SymphonyOperator {
private:
	static constexpr int32_t MAX_GRAINS = 8;
	static constexpr int32_t CAPTURE_BUFFER_SECONDS = 4; // 4 seconds of capture

	// Grain state
	struct Grain {
		bool active = false;
		int32_t start_sample = 0;     // Where in capture buffer this grain reads from
		int32_t length_samples = 0;   // Grain duration in samples
		int32_t current_sample = 0;   // Current playback position within grain
		float playback_rate = 1.0f;   // Pitch variation (1.0 = normal)
		float read_pos = 0.0f;        // Fractional read position for pitch shifting
	};

	const float *__restrict__ audio_in = nullptr;
	const float *__restrict__ grain_size_input = nullptr;    // Float: ms
	const float *__restrict__ density_input = nullptr;       // Float: grains/sec
	const float *__restrict__ position_input = nullptr;      // Float: 0-1 buffer position
	const float *__restrict__ pitch_rand_input = nullptr;    // Float: 0-0.5 semitone deviation
	float *__restrict__ audio_out = nullptr;

	// Capture buffer (circular, holds source audio)
	float *capture_buffer = nullptr;
	int32_t capture_size = 0;     // Total capture buffer size in samples
	int32_t capture_write = 0;    // Current write position

	// Grain pool
	Grain grains[MAX_GRAINS];

	// Scheduling state
	float schedule_counter = 0.0f; // Counts down to next grain spawn

	// Hanning window lookup table
	float *hanning_table = nullptr;
	int32_t hanning_size = 0;

	// Parameters
	float mix_rate = 44100.0f;
	float default_grain_size_ms = 50.0f;
	float default_density = 8.0f;
	float default_position = 0.5f;
	float default_pitch_randomness = 0.0f;

	// Fast PRNG (xorshift32)
	uint32_t rng_state = 1;

	inline uint32_t xorshift32() {
		rng_state ^= rng_state << 13;
		rng_state ^= rng_state >> 17;
		rng_state ^= rng_state << 5;
		return rng_state;
	}

	inline float random_float() {
		// Returns [0, 1)
		return (float)(xorshift32() & 0x7FFFFF) / (float)0x800000;
	}

	inline float hanning_window(float phase) const {
		// phase: 0.0 to 1.0 through the grain
		// Lookup in precomputed table with linear interpolation
		float pos = phase * (float)(hanning_size - 1);
		int32_t idx = (int32_t)pos;
		if (idx >= hanning_size - 1) return 0.0f;
		float frac = pos - (float)idx;
		return hanning_table[idx] + frac * (hanning_table[idx + 1] - hanning_table[idx]);
	}

	inline float read_capture(float pos) const {
		// Read from capture buffer with linear interpolation
		while (pos < 0.0f) pos += (float)capture_size;
		while (pos >= (float)capture_size) pos -= (float)capture_size;

		int32_t idx = (int32_t)pos;
		float frac = pos - (float)idx;
		int32_t next = (idx + 1) % capture_size;

		return capture_buffer[idx] + frac * (capture_buffer[next] - capture_buffer[idx]);
	}

public:
	SymphonyGrainCloud() {}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		audio_in = (const float *)p_input_ptrs[0];
		grain_size_input = (const float *)p_input_ptrs[1];
		density_input = (const float *)p_input_ptrs[2];
		position_input = (const float *)p_input_ptrs[3];
		pitch_rand_input = (const float *)p_input_ptrs[4];
		audio_out = (float *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		SYMPHONY_ASSUME_FRAMES(p_num_frames);

		// Read parameters (control-rate: once per micro-block)
		float grain_size_ms = grain_size_input ? *grain_size_input : default_grain_size_ms;
		float density = density_input ? *density_input : default_density;
		float position = position_input ? *position_input : default_position;
		float pitch_rand = pitch_rand_input ? *pitch_rand_input : default_pitch_randomness;

		grain_size_ms = CLAMP(grain_size_ms, 10.0f, 200.0f);
		density = CLAMP(density, 0.0f, 50.0f);
		position = CLAMP(position, 0.0f, 1.0f);
		pitch_rand = CLAMP(pitch_rand, 0.0f, 0.5f);

		int32_t grain_length = (int32_t)(grain_size_ms * mix_rate * 0.001f);
		if (grain_length < 1) grain_length = 1;

		// Average interval between grains (in samples)
		float avg_interval = (density > 0.001f) ? (mix_rate / density) : 1000000.0f;

		for (int32_t i = 0; i < p_num_frames; i++) {
			// Write input to capture buffer (always capture, even if input is silence)
			if (audio_in) {
				capture_buffer[capture_write] = audio_in[i];
			} else {
				capture_buffer[capture_write] = 0.0f;
			}
			capture_write = (capture_write + 1) % capture_size;

			// --- Grain scheduling (Poisson-like) ---
			schedule_counter -= 1.0f;
			if (schedule_counter <= 0.0f) {
				// Spawn a new grain
				spawn_grain(grain_length, position, pitch_rand);

				// Next grain interval with exponential randomization (Poisson process)
				// -log(uniform) gives exponential distribution
				float u = random_float();
				if (u < 0.0001f) u = 0.0001f; // Avoid log(0)
				schedule_counter = -Math::log(u) * avg_interval;
			}

			// --- Grain processing: overlap-add ---
			float output = 0.0f;

			for (int32_t g = 0; g < MAX_GRAINS; g++) {
				Grain &grain = grains[g];
				if (!grain.active) continue;

				// Grain phase for windowing (0 to 1)
				float phase = (float)grain.current_sample / (float)grain.length_samples;
				float window = hanning_window(phase);

				// Read from capture buffer at grain's position
				float read_sample_pos = (float)grain.start_sample + grain.read_pos;
				float sample = read_capture(read_sample_pos);

				output += sample * window;

				// Advance grain
				grain.read_pos += grain.playback_rate;
				grain.current_sample++;

				// Deactivate grain when done
				if (grain.current_sample >= grain.length_samples) {
					grain.active = false;
				}
			}

			audio_out[i] = output;
		}
	}

	void spawn_grain(int32_t length_samples, float position, float pitch_rand) {
		// Find a free grain slot
		int32_t slot = -1;
		for (int32_t g = 0; g < MAX_GRAINS; g++) {
			if (!grains[g].active) {
				slot = g;
				break;
			}
		}
		if (slot < 0) return; // All slots busy

		Grain &grain = grains[slot];
		grain.active = true;
		grain.length_samples = length_samples;
		grain.current_sample = 0;
		grain.read_pos = 0.0f;

		// Position in capture buffer (with randomization)
		float pos_randomness = 0.1f; // Fixed 10% position jitter
		float jitter = (random_float() - 0.5f) * 2.0f * pos_randomness;
		float final_pos = CLAMP(position + jitter, 0.0f, 1.0f);

		// Map position [0,1] to capture buffer offset behind write head
		// position=0: read from oldest data, position=1: read from newest
		int32_t offset_behind = (int32_t)((1.0f - final_pos) * (float)(capture_size - length_samples));
		grain.start_sample = (capture_write - offset_behind + capture_size) % capture_size;

		// Pitch randomization: semitone deviation
		float pitch_deviation = (random_float() - 0.5f) * 2.0f * pitch_rand;
		grain.playback_rate = Math::pow(2.0f, pitch_deviation / 12.0f);
	}

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		// Export: capture_write pos, schedule_counter, grain states, rng_state
		size_t needed = sizeof(int32_t) + sizeof(float) + sizeof(uint32_t) + sizeof(Grain) * MAX_GRAINS;
		if (!p_buffer) return needed;
		if (p_max_size < needed) return 0;

		size_t offset = 0;
		memcpy(p_buffer + offset, &capture_write, sizeof(int32_t));
		offset += sizeof(int32_t);
		memcpy(p_buffer + offset, &schedule_counter, sizeof(float));
		offset += sizeof(float);
		memcpy(p_buffer + offset, &rng_state, sizeof(uint32_t));
		offset += sizeof(uint32_t);
		memcpy(p_buffer + offset, grains, sizeof(Grain) * MAX_GRAINS);
		return offset + sizeof(Grain) * MAX_GRAINS;
	}

	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		size_t needed = sizeof(int32_t) + sizeof(float) + sizeof(uint32_t) + sizeof(Grain) * MAX_GRAINS;
		if (p_size < needed) return;

		size_t offset = 0;
		memcpy(&capture_write, p_buffer + offset, sizeof(int32_t));
		offset += sizeof(int32_t);
		memcpy(&schedule_counter, p_buffer + offset, sizeof(float));
		offset += sizeof(float);
		memcpy(&rng_state, p_buffer + offset, sizeof(uint32_t));
		offset += sizeof(uint32_t);
		memcpy(grains, p_buffer + offset, sizeof(Grain) * MAX_GRAINS);
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "GrainCloud";
		desc.category = "Synthesis";
		desc.inputs.push_back({ "audio_in", SymphonyPinType::AUDIO, false });
		desc.inputs.push_back({ "grain_size_ms", SymphonyPinType::FLOAT, false });
		desc.inputs.push_back({ "density", SymphonyPinType::FLOAT, false });
		desc.inputs.push_back({ "position", SymphonyPinType::FLOAT, false });
		desc.inputs.push_back({ "pitch_randomness", SymphonyPinType::FLOAT, false });
		desc.outputs.push_back({ "audio_out", SymphonyPinType::AUDIO, false });
		desc.params.push_back({ "grain_size_ms", 50.0f, 10.0f, 200.0f, 1.0f });
		desc.params.push_back({ "density", 8.0f, 0.0f, 50.0f, 0.1f });
		desc.params.push_back({ "position", 0.5f, 0.0f, 1.0f, 0.01f });
		desc.params.push_back({ "pitch_randomness", 0.0f, 0.0f, 0.5f, 0.01f });
		desc.params.push_back({ "seed", 1.0f, 1.0f, 999999.0f, 1.0f });
		desc.params.push_back({ "capture_seconds", 4.0f, 1.0f, 10.0f, 1.0f });
		desc.state_size = sizeof(SymphonyGrainCloud);
		desc.state_align = alignof(SymphonyGrainCloud);
		desc.create_fn = &SymphonyGrainCloud::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		void *mem = p_arena.alloc(sizeof(SymphonyGrainCloud), alignof(SymphonyGrainCloud));
		if (!mem) return nullptr;
		SymphonyGrainCloud *gc = new (mem) SymphonyGrainCloud();

		gc->mix_rate = p_mix_rate;
		gc->default_grain_size_ms = p_params.has("grain_size_ms") ? (float)p_params["grain_size_ms"] : 50.0f;
		gc->default_density = p_params.has("density") ? (float)p_params["density"] : 8.0f;
		gc->default_position = p_params.has("position") ? (float)p_params["position"] : 0.5f;
		gc->default_pitch_randomness = p_params.has("pitch_randomness") ? (float)p_params["pitch_randomness"] : 0.0f;

		// RNG seed (different per voice for variety)
		gc->rng_state = p_params.has("seed") ? (uint32_t)(float)p_params["seed"] : 1;
		if (gc->rng_state == 0) gc->rng_state = 1; // xorshift cannot have 0 state

		// Capture buffer
		float capture_sec = p_params.has("capture_seconds") ? (float)p_params["capture_seconds"] : 4.0f;
		capture_sec = CLAMP(capture_sec, 1.0f, 10.0f);
		gc->capture_size = (int32_t)(capture_sec * p_mix_rate);
		gc->capture_buffer = (float *)p_arena.alloc(sizeof(float) * gc->capture_size, 32);
		if (!gc->capture_buffer) return nullptr;
		memset(gc->capture_buffer, 0, sizeof(float) * gc->capture_size);
		gc->capture_write = 0;

		// Hanning window lookup table (1024 entries)
		gc->hanning_size = 1024;
		gc->hanning_table = (float *)p_arena.alloc(sizeof(float) * gc->hanning_size, alignof(float));
		if (!gc->hanning_table) return nullptr;
		for (int32_t i = 0; i < gc->hanning_size; i++) {
			float phase = (float)i / (float)(gc->hanning_size - 1);
			gc->hanning_table[i] = 0.5f * (1.0f - Math::cos(2.0f * (float)Math::PI * phase));
		}

		// Initialize grains as inactive
		for (int32_t g = 0; g < MAX_GRAINS; g++) {
			gc->grains[g].active = false;
		}

		// Schedule first grain quickly
		gc->schedule_counter = gc->random_float() * (p_mix_rate / gc->default_density);

		return gc;
	}
};
