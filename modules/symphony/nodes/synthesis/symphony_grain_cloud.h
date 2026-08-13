#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "../../core/symphony_checked_math.h"
#include "../../core/symphony_trigger.h"
#include "../../core/shared_pcm_cache.h"
#include "core/math/math_funcs.h"
#include "core/os/os.h"

#include <limits>

// GrainCloud: Internal micro-scheduler managing overlapping grains.
// Supports both live audio input granulation and source buffer mode.
//
// Each grain is a windowed (Hanning) segment of the source, with:
// - configurable size (10-200ms)
// - stochastic scheduling (Poisson-like based on density)
// - external trigger-driven spawning (sample-accurate via block-splitting)
// - per-grain pitch randomization (± semitones)
// - per-grain amplitude randomization (± dB)
// - overlap-add output
//
// Trigger input:
// - When trigger_only=false (default): external triggers spawn grains IN ADDITION to
//   the internal Poisson scheduler. Both sources coexist.
// - When trigger_only=true: internal scheduler is disabled; grains spawn ONLY from
//   external trigger events. Use with Clock for beat-synced granular.
// - Trigger value is currently unused (reserved for future velocity→amplitude mapping).
// - Sample-accurate: grains spawn at the exact sample offset of the trigger event.
//
// Source modes:
// - Live input: granulates the audio_in signal via an internal capture buffer
// - Buffer mode (S4.3): When source PCM data is loaded into the capture buffer
//   at compile time (via source_pcm parameter), the operator reads from pre-loaded
//   data. Position parameter scans through the buffer for time-stretch effects.
//
// S4.3 Enhancements:
// - scan_speed: automatically advances position through the buffer (0=manual, 1=normal)
// - amp_randomness: per-grain amplitude variation in dB
// - position_randomness: configurable jitter (was hardcoded at 10%)
// - pitch_tracking: YIN-based pitch detection for pitch-following grain playback
//
// Pan randomization: Not implemented here (mono output architecture).
// Use a downstream Panner node driven by GrainCloud's position for stereo spread.
//
// Max 8 concurrent grains to bound CPU usage.
class SymphonyGrainCloud : public SymphonyOperator {
private:
	static constexpr int32_t MAX_GRAINS = 8;
	static constexpr int32_t CAPTURE_BUFFER_SECONDS = 4;

	// Grain state
	struct Grain {
		bool active = false;
		int32_t start_sample = 0;     // Where in capture buffer this grain reads from
		int32_t length_samples = 0;   // Grain duration in samples
		int32_t current_sample = 0;   // Current playback position within grain
		float playback_rate = 1.0f;   // Pitch variation (1.0 = normal)
		float read_pos = 0.0f;        // Fractional read position for pitch shifting
		float amplitude = 1.0f;       // Per-grain amplitude (S4.3)
	};

	const float *SYMPHONY_RESTRICT audio_in = nullptr;
	const TriggerBuffer *SYMPHONY_RESTRICT trigger_in = nullptr;  // Trigger: external grain spawn
	const float *SYMPHONY_RESTRICT grain_size_input = nullptr;    // Float: ms
	const float *SYMPHONY_RESTRICT density_input = nullptr;       // Float: grains/sec
	const float *SYMPHONY_RESTRICT position_input = nullptr;      // Float: 0-1 buffer position
	const float *SYMPHONY_RESTRICT pitch_rand_input = nullptr;    // Float: 0-0.5 semitone deviation
	const float *SYMPHONY_RESTRICT scan_speed_input = nullptr;    // Float: 0-4 scan speed (S4.3)
	const float *SYMPHONY_RESTRICT amp_rand_input = nullptr;      // Float: 0-6 dB variation (S4.3)
	const float *SYMPHONY_RESTRICT pitch_tracking_input = nullptr; // Float: 0-1 pitch following (S4.3)
	float *SYMPHONY_RESTRICT audio_out = nullptr;

