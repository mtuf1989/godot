#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "../../core/symphony_checked_math.h"
#include "core/math/math_funcs.h"

#include <limits>

// Feedback Delay Network (FDN) reverb operator.
// Produces a wet reverb signal from an audio input without needing an AudioEffect bus.
// Configurable as 4-line or 8-line FDN via compile-time parameter "num_lines".
//
// Architecture:
//   audio_in → [pre_delay] → FDN core → audio_out (wet only)
//
// FDN core (per sample):
//   1. Read from each delay line at its tap point
//   2. Apply Hadamard mixing matrix to the read values
//   3. Apply per-line one-pole LP damping filter
//   4. Multiply by decay gain (derived from delay length and decay_time)
//   5. Add input signal to each line's feedback
//   6. Write to each delay line
//
// The Hadamard matrix ensures energy is distributed across all delay lines,
// producing dense, colorless reflections.
//
// Delay line lengths are derived from room_size using mutually prime ratios
// to avoid metallic coloration.
//
// Arena allocation: all delay line buffers + pre-delay buffer allocated at compile time.
// Max room_size determines buffer sizes.
//
class SymphonyFDNReverb : public SymphonyOperator {
private:
	static constexpr int MAX_LINES = 8;
	// Prime-ratio delay multipliers for colorless reflection density
	// These produce mutually incommensurate delay lengths from a single room_size param
	static constexpr float DELAY_RATIOS_4[4] = { 1.0f, 1.2599f, 1.4983f, 1.8409f };
	static constexpr float DELAY_RATIOS_8[8] = { 1.0f, 1.1225f, 1.2599f, 1.4142f, 1.4983f, 1.6818f, 1.8409f, 1.9953f };

	const float *SYMPHONY_RESTRICT audio_in = nullptr;
	const float *SYMPHONY_RESTRICT room_size_input = nullptr;
	const float *SYMPHONY_RESTRICT decay_time_input = nullptr;
	const float *SYMPHONY_RESTRICT damping_input = nullptr;
	const float *SYMPHONY_RESTRICT pre_delay_input = nullptr;
	float *SYMPHONY_RESTRICT audio_out = nullptr;

	// Delay line state
	float *delay_buffers[MAX_LINES] = {};
	int delay_lengths[MAX_LINES] = {};
	int write_positions[MAX_LINES] = {};
	int max_delay_samples = 0;

	// One-pole LP damping filters (per line)
	float damping_state[MAX_LINES] = {};

	// Pre-delay
	float *pre_delay_buffer = nullptr;
	int pre_delay_write_pos = 0;
	int max_pre_delay_samples = 0;

