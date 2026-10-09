#pragma once

// Shoebox early-reflection geometry + stereo tap-set runtime (no Object deps).

#include <cmath>
#include <cstdint>
#include <cstring>

struct SymphonyShoeboxER {
	static constexpr int NUM_WALLS = 6;
	static constexpr float DEFAULT_SPEED = 343.0f;
	static constexpr int CROSSFADE_SAMPLES = 1024;

	struct Tap {
		float delay_seconds = 0.0f;
		float gain = 0.0f;
	};

	struct StereoTap {
		float delay_seconds = 0.0f;
		float gain_l = 0.0f;
		float gain_r = 0.0f;
	};

	struct TapSet {
		StereoTap taps[NUM_WALLS];
		float first_reflection_s = 0.0f;
	};

	// Legacy absolute-path taps (moved from EarlyReflections).
	static void compute_shoebox_reflections(float dim_x, float dim_y, float dim_z,
			float listener_x, float listener_y, float listener_z,
			float source_x, float source_y, float source_z,
			float reflection, float speed, Tap out[NUM_WALLS]) {
		for (int i = 0; i < NUM_WALLS; i++) {
			out[i] = Tap();
		}
		if (dim_x <= 0.0f || dim_y <= 0.0f || dim_z <= 0.0f) {
			return;
		}
		const float spd = (speed > 1.0f) ? speed : DEFAULT_SPEED;
		const float refl = reflection < 0.0f ? 0.0f : (reflection > 1.0f ? 1.0f : reflection);
		const float half[3] = { dim_x * 0.5f, dim_y * 0.5f, dim_z * 0.5f };
		const float listener[3] = { listener_x, listener_y, listener_z };
		const float source[3] = { source_x, source_y, source_z };
		const float wall_pos[NUM_WALLS] = { half[0], -half[0], half[1], -half[1], half[2], -half[2] };
		const int wall_axis[NUM_WALLS] = { 0, 0, 1, 1, 2, 2 };

		for (int w = 0; w < NUM_WALLS; w++) {
			const int ax = wall_axis[w];
			float image[3] = { source[0], source[1], source[2] };
			image[ax] = 2.0f * wall_pos[w] - source[ax];
			const float dx = image[0] - listener[0];
			const float dy = image[1] - listener[1];
			const float dz = image[2] - listener[2];
			const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
			if (dist < 0.01f) {
				continue;
			}
			out[w].delay_seconds = dist / spd;
			out[w].gain = refl * (1.0f / (dist > 1.0f ? dist : 1.0f));
		}
	}

	// Relative-to-direct taps with L/R pan. Off-centre defaults keep all 6 distinct for cubes.
	// reflection = √(1 − α_mean) when driven from absorption; caller may pass directly.
	static void compute_relative(float dim_x, float dim_y, float dim_z, float reflection, float speed, TapSet &out) {
		out = TapSet();
		if (dim_x <= 0.0f || dim_y <= 0.0f || dim_z <= 0.0f) {
			return;
		}
		const float spd = (speed > 1.0f) ? speed : DEFAULT_SPEED;
		const float refl = reflection < 0.0f ? 0.0f : (reflection > 1.0f ? 1.0f : reflection);
		const float half[3] = { dim_x * 0.5f, dim_y * 0.5f, dim_z * 0.5f };

		// Default off-centre (fractions of half-extents) — all 6 images distinct in a cube.
		const float listener[3] = { -0.18f * half[0], 0.12f * half[1], 0.07f * half[2] };
		const float source[3] = { 0.22f * half[0], -0.09f * half[1], -0.15f * half[2] };

		const float ddx = source[0] - listener[0];
		const float ddy = source[1] - listener[1];
		const float ddz = source[2] - listener[2];
		const float direct = std::sqrt(ddx * ddx + ddy * ddy + ddz * ddz);

		const float wall_pos[NUM_WALLS] = { half[0], -half[0], half[1], -half[1], half[2], -half[2] };
		const int wall_axis[NUM_WALLS] = { 0, 0, 1, 1, 2, 2 };
		// Pan: +X / −X bias left-right; Y/Z milder.
		const float pan_l[NUM_WALLS] = { 0.35f, 0.65f, 0.50f, 0.50f, 0.55f, 0.45f };
		const float pan_r[NUM_WALLS] = { 0.65f, 0.35f, 0.50f, 0.50f, 0.45f, 0.55f };

		float first = 1e9f;
		for (int w = 0; w < NUM_WALLS; w++) {
			const int ax = wall_axis[w];
			float image[3] = { source[0], source[1], source[2] };
			image[ax] = 2.0f * wall_pos[w] - source[ax];
			const float dx = image[0] - listener[0];
			const float dy = image[1] - listener[1];
			const float dz = image[2] - listener[2];
			const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
			float rel = dist - direct;
			if (rel < 0.001f) {
				rel = 0.001f;
			}
			const float delay = rel / spd;
			const float g = refl * (1.0f / (dist > 1.0f ? dist : 1.0f));
			out.taps[w].delay_seconds = delay;
			out.taps[w].gain_l = g * pan_l[w];
			out.taps[w].gain_r = g * pan_r[w];
			if (delay < first) {
				first = delay;
			}
		}
		out.first_reflection_s = (first < 1e8f) ? first : 0.0f;
	}