	// Capture buffer (circular, holds source audio)
	float *capture_buffer = nullptr;
	int32_t capture_size = 0;     // Total capture buffer size in samples
	int32_t capture_write = 0;    // Current write position

	// Source buffer mode (S4.3): if source_pcm_loaded, capture_buffer is pre-filled
	bool source_pcm_loaded = false;
	int32_t source_pcm_length = 0; // Length of pre-loaded PCM (may be less than capture_size)
	bool using_shared_pcm = false;  // True if capture_buffer points to SharedPCMCache data
	StringName shared_pcm_key;     // Cache key for release in cleanup()

	// Grain pool
	Grain grains[MAX_GRAINS];

	// Scheduling state
	float schedule_counter = 0.0f; // Counts down to next grain spawn

	// Auto-scan position (S4.3): accumulates when scan_speed > 0
	float auto_position = 0.0f;   // [0, 1] — automatically advancing scan position

	// Hanning window lookup table
	float *hanning_table = nullptr;
	int32_t hanning_size = 0;

	// Parameters
	float mix_rate = 44100.0f;
	float default_grain_size_ms = 50.0f;
	float default_density = 8.0f;
	float default_position = 0.5f;
	float default_pitch_randomness = 0.0f;
	float default_scan_speed = 0.0f;         // S4.3: 0 = manual position control
	float default_amp_randomness = 0.0f;     // S4.3: 0 = no amplitude variation
	float default_position_randomness = 0.1f; // S4.3: configurable (was hardcoded 10%)
	float default_pitch_tracking = 0.0f;     // S4.3: 0 = no pitch following
	bool trigger_only = false;               // When true, disable internal Poisson scheduler; spawn grains only from trigger input

	// Pitch detection state (S4.3: YIN-based)
	float detected_pitch_hz = 0.0f;          // Cached pitch from last detection
	int32_t pitch_update_counter = 0;        // Counts samples until next pitch update
	static constexpr int32_t PITCH_UPDATE_INTERVAL = 2048; // ~42ms at 48kHz
	static constexpr int32_t PITCH_WINDOW_SIZE = 1024;     // Analysis window
	static constexpr float PITCH_REFERENCE_HZ = 440.0f;    // Reference pitch (A4)

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
		float pos = phase * (float)(hanning_size - 1);
		int32_t idx = (int32_t)pos;
		if (idx >= hanning_size - 1) {
			return 0.0f;
		}
		float frac = pos - (float)idx;
		return hanning_table[idx] + frac * (hanning_table[idx + 1] - hanning_table[idx]);
	}

	inline float read_capture(float pos) const {
		// Read from capture buffer with linear interpolation
		int32_t buf_len = source_pcm_loaded ? source_pcm_length : capture_size;
		while (pos < 0.0f) {
			pos += (float)buf_len;
		}
		while (pos >= (float)buf_len) {
			pos -= (float)buf_len;
		}

		int32_t idx = (int32_t)pos;
		float frac = pos - (float)idx;
		int32_t next = (idx + 1) % buf_len;

		return capture_buffer[idx] + frac * (capture_buffer[next] - capture_buffer[idx]);
	}

	// S4.3: YIN pitch detection — lightweight, runs periodically
	// Computes CMND (cumulative mean normalized difference) and finds first
	// valley below threshold to determine fundamental frequency.
	float detect_pitch(int32_t center_pos, int32_t window_size) const {
		const int32_t max_lag = window_size / 2;
		const int32_t min_lag = (int32_t)(mix_rate / 2000.0f); // 2kHz max pitch
		const int32_t max_lag_clamped = MIN(max_lag, (int32_t)(mix_rate / 50.0f)); // 50Hz min pitch
		const float threshold = 0.15f;

		float running_sum = 0.0f;

		for (int32_t tau = 1; tau <= max_lag_clamped; tau++) {
			float diff = 0.0f;
			for (int32_t j = 0; j < window_size - max_lag_clamped; j++) {
				float d = read_capture((float)(center_pos + j)) - read_capture((float)(center_pos + j + tau));
				diff += d * d;
			}
			running_sum += diff;
			float cmnd = (running_sum > 0.0f) ? (diff * (float)tau / running_sum) : 1.0f;

			if (tau >= min_lag && cmnd < threshold) {
				// Found the pitch period
				return mix_rate / (float)tau;
			}
		}
		return 0.0f; // No pitch detected
	}

