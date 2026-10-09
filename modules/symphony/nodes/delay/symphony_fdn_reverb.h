#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "../../core/symphony_checked_math.h"
#include "../../core/symphony_denormal_guard.h"
#include "../../dsp/symphony_fdn_core.h"
#include "core/math/math_funcs.h"

#include <cmath>
#include <cstring>
#include <limits>

// Feedback Delay Network reverb — wraps SymphonyFDNCore.
// Pins/params unchanged: room_size → mean delay, damping → hf_ratio = 1−0.9·damping,
// num_lines → minimum line count (may promote to 8 for mode density).
class SymphonyFDNReverb : public SymphonyOperator {
private:
	const float *SYMPHONY_RESTRICT audio_in = nullptr;
	const float *SYMPHONY_RESTRICT room_size_input = nullptr;
	const float *SYMPHONY_RESTRICT decay_time_input = nullptr;
	const float *SYMPHONY_RESTRICT damping_input = nullptr;
	const float *SYMPHONY_RESTRICT pre_delay_input = nullptr;
	float *SYMPHONY_RESTRICT audio_out = nullptr;

	SymphonyFDNCore::State core;
	SymphonyFDNCore::Design design;

	float *pre_delay_buffer = nullptr;
	int pre_delay_write_pos = 0;
	int max_pre_delay_samples = 0;
	float pre_delay_f = 0.0f;
	float pre_delay_target = 0.0f;
	float pre_smooth = 1.0f;

	int min_lines = 4;
	int max_delay_samples = 0;
	float mix_rate = 48000.0f;
	float max_delay_ms = 100.0f;
	float default_room_size = 0.5f;
	float default_decay_time = 2.0f;
	float default_damping = 0.5f;
	float default_pre_delay_ms = 20.0f;

	float cached_room_size = -1.0f;
	float cached_decay_time = -1.0f;
	float cached_damping = -1.0f;
	float cached_pre_delay_ms = -1.0f;

	uint8_t silent_blocks = 0;
	static constexpr float ACTIVITY_THRESHOLD = 1e-6f;

	void _refresh(float room_size, float decay_time, float damping, float pre_delay_ms) {
		bool dirty = (room_size != cached_room_size) || (decay_time != cached_decay_time) || (damping != cached_damping);
		if (dirty) {
			const float mean_delay_s = CLAMP(room_size, 0.01f, 1.0f) * (max_delay_ms * 0.001f) * 0.5f;
			const float hf_ratio = CLAMP(1.0f - 0.9f * damping, 0.05f, 1.0f);
			SymphonyFDNCore::Design d;
			if (SymphonyFDNCore::design(mix_rate, min_lines, mean_delay_s, decay_time, hf_ratio, max_delay_samples, d)) {
				design = d;
				SymphonyFDNCore::apply_design(core, design, false);
			}
			cached_room_size = room_size;
			cached_decay_time = decay_time;
			cached_damping = damping;
		}
		if (pre_delay_ms != cached_pre_delay_ms) {
			pre_delay_target = pre_delay_ms * mix_rate * 0.001f;
			pre_delay_target = CLAMP(pre_delay_target, 0.0f, (float)(max_pre_delay_samples - 2));
			cached_pre_delay_ms = pre_delay_ms;
		}
	}

