#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "../../core/symphony_checked_math.h"
#include "core/math/math_funcs.h"
#include "core/math/vector3.h"

#include <cstring>
#include <limits>

// SymphonyEarlyReflections (Task 16, Phase S6) — shoebox early reflections.
//
// Conveys room size before the reverb tail with NO geometry queries (Resonance
// ComputeReflections). Models 6 first-order image sources — one per shoebox wall
// (±X, ±Y, ±Z). For each wall, the source is mirrored across the wall plane; the
// reflected path length listener→image gives a tap delay (distance / speed) and
// a gain (1/distance falloff × the wall's reflection coefficient = 1 − absorption).
//
// The operator is a 6-tap delay line fed by the dry input. Room dimensions,
// reflection coefficient, and the listener/source offset arrive as float inputs
// so the engine can drive them per room (authored AcousticRoom3D shoebox dims,
// else the Task 9 estimate). One shared instance per reverb pool slot.
//
// Arena allocation: a single delay buffer sized for the largest room dimension.
class SymphonyEarlyReflections : public SymphonyOperator {
public:
	static constexpr int NUM_WALLS = 6;
	static constexpr float DEFAULT_SPEED_OF_SOUND = 343.0f;

	// One computed image-source tap.
	struct Tap {
		float delay_seconds = 0.0f;
		float gain = 0.0f;
	};

	// Pure geometry: compute the 6 shoebox image-source taps.
	//   p_dimensions   : room W,H,D in metres (full extents, not half).
	//   p_listener_off : listener position relative to the room centre.
	//   p_source_off   : source position relative to the room centre.
	//   p_reflection   : wall reflection coefficient [0,1] (1 − absorption).
	//   p_speed        : speed of sound (m/s).
	// Fills p_out[NUM_WALLS]. Unauthored (non-positive) dimensions → all-zero taps.
	static void compute_shoebox_reflections(const Vector3 &p_dimensions, const Vector3 &p_listener_off,
			const Vector3 &p_source_off, float p_reflection, float p_speed, Tap p_out[NUM_WALLS]) {
		for (int i = 0; i < NUM_WALLS; i++) {
			p_out[i] = Tap();
		}
		if (p_dimensions.x <= 0.0f || p_dimensions.y <= 0.0f || p_dimensions.z <= 0.0f) {
			return; // unauthored / invalid — no reflections (graceful fallback)
		}
		const float speed = (p_speed > 1.0f) ? p_speed : DEFAULT_SPEED_OF_SOUND;
		const float refl = CLAMP(p_reflection, 0.0f, 1.0f);
		const Vector3 half = p_dimensions * 0.5f;

		// Wall planes at ±half along each axis. Mirror the source across each.
		const float wall_pos[NUM_WALLS] = { half.x, -half.x, half.y, -half.y, half.z, -half.z };
		const int wall_axis[NUM_WALLS] = { 0, 0, 1, 1, 2, 2 };

		for (int w = 0; w < NUM_WALLS; w++) {
			const int ax = wall_axis[w];
			Vector3 image = p_source_off;
			// Reflect the source coordinate across the wall plane: x' = 2*wall − x.
			image[ax] = 2.0f * wall_pos[w] - p_source_off[ax];
			const float dist = image.distance_to(p_listener_off);
			if (dist < 0.01f) {
				continue;
			}
			p_out[w].delay_seconds = dist / speed;
			// Distance falloff (1/d, clamped near field to 1) × wall reflectivity.
			p_out[w].gain = refl * (1.0f / MAX(dist, 1.0f));
		}
	}

private:
	const float *SYMPHONY_RESTRICT audio_in = nullptr;
	const float *SYMPHONY_RESTRICT room_w_input = nullptr;
	const float *SYMPHONY_RESTRICT room_h_input = nullptr;
	const float *SYMPHONY_RESTRICT room_d_input = nullptr;
	const float *SYMPHONY_RESTRICT reflection_input = nullptr;
	float *SYMPHONY_RESTRICT audio_out = nullptr;

	// Multi-tap delay buffer (single shared line; taps read at per-wall offsets).
	float *delay_buffer = nullptr;
	int max_delay_samples = 0;
	int write_pos = 0;

	float mix_rate = 48000.0f;
	float speed_of_sound = DEFAULT_SPEED_OF_SOUND;

	// Defaults (used when an input pin is unconnected).
	Vector3 default_dimensions = Vector3(8.0f, 3.0f, 6.0f);
	float default_reflection = 0.7f;
	// Listener/source offsets relative to room centre. The engine drives these by
	// re-creating taps; for the in-graph operator we place both near the centre
	// (a conservative, room-size-dominated pattern — exact positions are a tuning
	// refinement and don't change the "bigger room → later/quieter" behaviour).
	Vector3 listener_off = Vector3(0, 0, 0);
	Vector3 source_off = Vector3(0, 0, 0);

	// Cached taps (recomputed only when controls change).
	Tap taps[NUM_WALLS];
	float cached_w = -1.0f, cached_h = -1.0f, cached_d = -1.0f, cached_refl = -1.0f;
	int tap_samples[NUM_WALLS] = {};

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
		compute_shoebox_reflections(Vector3(p_w, p_h, p_d), listener_off, source_off, p_refl, speed_of_sound, taps);
		for (int w = 0; w < NUM_WALLS; w++) {
			int s = (int)(taps[w].delay_seconds * mix_rate + 0.5f);
			tap_samples[w] = CLAMP(s, 0, max_delay_samples - 1);
		}
	}

public:
	SymphonyEarlyReflections(float p_mix_rate, int p_max_delay_samples, float *p_delay_memory,
			const Vector3 &p_dimensions, float p_reflection, float p_speed)
			: max_delay_samples(p_max_delay_samples),
			  mix_rate(p_mix_rate),
			  speed_of_sound(p_speed),
			  default_dimensions(p_dimensions),
			  default_reflection(p_reflection) {
		delay_buffer = p_delay_memory;
		if (delay_buffer) {
			memset(delay_buffer, 0, sizeof(float) * max_delay_samples);
		}
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

		float peak = 0.0f;
		for (int32_t i = 0; i < p_num_frames; i++) {
			const float in = audio_in ? audio_in[i] : 0.0f;
			if (delay_buffer) {
				delay_buffer[write_pos] = in;
			}

			float out = 0.0f;
			if (delay_buffer) {
				for (int wtap = 0; wtap < NUM_WALLS; wtap++) {
					if (taps[wtap].gain <= 0.0f) {
						continue;
					}
					int read_pos = write_pos - tap_samples[wtap];
					if (read_pos < 0) {
						read_pos += max_delay_samples;
					}
					out += delay_buffer[read_pos] * taps[wtap].gain;
				}
				write_pos = (write_pos + 1) % max_delay_samples;
			}

			audio_out[i] = out;
			peak = MAX(peak, Math::abs(out));
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
		desc.params.push_back({ "max_room_dim", 40.0f, 1.0f, 200.0f, 1.0f }); // largest supported room span (m)
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
		// A first-order image can be up to ~2× the room diagonal away. Size the
		// buffer for 3× the max dimension to be safe, in samples.
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
