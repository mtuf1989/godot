#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "../../core/symphony_checked_math.h"
#include "../../dsp/symphony_shoebox_er.h"
#include "core/math/math_funcs.h"
#include "core/math/vector3.h"

#include <cstring>
#include <limits>

// SymphonyEarlyReflections — shoebox early reflections wrapping SymphonyShoeboxER.
class SymphonyEarlyReflections : public SymphonyOperator {
public:
	static constexpr int NUM_WALLS = 6;
	static constexpr float DEFAULT_SPEED_OF_SOUND = 343.0f;

	struct Tap {
		float delay_seconds = 0.0f;
		float gain = 0.0f;
	};

	static void compute_shoebox_reflections(const Vector3 &p_dimensions, const Vector3 &p_listener_off,
			const Vector3 &p_source_off, float p_reflection, float p_speed, Tap p_out[NUM_WALLS]) {
		SymphonyShoeboxER::Tap core[NUM_WALLS];
		SymphonyShoeboxER::compute_shoebox_reflections(
				p_dimensions.x, p_dimensions.y, p_dimensions.z,
				p_listener_off.x, p_listener_off.y, p_listener_off.z,
				p_source_off.x, p_source_off.y, p_source_off.z,
				p_reflection, p_speed, core);
		for (int i = 0; i < NUM_WALLS; i++) {
			p_out[i].delay_seconds = core[i].delay_seconds;
			p_out[i].gain = core[i].gain;
		}
	}

	static void compute_relative(const Vector3 &p_dimensions, float p_reflection, float p_speed,
			SymphonyShoeboxER::TapSet &r_out) {
		SymphonyShoeboxER::compute_relative(p_dimensions.x, p_dimensions.y, p_dimensions.z,
				p_reflection, p_speed, r_out);
	}

private:
	const float *SYMPHONY_RESTRICT audio_in = nullptr;
	const float *SYMPHONY_RESTRICT room_w_input = nullptr;
	const float *SYMPHONY_RESTRICT room_h_input = nullptr;
	const float *SYMPHONY_RESTRICT room_d_input = nullptr;
	const float *SYMPHONY_RESTRICT reflection_input = nullptr;
	float *SYMPHONY_RESTRICT audio_out = nullptr;

	SymphonyShoeboxER::Runtime runtime;

	float mix_rate = 48000.0f;
	float speed_of_sound = DEFAULT_SPEED_OF_SOUND;
	Vector3 default_dimensions = Vector3(8.0f, 3.0f, 6.0f);
	float default_reflection = 0.7f;

	float cached_w = -1.0f, cached_h = -1.0f, cached_d = -1.0f, cached_refl = -1.0f;

	uint8_t silent_blocks = 0;
	static constexpr float ACTIVITY_THRESHOLD = 1e-6f;