	// Configuration
	int num_lines = 4;
	float mix_rate = 48000.0f;
	float default_room_size = 0.5f;     // 0-1 normalized
	float default_decay_time = 2.0f;    // seconds
	float default_damping = 0.5f;       // 0-1 (0=no damping, 1=maximum damping)
	float default_pre_delay_ms = 20.0f; // milliseconds

public:
	SymphonyFDNReverb(float p_mix_rate, int p_num_lines, int p_max_delay_samples, int p_max_pre_delay_samples,
			float *p_delay_memory, float *p_pre_delay_memory,
			float p_room_size, float p_decay_time, float p_damping, float p_pre_delay_ms)
			: max_delay_samples(p_max_delay_samples),
			  pre_delay_buffer(p_pre_delay_memory),
			  max_pre_delay_samples(p_max_pre_delay_samples),
			  num_lines(p_num_lines),
			  mix_rate(p_mix_rate),
			  default_room_size(p_room_size),
			  default_decay_time(p_decay_time),
			  default_damping(p_damping),
			  default_pre_delay_ms(p_pre_delay_ms) {
		// Partition the contiguous delay memory block into per-line buffers
		for (int l = 0; l < num_lines; l++) {
			delay_buffers[l] = p_delay_memory + (l * p_max_delay_samples);
			write_positions[l] = 0;
			damping_state[l] = 0.0f;
		}
		// Zero all buffers
		memset(p_delay_memory, 0, sizeof(float) * num_lines * p_max_delay_samples);
		if (pre_delay_buffer) {
			memset(pre_delay_buffer, 0, sizeof(float) * p_max_pre_delay_samples);
		}
		// Compute initial delay lengths from room_size
		_update_delay_lengths(p_room_size);
	}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		audio_in = (const float *)p_input_ptrs[0];
		room_size_input = (const float *)p_input_ptrs[1];
		decay_time_input = (const float *)p_input_ptrs[2];
		damping_input = (const float *)p_input_ptrs[3];
		pre_delay_input = (const float *)p_input_ptrs[4];
		audio_out = (float *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		SYMPHONY_ASSUME_FRAMES(p_num_frames);

		float room_size = room_size_input ? *room_size_input : default_room_size;
		float decay_time = decay_time_input ? *decay_time_input : default_decay_time;
		float damping = damping_input ? *damping_input : default_damping;
		float pre_delay_ms = pre_delay_input ? *pre_delay_input : default_pre_delay_ms;

		// Clamp parameters
		room_size = CLAMP(room_size, 0.01f, 1.0f);
		decay_time = CLAMP(decay_time, 0.1f, 20.0f);
		damping = CLAMP(damping, 0.0f, 1.0f);
		pre_delay_ms = CLAMP(pre_delay_ms, 0.0f, 200.0f);

		// Update delay lengths based on room_size
		_update_delay_lengths(room_size);

		// Pre-delay in samples
		int pre_delay_samples = (int)(pre_delay_ms * mix_rate / 1000.0f);
		pre_delay_samples = MIN(pre_delay_samples, max_pre_delay_samples - 1);

		// Damping coefficient for one-pole LP (higher damping = more low-pass)
		float damp_coeff = damping * 0.7f; // Scale to useful range

		// Compute per-line decay gains
		// gain_per_line = 10^(-3 * delay_length_sec / decay_time)
		// This ensures RT60 = decay_time regardless of individual delay lengths
		float line_gains[MAX_LINES];
		for (int l = 0; l < num_lines; l++) {
			float delay_sec = (float)delay_lengths[l] / mix_rate;
			line_gains[l] = Math::pow(10.0f, -3.0f * delay_sec / decay_time);
		}

		// Hadamard normalization factor
		float hadamard_scale = 1.0f / Math::sqrt((float)num_lines);

		for (int32_t i = 0; i < p_num_frames; i++) {
			// Get input (may be null if unconnected)
			float input_sample = audio_in ? audio_in[i] : 0.0f;

			// Apply pre-delay
			float delayed_input;
			if (pre_delay_samples > 0 && pre_delay_buffer) {
				pre_delay_buffer[pre_delay_write_pos] = input_sample;
				int read_pos = pre_delay_write_pos - pre_delay_samples;
				if (read_pos < 0) {
					read_pos += max_pre_delay_samples;
				}
				delayed_input = pre_delay_buffer[read_pos];
				pre_delay_write_pos = (pre_delay_write_pos + 1) % max_pre_delay_samples;
			} else {
				delayed_input = input_sample;
			}

			// Read from all delay lines
			float tap_values[MAX_LINES];
			for (int l = 0; l < num_lines; l++) {
				int read_pos = write_positions[l] - delay_lengths[l];
				if (read_pos < 0) {
					read_pos += max_delay_samples;
				}
				tap_values[l] = delay_buffers[l][read_pos];
			}

			// Apply Hadamard mixing matrix
			float mixed[MAX_LINES];
			_hadamard_mix(tap_values, mixed);

			// Scale by Hadamard normalization
			for (int l = 0; l < num_lines; l++) {
				mixed[l] *= hadamard_scale;
			}

			// Apply damping (one-pole LP) and decay gain, then write back
			float output_sum = 0.0f;
			for (int l = 0; l < num_lines; l++) {
				// One-pole LP: y[n] = (1 - coeff) * x[n] + coeff * y[n-1]
				damping_state[l] = (1.0f - damp_coeff) * mixed[l] + damp_coeff * damping_state[l];

				// Apply RT60-matched decay gain
				float feedback = damping_state[l] * line_gains[l];

				// Write input + feedback to delay line
				delay_buffers[l][write_positions[l]] = delayed_input + feedback;
				write_positions[l] = (write_positions[l] + 1) % max_delay_samples;

				// Sum taps for output (pre-mixing, for more direct character)
				output_sum += tap_values[l];
			}

			// Output: sum of all taps, normalized
			audio_out[i] = output_sum * hadamard_scale;
		}
	}

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		// State: damping_state + write_positions + pre_delay_write_pos + carrier_phase placeholder
		size_t needed = sizeof(float) * MAX_LINES + sizeof(int) * MAX_LINES + sizeof(int);
		if (!p_buffer) {
			return needed;
		}
		if (p_max_size < needed) {
			return 0;
		}
		size_t offset = 0;
		memcpy(p_buffer + offset, damping_state, sizeof(float) * MAX_LINES);
		offset += sizeof(float) * MAX_LINES;
		memcpy(p_buffer + offset, write_positions, sizeof(int) * MAX_LINES);
		offset += sizeof(int) * MAX_LINES;
		memcpy(p_buffer + offset, &pre_delay_write_pos, sizeof(int));
		offset += sizeof(int);
		return offset;
	}

	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		size_t needed = sizeof(float) * MAX_LINES + sizeof(int) * MAX_LINES + sizeof(int);
		if (p_size >= needed) {
			size_t offset = 0;
			memcpy(damping_state, p_buffer + offset, sizeof(float) * MAX_LINES);
			offset += sizeof(float) * MAX_LINES;
			memcpy(write_positions, p_buffer + offset, sizeof(int) * MAX_LINES);
			offset += sizeof(int) * MAX_LINES;
			memcpy(&pre_delay_write_pos, p_buffer + offset, sizeof(int));
		}
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "FDNReverb";
		desc.category = "Delay";
		desc.inputs.push_back({ "audio_in", SymphonyPinType::AUDIO, false });
		desc.inputs.push_back({ "room_size", SymphonyPinType::FLOAT, false });
		desc.inputs.push_back({ "decay_time", SymphonyPinType::FLOAT, false });
		desc.inputs.push_back({ "damping", SymphonyPinType::FLOAT, false });
		desc.inputs.push_back({ "pre_delay_ms", SymphonyPinType::FLOAT, false });
		desc.outputs.push_back({ "audio_out", SymphonyPinType::AUDIO, false });
		desc.params.push_back({ "num_lines", 4.0f, 4.0f, 8.0f, 4.0f }); // 4 or 8
		desc.params.push_back({ "max_delay_ms", 100.0f, 10.0f, 200.0f, 1.0f });
		desc.params.push_back({ "room_size", 0.5f, 0.01f, 1.0f, 0.01f });
		desc.params.push_back({ "decay_time", 2.0f, 0.1f, 20.0f, 0.1f });
		desc.params.push_back({ "damping", 0.5f, 0.0f, 1.0f, 0.01f });
		desc.params.push_back({ "pre_delay_ms", 20.0f, 0.0f, 200.0f, 1.0f });
		// State size is the operator struct only — delay buffers are arena-allocated separately
		desc.state_size = sizeof(SymphonyFDNReverb);
		desc.state_align = alignof(SymphonyFDNReverb);
		desc.extra_arena_bytes = 0;
		desc.extra_arena_bytes_fn = &SymphonyFDNReverb::calculate_arena_bytes;
		desc.create_fn = &SymphonyFDNReverb::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	struct Config {
		int num_lines = 4;
		float max_delay_ms = 100.0f;
		float room_size = 0.5f;
		float decay_time = 2.0f;
		float damping = 0.5f;
		float pre_delay_ms = 20.0f;
		int max_delay_samples = 0;
		int max_pre_delay_samples = 0;
	};

	[[nodiscard]] static Config resolve_config(const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		Config cfg;
		cfg.num_lines = p_params.has("num_lines") ? (int)(float)p_params["num_lines"] : 4;
		cfg.num_lines = (cfg.num_lines >= 8) ? 8 : 4;
		cfg.max_delay_ms = p_params.has("max_delay_ms") ? (float)p_params["max_delay_ms"] : 100.0f;
		cfg.max_delay_ms = CLAMP(cfg.max_delay_ms, 10.0f, 200.0f);
		cfg.room_size = p_params.has("room_size") ? (float)p_params["room_size"] : 0.5f;
		cfg.decay_time = p_params.has("decay_time") ? (float)p_params["decay_time"] : 2.0f;
		cfg.damping = p_params.has("damping") ? (float)p_params["damping"] : 0.5f;
		cfg.pre_delay_ms = p_params.has("pre_delay_ms") ? (float)p_params["pre_delay_ms"] : 20.0f;
		float rate = p_mix_rate > 1.0f ? p_mix_rate : 48000.0f;
		cfg.max_delay_samples = (int)(cfg.max_delay_ms * rate / 1000.0f) + 1;
		cfg.max_pre_delay_samples = (int)(200.0f * rate / 1000.0f) + 1;
		return cfg;
	}

	static size_t calculate_arena_bytes(const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		Config cfg = resolve_config(p_params, p_mix_rate);
		size_t offset = 0;
		if (!SymphonyCheckedMath::bump(offset, sizeof(float) * (size_t)cfg.num_lines * (size_t)cfg.max_delay_samples, 32)) {
			return std::numeric_limits<size_t>::max();
		}
		if (!SymphonyCheckedMath::bump(offset, sizeof(float) * (size_t)cfg.max_pre_delay_samples, 32)) {
			return std::numeric_limits<size_t>::max();
		}
		return offset;
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		Config cfg = resolve_config(p_params, p_mix_rate);

		void *mem = p_arena.alloc(sizeof(SymphonyFDNReverb), alignof(SymphonyFDNReverb));
		if (!mem) {
			return nullptr;
		}

		size_t delay_mem_size = sizeof(float) * cfg.num_lines * cfg.max_delay_samples;
		float *delay_memory = (float *)p_arena.alloc(delay_mem_size, 32);
		if (!delay_memory) {
			return nullptr;
		}

		size_t pre_delay_mem_size = sizeof(float) * cfg.max_pre_delay_samples;
		float *pre_delay_memory = (float *)p_arena.alloc(pre_delay_mem_size, 32);
		if (!pre_delay_memory) {
			return nullptr;
		}

		return new (mem) SymphonyFDNReverb(p_mix_rate, cfg.num_lines, cfg.max_delay_samples, cfg.max_pre_delay_samples,
				delay_memory, pre_delay_memory,
				cfg.room_size, cfg.decay_time, cfg.damping, cfg.pre_delay_ms);
	}