	float _read_pre_delay() const {
		if (!pre_delay_buffer || pre_delay_f < 0.5f) {
			return 0.0f; // caller supplies undelayed when target ~0
		}
		float read_pos = (float)pre_delay_write_pos - pre_delay_f;
		while (read_pos < 0.0f) {
			read_pos += (float)max_pre_delay_samples;
		}
		const int idx = (int)read_pos;
		const float frac = read_pos - (float)idx;
		int next = idx + 1;
		if (next >= max_pre_delay_samples) {
			next = 0;
		}
		return pre_delay_buffer[idx] + frac * (pre_delay_buffer[next] - pre_delay_buffer[idx]);
	}

public:
	SymphonyFDNReverb(float p_mix_rate, int p_min_lines, int p_max_delay_samples, int p_max_pre_delay_samples,
			float p_max_delay_ms, float *p_delay_memory, float *p_pre_delay_memory,
			float p_room_size, float p_decay_time, float p_damping, float p_pre_delay_ms)
			: pre_delay_buffer(p_pre_delay_memory),
			  max_pre_delay_samples(p_max_pre_delay_samples),
			  min_lines(p_min_lines),
			  max_delay_samples(p_max_delay_samples),
			  mix_rate(p_mix_rate),
			  max_delay_ms(p_max_delay_ms),
			  default_room_size(p_room_size),
			  default_decay_time(p_decay_time),
			  default_damping(p_damping),
			  default_pre_delay_ms(p_pre_delay_ms) {
		// Bind with max lines; design may use 4 or 8.
		SymphonyFDNCore::bind_buffers(core, p_delay_memory, SymphonyFDNCore::MAX_LINES, p_max_delay_samples, p_mix_rate);
		if (pre_delay_buffer) {
			memset(pre_delay_buffer, 0, sizeof(float) * p_max_pre_delay_samples);
		}
		const float smooth_n = 0.020f * mix_rate;
		pre_smooth = (smooth_n > 1.0f) ? (1.0f - exp2f(-1.0f / smooth_n)) : 1.0f;
		_refresh(p_room_size, p_decay_time, p_damping, p_pre_delay_ms);
		SymphonyFDNCore::apply_design(core, design, true);
		pre_delay_f = pre_delay_target;
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

		room_size = CLAMP(room_size, 0.01f, 1.0f);
		decay_time = CLAMP(decay_time, 0.1f, 20.0f);
		damping = CLAMP(damping, 0.0f, 1.0f);
		pre_delay_ms = CLAMP(pre_delay_ms, 0.0f, 200.0f);
		_refresh(room_size, decay_time, damping, pre_delay_ms);

		float peak = 0.0f;
		float wet[SYMPHONY_MICRO_BLOCK_SIZE];
		float delayed[SYMPHONY_MICRO_BLOCK_SIZE];

		for (int32_t i = 0; i < p_num_frames; i++) {
			const float input_sample = audio_in ? audio_in[i] : 0.0f;
			// Always write pre-delay; smooth fractional read (P6).
			if (pre_delay_buffer) {
				pre_delay_buffer[pre_delay_write_pos] = input_sample;
				pre_delay_f += (pre_delay_target - pre_delay_f) * pre_smooth;
				if (pre_delay_f < 0.5f) {
					delayed[i] = input_sample;
				} else {
					delayed[i] = _read_pre_delay();
				}
				pre_delay_write_pos = (pre_delay_write_pos + 1) % max_pre_delay_samples;
			} else {
				delayed[i] = input_sample;
			}
		}

		SymphonyFDNCore::process(core, delayed, wet, nullptr, p_num_frames);

		for (int32_t i = 0; i < p_num_frames; i++) {
			audio_out[i] = wet[i];
			peak = MAX(peak, Math::abs(wet[i]));
		}

		if (peak < ACTIVITY_THRESHOLD) {
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
		size_t needed = sizeof(float) * SymphonyFDNCore::MAX_LINES + sizeof(int) * SymphonyFDNCore::MAX_LINES + sizeof(int);
		if (!p_buffer) {
			return needed;
		}
		if (p_max_size < needed) {
			return 0;
		}
		size_t offset = 0;
		memcpy(p_buffer + offset, core.damp_z, sizeof(float) * SymphonyFDNCore::MAX_LINES);
		offset += sizeof(float) * SymphonyFDNCore::MAX_LINES;
		memcpy(p_buffer + offset, core.write_pos, sizeof(int) * SymphonyFDNCore::MAX_LINES);
		offset += sizeof(int) * SymphonyFDNCore::MAX_LINES;
		memcpy(p_buffer + offset, &pre_delay_write_pos, sizeof(int));
		return offset + sizeof(int);
	}

	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		size_t needed = sizeof(float) * SymphonyFDNCore::MAX_LINES + sizeof(int) * SymphonyFDNCore::MAX_LINES + sizeof(int);
		if (p_size < needed) {
			return;
		}
		size_t offset = 0;
		memcpy(core.damp_z, p_buffer + offset, sizeof(float) * SymphonyFDNCore::MAX_LINES);
		offset += sizeof(float) * SymphonyFDNCore::MAX_LINES;
		memcpy(core.write_pos, p_buffer + offset, sizeof(int) * SymphonyFDNCore::MAX_LINES);
		offset += sizeof(int) * SymphonyFDNCore::MAX_LINES;
		memcpy(&pre_delay_write_pos, p_buffer + offset, sizeof(int));
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
		desc.params.push_back({ "num_lines", 4.0f, 4.0f, 8.0f, 4.0f });
		desc.params.push_back({ "max_delay_ms", 100.0f, 10.0f, 200.0f, 1.0f });
		desc.params.push_back({ "room_size", 0.5f, 0.01f, 1.0f, 0.01f });
		desc.params.push_back({ "decay_time", 2.0f, 0.1f, 20.0f, 0.1f });
		desc.params.push_back({ "damping", 0.5f, 0.0f, 1.0f, 0.01f });
		desc.params.push_back({ "pre_delay_ms", 20.0f, 0.0f, 200.0f, 1.0f });
		desc.state_size = sizeof(SymphonyFDNReverb);
		desc.state_align = alignof(SymphonyFDNReverb);
		desc.extra_arena_bytes = 0;
		desc.extra_arena_bytes_fn = &SymphonyFDNReverb::calculate_arena_bytes;
		desc.cost_per_sample = 16.0f;
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
		// Always reserve MAX_LINES — design may promote 4→8 for mode density.
		if (!SymphonyCheckedMath::bump(offset, sizeof(float) * (size_t)SymphonyFDNCore::MAX_LINES * (size_t)cfg.max_delay_samples, 32)) {
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
		float *delay_memory = (float *)p_arena.alloc(sizeof(float) * SymphonyFDNCore::MAX_LINES * cfg.max_delay_samples, 32);
		float *pre_delay_memory = (float *)p_arena.alloc(sizeof(float) * cfg.max_pre_delay_samples, 32);
		if (!delay_memory || !pre_delay_memory) {
			return nullptr;
		}
		return new (mem) SymphonyFDNReverb(p_mix_rate, cfg.num_lines, cfg.max_delay_samples, cfg.max_pre_delay_samples,
				cfg.max_delay_ms, delay_memory, pre_delay_memory,
				cfg.room_size, cfg.decay_time, cfg.damping, cfg.pre_delay_ms);
	}
};