	void _refresh_taps(float p_w, float p_h, float p_d, float p_refl) {
		if (p_w == cached_w && p_h == cached_h && p_d == cached_d && p_refl == cached_refl) {
			return;
		}
		cached_w = p_w;
		cached_h = p_h;
		cached_d = p_d;
		cached_refl = p_refl;
		SymphonyShoeboxER::TapSet taps;
		SymphonyShoeboxER::compute_relative(p_w, p_h, p_d, p_refl, speed_of_sound, taps);
		SymphonyShoeboxER::set_taps(runtime, taps);
	}

public:
	SymphonyEarlyReflections(float p_mix_rate, int p_max_delay_samples, float *p_delay_memory,
			const Vector3 &p_dimensions, float p_reflection, float p_speed)
			: mix_rate(p_mix_rate),
			  speed_of_sound(p_speed),
			  default_dimensions(p_dimensions),
			  default_reflection(p_reflection) {
		SymphonyShoeboxER::bind_buffer(runtime, p_delay_memory, p_max_delay_samples, p_mix_rate);
		_refresh_taps(p_dimensions.x, p_dimensions.y, p_dimensions.z, p_reflection);
	}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		audio_in = (const float *)p_input_ptrs[0];
		room_w_input = (const float *)p_input_ptrs[1];
		room_h_input = (const float *)p_input_ptrs[2];
		room_d_input = (const float *)p_input_ptrs[3];
		reflection_input = (const float *)p_input_ptrs[4];
		audio_out = (float *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		SYMPHONY_ASSUME_FRAMES(p_num_frames);

		float w = room_w_input ? *room_w_input : default_dimensions.x;
		float h = room_h_input ? *room_h_input : default_dimensions.y;
		float d = room_d_input ? *room_d_input : default_dimensions.z;
		float refl = reflection_input ? *reflection_input : default_reflection;
		refl = CLAMP(refl, 0.0f, 1.0f);
		_refresh_taps(w, h, d, refl);

		float er_l[SYMPHONY_MICRO_BLOCK_SIZE];
		float er_r[SYMPHONY_MICRO_BLOCK_SIZE];
		SymphonyShoeboxER::process(runtime, audio_in, er_l, er_r, p_num_frames);

		float peak = 0.0f;
		for (int32_t i = 0; i < p_num_frames; i++) {
			audio_out[i] = 0.5f * (er_l[i] + er_r[i]);
			peak = MAX(peak, Math::abs(audio_out[i]));
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

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "EarlyReflections";
		desc.category = "Delay";
		desc.inputs.push_back({ "audio_in", SymphonyPinType::AUDIO, false });
		desc.inputs.push_back({ "room_width", SymphonyPinType::FLOAT, false });
		desc.inputs.push_back({ "room_height", SymphonyPinType::FLOAT, false });
		desc.inputs.push_back({ "room_depth", SymphonyPinType::FLOAT, false });
		desc.inputs.push_back({ "reflection", SymphonyPinType::FLOAT, false });
		desc.outputs.push_back({ "audio_out", SymphonyPinType::AUDIO, false });
		desc.params.push_back({ "max_room_dim", 40.0f, 1.0f, 200.0f, 1.0f });
		desc.params.push_back({ "room_width", 8.0f, 0.0f, 200.0f, 0.1f });
		desc.params.push_back({ "room_height", 3.0f, 0.0f, 200.0f, 0.1f });
		desc.params.push_back({ "room_depth", 6.0f, 0.0f, 200.0f, 0.1f });
		desc.params.push_back({ "reflection", 0.7f, 0.0f, 1.0f, 0.01f });
		desc.state_size = sizeof(SymphonyEarlyReflections);
		desc.state_align = alignof(SymphonyEarlyReflections);
		desc.extra_arena_bytes = 0;
		desc.extra_arena_bytes_fn = &SymphonyEarlyReflections::calculate_arena_bytes;
		desc.cost_per_sample = 8.0f;
		desc.create_fn = &SymphonyEarlyReflections::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	struct Config {
		float max_room_dim = 40.0f;
		Vector3 dimensions = Vector3(8.0f, 3.0f, 6.0f);
		float reflection = 0.7f;
		int max_delay_samples = 0;
	};

	[[nodiscard]] static Config resolve_config(const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		Config cfg;
		cfg.max_room_dim = p_params.has("max_room_dim") ? (float)p_params["max_room_dim"] : 40.0f;
		cfg.max_room_dim = CLAMP(cfg.max_room_dim, 1.0f, 200.0f);
		cfg.dimensions.x = p_params.has("room_width") ? (float)p_params["room_width"] : 8.0f;
		cfg.dimensions.y = p_params.has("room_height") ? (float)p_params["room_height"] : 3.0f;
		cfg.dimensions.z = p_params.has("room_depth") ? (float)p_params["room_depth"] : 6.0f;
		cfg.reflection = p_params.has("reflection") ? (float)p_params["reflection"] : 0.7f;
		float rate = p_mix_rate > 1.0f ? p_mix_rate : 48000.0f;
		float max_dist = cfg.max_room_dim * 3.0f;
		cfg.max_delay_samples = (int)(max_dist / DEFAULT_SPEED_OF_SOUND * rate) + 2;
		return cfg;
	}

	static size_t calculate_arena_bytes(const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		Config cfg = resolve_config(p_params, p_mix_rate);
		size_t offset = 0;
		if (!SymphonyCheckedMath::bump(offset, sizeof(float) * (size_t)cfg.max_delay_samples, 32)) {
			return std::numeric_limits<size_t>::max();
		}
		return offset;
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		Config cfg = resolve_config(p_params, p_mix_rate);
		void *mem = p_arena.alloc(sizeof(SymphonyEarlyReflections), alignof(SymphonyEarlyReflections));
		if (!mem) {
			return nullptr;
		}
		float *delay_memory = (float *)p_arena.alloc(sizeof(float) * cfg.max_delay_samples, 32);
		if (!delay_memory) {
			return nullptr;
		}
		return new (mem) SymphonyEarlyReflections(p_mix_rate, cfg.max_delay_samples, delay_memory,
				cfg.dimensions, cfg.reflection, DEFAULT_SPEED_OF_SOUND);
	}
};