private:
	void _update_delay_lengths(float p_room_size) {
		// Base delay from room_size (normalized 0-1 maps to 10%-100% of max buffer)
		float base_delay_samples = p_room_size * (float)(max_delay_samples - 1) * 0.9f + (float)(max_delay_samples - 1) * 0.1f;

		const float *ratios = (num_lines == 8) ? DELAY_RATIOS_8 : DELAY_RATIOS_4;
		for (int l = 0; l < num_lines; l++) {
			delay_lengths[l] = (int)(base_delay_samples / ratios[l]);
			// Clamp to valid range
			delay_lengths[l] = CLAMP(delay_lengths[l], 1, max_delay_samples - 1);
		}
	}

	void _hadamard_mix(const float *p_input, float *p_output) {
		if (num_lines == 4) {
			// 4×4 Hadamard matrix (unnormalized — normalization applied after)
			// H4 = [[1,1,1,1],[1,-1,1,-1],[1,1,-1,-1],[1,-1,-1,1]]
			p_output[0] = p_input[0] + p_input[1] + p_input[2] + p_input[3];
			p_output[1] = p_input[0] - p_input[1] + p_input[2] - p_input[3];
			p_output[2] = p_input[0] + p_input[1] - p_input[2] - p_input[3];
			p_output[3] = p_input[0] - p_input[1] - p_input[2] + p_input[3];
		} else {
			// 8×8 Hadamard matrix via recursive construction: H8 = H4 ⊗ H2
			// H2 = [[1,1],[1,-1]]
			// First apply H4 to pairs, then H2 across pairs
			float temp_a[4], temp_b[4];
			for (int k = 0; k < 4; k++) {
				temp_a[k] = p_input[k] + p_input[k + 4];
				temp_b[k] = p_input[k] - p_input[k + 4];
			}
			// Apply H4 to each half
			p_output[0] = temp_a[0] + temp_a[1] + temp_a[2] + temp_a[3];
			p_output[1] = temp_a[0] - temp_a[1] + temp_a[2] - temp_a[3];
			p_output[2] = temp_a[0] + temp_a[1] - temp_a[2] - temp_a[3];
			p_output[3] = temp_a[0] - temp_a[1] - temp_a[2] + temp_a[3];
			p_output[4] = temp_b[0] + temp_b[1] + temp_b[2] + temp_b[3];
			p_output[5] = temp_b[0] - temp_b[1] + temp_b[2] - temp_b[3];
			p_output[6] = temp_b[0] + temp_b[1] - temp_b[2] - temp_b[3];
			p_output[7] = temp_b[0] - temp_b[1] - temp_b[2] + temp_b[3];
		}
	}
};

// Static constexpr member definitions (required for ODR-use in C++14/17)
constexpr float SymphonyFDNReverb::DELAY_RATIOS_4[4];
constexpr float SymphonyFDNReverb::DELAY_RATIOS_8[8];