public:
	SymphonyGrainCloud() {}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		audio_in = (const float *)p_input_ptrs[0];
		trigger_in = (const TriggerBuffer *)p_input_ptrs[1];
		grain_size_input = (const float *)p_input_ptrs[2];
		density_input = (const float *)p_input_ptrs[3];
		position_input = (const float *)p_input_ptrs[4];
		pitch_rand_input = (const float *)p_input_ptrs[5];
		scan_speed_input = (const float *)p_input_ptrs[6];
		amp_rand_input = (const float *)p_input_ptrs[7];
		pitch_tracking_input = (const float *)p_input_ptrs[8];
		audio_out = (float *)p_output_ptrs[0];
	}

	virtual void cleanup() override {
		// Release shared PCM cache reference (if using shared mode).
		if (using_shared_pcm && shared_pcm_key != StringName()) {
			SharedPCMCache *cache = SharedPCMCache::get_singleton();
			if (cache) {
				cache->release(shared_pcm_key);
			}
			using_shared_pcm = false;
			capture_buffer = nullptr; // No longer valid after release
		}
	}

	virtual void execute(int32_t p_num_frames) override {
		SYMPHONY_ASSUME_FRAMES(p_num_frames);

		// Read parameters (control-rate: once per micro-block)
		float grain_size_ms = grain_size_input ? *grain_size_input : default_grain_size_ms;
		float density = density_input ? *density_input : default_density;
		float position = position_input ? *position_input : default_position;
		float pitch_rand = pitch_rand_input ? *pitch_rand_input : default_pitch_randomness;
		float scan_speed = scan_speed_input ? *scan_speed_input : default_scan_speed;
		float amp_rand = amp_rand_input ? *amp_rand_input : default_amp_randomness;
		float pitch_tracking = pitch_tracking_input ? *pitch_tracking_input : default_pitch_tracking;

		grain_size_ms = CLAMP(grain_size_ms, 10.0f, 200.0f);
		density = CLAMP(density, 0.0f, 50.0f);
		position = CLAMP(position, 0.0f, 1.0f);
		pitch_rand = CLAMP(pitch_rand, 0.0f, 0.5f);
		scan_speed = CLAMP(scan_speed, 0.0f, 4.0f);
		amp_rand = CLAMP(amp_rand, 0.0f, 6.0f);
		pitch_tracking = CLAMP(pitch_tracking, 0.0f, 1.0f);

		int32_t grain_length = (int32_t)(grain_size_ms * mix_rate * 0.001f);
		if (grain_length < 1) {
			grain_length = 1;
		}

		// Average interval between grains (in samples)
		float avg_interval = (density > 0.001f) ? (mix_rate / density) : 1000000.0f;

		// S4.3: Calculate scan increment per sample
		int32_t effective_size = source_pcm_loaded ? source_pcm_length : capture_size;
		float scan_increment = scan_speed / (float)effective_size;

		// --- Block-splitting for trigger-driven grain spawning ---
		// Process audio in segments split at trigger boundaries for sample-accurate grain spawn.
		int32_t current_frame = 0;
		int32_t trigger_idx = 0;

		while (current_frame < p_num_frames) {
			// Determine next segment boundary: either the next trigger or end of block.
			int32_t segment_end = p_num_frames;
			bool trigger_at_boundary = false;
			float trigger_value = 1.0f;

			if (trigger_in && trigger_idx < trigger_in->count) {
				int32_t trig_offset = trigger_in->events[trigger_idx].sample_offset;
				if (trig_offset <= current_frame) {
					// Trigger at or before current position — spawn immediately, advance index.
					trigger_value = trigger_in->events[trigger_idx].value;
					trigger_idx++;
					// Compute effective position at this sample
					float eff_pos = position + auto_position;
					if (eff_pos >= 1.0f) eff_pos -= 1.0f;
					spawn_grain(grain_length, eff_pos, pitch_rand, amp_rand, pitch_tracking);
					// Don't change segment_end — just continue processing from current_frame.
					continue;
				} else {
					segment_end = trig_offset;
					trigger_at_boundary = true;
					trigger_value = trigger_in->events[trigger_idx].value;
				}
			}

			// --- Process audio samples in [current_frame, segment_end) ---
			for (int32_t i = current_frame; i < segment_end; i++) {
				// Write input to capture buffer (only in live mode)
				if (!source_pcm_loaded) {
					if (audio_in) {
						capture_buffer[capture_write] = audio_in[i];
					} else {
						capture_buffer[capture_write] = 0.0f;
					}
					capture_write = (capture_write + 1) % capture_size;
				}

				// S4.3: Auto-advance position
				if (scan_speed > 0.001f) {
					auto_position += scan_increment;
					if (auto_position >= 1.0f) {
						auto_position -= 1.0f;
					}
				}

				// Effective position
				float effective_position = position + auto_position;
				if (effective_position >= 1.0f) {
					effective_position -= 1.0f;
				}

				// S4.3: Periodic pitch detection (YIN)
				if (pitch_tracking > 0.001f) {
					pitch_update_counter++;
					if (pitch_update_counter >= PITCH_UPDATE_INTERVAL) {
						pitch_update_counter = 0;
						int32_t analyze_pos = source_pcm_loaded
							? (int32_t)(effective_position * (float)source_pcm_length)
							: (capture_write - PITCH_WINDOW_SIZE + capture_size) % capture_size;
						detected_pitch_hz = detect_pitch(analyze_pos, PITCH_WINDOW_SIZE);
					}
				}

				// --- Internal Poisson scheduling (disabled when trigger_only=true) ---
				if (!trigger_only) {
					schedule_counter -= 1.0f;
					if (schedule_counter <= 0.0f) {
						spawn_grain(grain_length, effective_position, pitch_rand, amp_rand, pitch_tracking);
						float u = random_float();
						if (u < 0.0001f) u = 0.0001f;
						schedule_counter = -Math::log(u) * avg_interval;
					}
				}

				// --- Grain processing: overlap-add ---
				float output = 0.0f;

				for (int32_t g = 0; g < MAX_GRAINS; g++) {
					Grain &grain = grains[g];
					if (!grain.active) {
						continue;
					}

					float phase = (float)grain.current_sample / (float)grain.length_samples;
					float window = hanning_window(phase);
					float read_sample_pos = (float)grain.start_sample + grain.read_pos;
					float sample = read_capture(read_sample_pos);
					output += sample * window * grain.amplitude;

					grain.read_pos += grain.playback_rate;
					grain.current_sample++;

					if (grain.current_sample >= grain.length_samples) {
						grain.active = false;
					}
				}

				audio_out[i] = output;
			}

			// If a trigger is at the segment boundary, spawn grain at that exact sample.
			if (trigger_at_boundary) {
				float eff_pos = position + auto_position;
				if (eff_pos >= 1.0f) eff_pos -= 1.0f;
				spawn_grain(grain_length, eff_pos, pitch_rand, amp_rand, pitch_tracking);
				trigger_idx++;
			}

			current_frame = segment_end;
		}
	}

	void spawn_grain(int32_t length_samples, float position, float pitch_rand, float amp_rand, float pitch_tracking) {
		// Find a free grain slot
		int32_t slot = -1;
		for (int32_t g = 0; g < MAX_GRAINS; g++) {
			if (!grains[g].active) {
				slot = g;
				break;
			}
		}
		if (slot < 0) {
			return; // All slots busy
		}

		Grain &grain = grains[slot];
		grain.active = true;
		grain.length_samples = length_samples;
		grain.current_sample = 0;
		grain.read_pos = 0.0f;

		// Position in capture buffer (with configurable randomization)
		float jitter = (random_float() - 0.5f) * 2.0f * default_position_randomness;
		float final_pos = CLAMP(position + jitter, 0.0f, 1.0f);

		if (source_pcm_loaded) {
			// Source buffer mode: position maps directly to PCM data [0, source_pcm_length)
			grain.start_sample = (int32_t)(final_pos * (float)(source_pcm_length - length_samples));
			grain.start_sample = CLAMP(grain.start_sample, 0, source_pcm_length - 1);
		} else {
			// Live mode: position maps to offset behind write head
			int32_t offset_behind = (int32_t)((1.0f - final_pos) * (float)(capture_size - length_samples));
			grain.start_sample = (capture_write - offset_behind + capture_size) % capture_size;
		}

		// Pitch randomization: semitone deviation
		float pitch_deviation = (random_float() - 0.5f) * 2.0f * pitch_rand;
		grain.playback_rate = Math::pow(2.0f, pitch_deviation / 12.0f);

		// S4.3: Pitch tracking — adjust grain rate to follow detected pitch
		if (pitch_tracking > 0.001f && detected_pitch_hz > 50.0f) {
			float pitch_ratio = detected_pitch_hz / PITCH_REFERENCE_HZ;
			// Blend between original rate and pitch-following rate
			grain.playback_rate = grain.playback_rate * (1.0f - pitch_tracking) +
								  grain.playback_rate * pitch_ratio * pitch_tracking;
		}

		// S4.3: Amplitude randomization (± dB)
		if (amp_rand > 0.001f) {
			float amp_deviation_db = (random_float() - 0.5f) * 2.0f * amp_rand;
			grain.amplitude = Math::pow(10.0f, amp_deviation_db / 20.0f);
		} else {
			grain.amplitude = 1.0f;
		}
	}

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		size_t needed = sizeof(int32_t) + sizeof(float) * 2 + sizeof(uint32_t) + sizeof(Grain) * MAX_GRAINS
			+ sizeof(float) + sizeof(int32_t); // detected_pitch_hz + pitch_update_counter
		if (!p_buffer) {
			return needed;
		}
		if (p_max_size < needed) {
			return 0;
		}

		size_t offset = 0;
		memcpy(p_buffer + offset, &capture_write, sizeof(int32_t));
		offset += sizeof(int32_t);
		memcpy(p_buffer + offset, &schedule_counter, sizeof(float));
		offset += sizeof(float);
		memcpy(p_buffer + offset, &auto_position, sizeof(float));
		offset += sizeof(float);
		memcpy(p_buffer + offset, &rng_state, sizeof(uint32_t));
		offset += sizeof(uint32_t);
		memcpy(p_buffer + offset, grains, sizeof(Grain) * MAX_GRAINS);
		offset += sizeof(Grain) * MAX_GRAINS;
		memcpy(p_buffer + offset, &detected_pitch_hz, sizeof(float));
		offset += sizeof(float);
		memcpy(p_buffer + offset, &pitch_update_counter, sizeof(int32_t));
		offset += sizeof(int32_t);
		return offset;
	}

	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		size_t needed = sizeof(int32_t) + sizeof(float) * 2 + sizeof(uint32_t) + sizeof(Grain) * MAX_GRAINS
			+ sizeof(float) + sizeof(int32_t); // detected_pitch_hz + pitch_update_counter
		if (p_size < needed) {
			return;
		}

		size_t offset = 0;
		memcpy(&capture_write, p_buffer + offset, sizeof(int32_t));
		offset += sizeof(int32_t);
		memcpy(&schedule_counter, p_buffer + offset, sizeof(float));
		offset += sizeof(float);
		memcpy(&auto_position, p_buffer + offset, sizeof(float));
		offset += sizeof(float);
		memcpy(&rng_state, p_buffer + offset, sizeof(uint32_t));
		offset += sizeof(uint32_t);
		memcpy(grains, p_buffer + offset, sizeof(Grain) * MAX_GRAINS);
		offset += sizeof(Grain) * MAX_GRAINS;
		memcpy(&detected_pitch_hz, p_buffer + offset, sizeof(float));
		offset += sizeof(float);
		memcpy(&pitch_update_counter, p_buffer + offset, sizeof(int32_t));
		offset += sizeof(int32_t);
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "GrainCloud";
		desc.category = "Synthesis";
		desc.inputs.push_back({ "audio_in", SymphonyPinType::AUDIO, false });
		desc.inputs.push_back({ "trigger", SymphonyPinType::TRIGGER, false });
		desc.inputs.push_back({ "grain_size_ms", SymphonyPinType::FLOAT, false });
		desc.inputs.push_back({ "density", SymphonyPinType::FLOAT, false });
		desc.inputs.push_back({ "position", SymphonyPinType::FLOAT, false });
		desc.inputs.push_back({ "pitch_randomness", SymphonyPinType::FLOAT, false });
		desc.inputs.push_back({ "scan_speed", SymphonyPinType::FLOAT, false });
		desc.inputs.push_back({ "amp_randomness", SymphonyPinType::FLOAT, false });
		desc.inputs.push_back({ "pitch_tracking", SymphonyPinType::FLOAT, false });
		desc.outputs.push_back({ "audio_out", SymphonyPinType::AUDIO, false });
		desc.params.push_back({ "grain_size_ms", 50.0f, 10.0f, 200.0f, 1.0f });
		desc.params.push_back({ "density", 8.0f, 0.0f, 50.0f, 0.1f });
		desc.params.push_back({ "position", 0.5f, 0.0f, 1.0f, 0.01f });
		desc.params.push_back({ "pitch_randomness", 0.0f, 0.0f, 0.5f, 0.01f });
		desc.params.push_back({ "scan_speed", 0.0f, 0.0f, 4.0f, 0.01f });
		desc.params.push_back({ "amp_randomness", 0.0f, 0.0f, 6.0f, 0.1f });
		desc.params.push_back({ "position_randomness", 0.1f, 0.0f, 1.0f, 0.01f });
		desc.params.push_back({ "pitch_tracking", 0.0f, 0.0f, 1.0f, 0.01f });
		desc.params.push_back({ "trigger_only", 0.0f, 0.0f, 1.0f, 1.0f });
		desc.params.push_back({ "seed", 1.0f, 1.0f, 999999.0f, 1.0f });
		desc.params.push_back({ "capture_seconds", 2.0f, 1.0f, 10.0f, 1.0f });
		desc.state_size = sizeof(SymphonyGrainCloud);
		desc.state_align = alignof(SymphonyGrainCloud);
		desc.extra_arena_bytes = 0; // Superseded by extra_arena_bytes_fn below.
		desc.extra_arena_bytes_fn = &SymphonyGrainCloud::calculate_arena_bytes;
		// Base covers capture + moderate grain load; density/pitch extras via extra_cost_fn.
		desc.cost_per_sample = 24.0f;
		desc.extra_cost_fn = &SymphonyGrainCloud::extra_cost;
		desc.create_fn = &SymphonyGrainCloud::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	[[nodiscard]] static float extra_cost(const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		(void)p_mix_rate;
		float density = p_params.has("density") ? (float)p_params["density"] : 8.0f;
		density = CLAMP(density, 0.0f, 50.0f);
		float grain_ms = p_params.has("grain_size_ms") ? (float)p_params["grain_size_ms"] : 50.0f;
		grain_ms = CLAMP(grain_ms, 10.0f, 200.0f);
		float pitch_tracking = p_params.has("pitch_tracking") ? (float)p_params["pitch_tracking"] : 0.0f;
		pitch_tracking = CLAMP(pitch_tracking, 0.0f, 1.0f);

		// Expected concurrent grains ≈ density × duration, capped at MAX_GRAINS.
		const float concurrent = MIN((float)MAX_GRAINS, density * grain_ms * 0.001f);
		// cost_per_sample=24 already budgets ~4 overlapping grains; charge surplus loops.
		const float surplus = MAX(0.0f, concurrent - 4.0f);
		float cost = surplus * (float)SYMPHONY_MICRO_BLOCK_SIZE * 3.0f;
		if (pitch_tracking > 0.001f) {
			cost += (float)SYMPHONY_MICRO_BLOCK_SIZE * 6.0f; // periodic YIN
		}
		return cost;
	}

	// Per-instance arena sizing: returns only what this specific instance needs.
	// - Shared PCM mode (source_pcm_cache_key present): only the hanning table.
	// - Live/copy mode: full capture buffer + hanning table.
	static size_t calculate_arena_bytes(const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		static constexpr size_t HANNING_BYTES = sizeof(float) * 1024;
		size_t offset = 0;

		bool shared_only = false;
		if (p_params.has("source_pcm_cache_key") && p_params.has("source_pcm_ptr") && p_params.has("source_pcm_length")) {
			String key = p_params.has("source_pcm_cache_key") ? String(p_params["source_pcm_cache_key"]) : "";
			shared_only = !key.is_empty();
		}

		if (!shared_only) {
			float capture_sec = p_params.has("capture_seconds") ? (float)p_params["capture_seconds"] : 2.0f;
			capture_sec = CLAMP(capture_sec, 1.0f, 10.0f);
			float rate = p_mix_rate > 1.0f ? p_mix_rate : 48000.0f;
			int32_t capture_samples = (int32_t)(capture_sec * rate);
			if (!SymphonyCheckedMath::bump(offset, sizeof(float) * (size_t)capture_samples, 32)) {
				return std::numeric_limits<size_t>::max();
			}
		}
		if (!SymphonyCheckedMath::bump(offset, HANNING_BYTES, 32)) {
			return std::numeric_limits<size_t>::max();
		}
		return offset;
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		void *mem = p_arena.alloc(sizeof(SymphonyGrainCloud), alignof(SymphonyGrainCloud));
		if (!mem) {
			return nullptr;
		}
		SymphonyGrainCloud *gc = new (mem) SymphonyGrainCloud();

		gc->mix_rate = p_mix_rate;
		gc->default_grain_size_ms = p_params.has("grain_size_ms") ? (float)p_params["grain_size_ms"] : 50.0f;
		gc->default_density = p_params.has("density") ? (float)p_params["density"] : 8.0f;
		gc->default_position = p_params.has("position") ? (float)p_params["position"] : 0.5f;
		gc->default_pitch_randomness = p_params.has("pitch_randomness") ? (float)p_params["pitch_randomness"] : 0.0f;
		gc->default_scan_speed = p_params.has("scan_speed") ? (float)p_params["scan_speed"] : 0.0f;
		gc->default_amp_randomness = p_params.has("amp_randomness") ? (float)p_params["amp_randomness"] : 0.0f;
		gc->default_position_randomness = p_params.has("position_randomness") ? (float)p_params["position_randomness"] : 0.1f;
		gc->default_pitch_tracking = p_params.has("pitch_tracking") ? (float)p_params["pitch_tracking"] : 0.0f;
		gc->trigger_only = p_params.has("trigger_only") ? ((float)p_params["trigger_only"] >= 0.5f) : false;

		// RNG seed (different per voice for variety)
		gc->rng_state = p_params.has("seed") ? (uint32_t)(float)p_params["seed"] : 1;
		if (gc->rng_state == 0) {
			gc->rng_state = 1; // xorshift cannot have 0 state
		}

		// Capture buffer sizing
		float capture_sec = p_params.has("capture_seconds") ? (float)p_params["capture_seconds"] : 2.0f;
		capture_sec = CLAMP(capture_sec, 1.0f, 10.0f);

		// A2: On web targets, clamp to 4s max to limit memory pressure.
		if (OS::get_singleton()->has_feature("web")) {
			capture_sec = MIN(capture_sec, 4.0f);
		}

		gc->capture_size = (int32_t)(capture_sec * p_mix_rate);
		gc->capture_write = 0;

		// --- Determine allocation strategy ---
		// Try shared PCM path first. If successful, we skip the arena capture buffer entirely.
		bool shared_pcm_acquired = false;

		if (p_params.has("source_pcm_ptr") && p_params.has("source_pcm_length")) {
			int64_t ptr_val = (int64_t)p_params["source_pcm_ptr"];
			const float *source_data = reinterpret_cast<const float *>(ptr_val);
			int32_t pcm_len = (int32_t)(float)p_params["source_pcm_length"];

			if (source_data && pcm_len > 0) {
				int32_t use_len = (pcm_len < gc->capture_size) ? pcm_len : gc->capture_size;

				// Phase B2: SharedPCMCache path — zero arena cost for the capture buffer.
				if (p_params.has("source_pcm_cache_key")) {
					StringName cache_key = StringName(String(p_params["source_pcm_cache_key"]));
					SharedPCMCache *pcm_cache = SharedPCMCache::get_singleton();

					if (pcm_cache && cache_key != StringName()) {
						const float *shared_buf = pcm_cache->acquire(cache_key, source_data, use_len);
						if (shared_buf) {
							gc->capture_buffer = const_cast<float *>(shared_buf);
							gc->using_shared_pcm = true;
							gc->shared_pcm_key = cache_key;
							gc->source_pcm_loaded = true;
							gc->source_pcm_length = use_len;
							shared_pcm_acquired = true;
						}
					}
				}

				// Fallback: shared cache not available/failed — allocate arena buffer and copy.
				if (!shared_pcm_acquired) {
					gc->capture_buffer = (float *)p_arena.alloc(sizeof(float) * gc->capture_size, 32);
					if (!gc->capture_buffer) {
						return nullptr;
					}
					memset(gc->capture_buffer, 0, sizeof(float) * gc->capture_size);
					memcpy(gc->capture_buffer, source_data, sizeof(float) * use_len);
					gc->source_pcm_loaded = true;
					gc->source_pcm_length = use_len;
				}
			}
		}

		// Live-input mode: no source PCM at all — allocate arena capture buffer for live writing.
		if (!gc->source_pcm_loaded && !shared_pcm_acquired) {
			gc->capture_buffer = (float *)p_arena.alloc(sizeof(float) * gc->capture_size, 32);
			if (!gc->capture_buffer) {
				return nullptr;
			}
			memset(gc->capture_buffer, 0, sizeof(float) * gc->capture_size);
		}

		// Hanning window lookup table (1024 entries)
		gc->hanning_size = 1024;
		gc->hanning_table = (float *)p_arena.alloc(sizeof(float) * gc->hanning_size, 32);
		if (!gc->hanning_table) {
			return nullptr;
		}
		for (int32_t i = 0; i < gc->hanning_size; i++) {
			float phase = (float)i / (float)(gc->hanning_size - 1);
			gc->hanning_table[i] = 0.5f * (1.0f - Math::cos(Math::TAU * phase));
		}

		// Initialize grains as inactive
		for (int32_t g = 0; g < MAX_GRAINS; g++) {
			gc->grains[g].active = false;
		}

		// Auto-scan position starts at 0
		gc->auto_position = 0.0f;

		// Pitch detection state initialized
		gc->detected_pitch_hz = 0.0f;
		gc->pitch_update_counter = 0;

		// Schedule first grain quickly
		gc->schedule_counter = gc->random_float() * (p_mix_rate / gc->default_density);

		// Safety: verify we haven't overrun the arena budget.
		DEV_ASSERT(p_arena.get_remaining() < p_arena.capacity); // Underflow check — remaining must be < capacity (i.e., non-wrapped).

		return gc;
	}
};