	struct Runtime {
		float *delay = nullptr;
		int max_delay = 0;
		int write_pos = 0;
		TapSet a;
		TapSet b;
		int crossfade_pos = CROSSFADE_SAMPLES; // >= N → fully on `a`
		bool has_b = false;
		float mix_rate = 48000.0f;
	};

	static void bind_buffer(Runtime &rt, float *mem, int max_delay, float sr) {
		rt.delay = mem;
		rt.max_delay = max_delay;
		rt.write_pos = 0;
		rt.mix_rate = sr;
		rt.crossfade_pos = CROSSFADE_SAMPLES;
		rt.has_b = false;
		if (mem) {
			std::memset(mem, 0, sizeof(float) * (size_t)max_delay);
		}
	}

	static void set_taps(Runtime &rt, const TapSet &next) {
		if (!rt.has_b && rt.crossfade_pos >= CROSSFADE_SAMPLES) {
			rt.a = next;
			rt.has_b = false;
			rt.crossfade_pos = CROSSFADE_SAMPLES;
			return;
		}
		// Crossfade from current audible set to next.
		if (rt.crossfade_pos < CROSSFADE_SAMPLES && rt.has_b) {
			// Already crossfading — snap destination.
			rt.b = next;
			return;
		}
		rt.b = next;
		rt.has_b = true;
		rt.crossfade_pos = 0;
	}

	static void read_set(const Runtime &rt, const TapSet &set, float &l, float &r) {
		l = 0.0f;
		r = 0.0f;
		if (!rt.delay) {
			return;
		}
		for (int w = 0; w < NUM_WALLS; w++) {
			const float g_l = set.taps[w].gain_l;
			const float g_r = set.taps[w].gain_r;
			if (g_l <= 0.0f && g_r <= 0.0f) {
				continue;
			}
			float dly = set.taps[w].delay_seconds * rt.mix_rate;
			if (dly < 0.0f) {
				dly = 0.0f;
			}
			if (dly > (float)(rt.max_delay - 2)) {
				dly = (float)(rt.max_delay - 2);
			}
			float read_pos = (float)rt.write_pos - dly;
			while (read_pos < 0.0f) {
				read_pos += (float)rt.max_delay;
			}
			const int idx = (int)read_pos;
			const float frac = read_pos - (float)idx;
			int next = idx + 1;
			if (next >= rt.max_delay) {
				next = 0;
			}
			const float s = rt.delay[idx] + frac * (rt.delay[next] - rt.delay[idx]);
			l += s * g_l;
			r += s * g_r;
		}
	}

	// Mono in → stereo out early reflections with A/B crossfade.
	static void process(Runtime &rt, const float *in, float *out_l, float *out_r, int frames) {
		for (int i = 0; i < frames; i++) {
			const float x = in ? in[i] : 0.0f;
			if (rt.delay) {
				rt.delay[rt.write_pos] = x;
			}

			float l_a = 0.0f, r_a = 0.0f, l_b = 0.0f, r_b = 0.0f;
			read_set(rt, rt.a, l_a, r_a);
			float l = l_a;
			float r = r_a;
			if (rt.has_b && rt.crossfade_pos < CROSSFADE_SAMPLES) {
				read_set(rt, rt.b, l_b, r_b);
				const float t = (float)rt.crossfade_pos / (float)CROSSFADE_SAMPLES;
				const float wa = 1.0f - t;
				const float wb = t;
				l = wa * l_a + wb * l_b;
				r = wa * r_a + wb * r_b;
				rt.crossfade_pos++;
				if (rt.crossfade_pos >= CROSSFADE_SAMPLES) {
					rt.a = rt.b;
					rt.has_b = false;
				}
			}

			if (rt.delay) {
				rt.write_pos++;
				if (rt.write_pos >= rt.max_delay) {
					rt.write_pos = 0;
				}
			}
			if (out_l) {
				out_l[i] = l;
			}
			if (out_r) {
				out_r[i] = r;
			} else if (out_l) {
				out_l[i] = 0.5f * (l + r);
			}
		}
	}
};
