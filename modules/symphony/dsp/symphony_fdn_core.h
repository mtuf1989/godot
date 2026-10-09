#pragma once

// SymphonyFDNCore — header-only FDN late-reverb core (no Object / ClassDB).
// Used by FDNReverb graph node and AudioEffectSymphonyReverb.

#include "../core/symphony_denormal_guard.h"

#include <cmath>
#include <cstdint>
#include <cstring>

struct SymphonyFDNCore {
	static constexpr int MAX_LINES = 8;
	static constexpr float SPREAD = 2.3f;
	static constexpr float TAP_SMOOTH_SEC = 0.050f;
	static constexpr float MODE_DENSITY_THRESHOLD_S = 0.125f;

	// Verified sign vectors (not Hadamard rows). c_L · c_R = 0.
	static constexpr float B8[8] = { 1, 1, 1, -1, 1, -1, -1, -1 };
	static constexpr float C_L8[8] = { 1, 1, 1, 1, 1, 1, -1, -1 };
	static constexpr float C_R8[8] = { 1, 1, 1, -1, 1, -1, 1, 1 };
	static constexpr float B4[4] = { 1, 1, 1, -1 };

	struct Design {
		int num_lines = 4;
		int delays[MAX_LINES] = {}; // samples
		float g[MAX_LINES] = {}; // DC loop gain
		float a[MAX_LINES] = {}; // shelf coeff (1-r)/(1+r)
		float out_gain = 1.0f;
		float b[MAX_LINES] = {};
		float c_l[MAX_LINES] = {};
		float c_r[MAX_LINES] = {};
		float mean_delay_s = 0.0f;
		float sr = 48000.0f;
		float rt60 = 1.0f;
		float hf_ratio = 1.0f;
	};

	struct State {
		float *buffers[MAX_LINES] = {};
		int max_delay = 0;
		int num_lines = 0;
		int write_pos[MAX_LINES] = {};
		float delay_f[MAX_LINES] = {};
		float delay_target[MAX_LINES] = {};
		float damp_z[MAX_LINES] = {};
		float tap_smooth = 1.0f;

		float g_cur[MAX_LINES] = {};
		float a_cur[MAX_LINES] = {};
		float out_gain_cur = 0.0f;
		float g_target[MAX_LINES] = {};
		float a_target[MAX_LINES] = {};
		float out_gain_target = 0.0f;
		float b[MAX_LINES] = {};
		float c_l[MAX_LINES] = {};
		float c_r[MAX_LINES] = {};
		float param_smooth = 1.0f; // per-sample toward target over ~block
	};

	static int gcd_i(int a, int b) {
		a = a < 0 ? -a : a;
		b = b < 0 ? -b : b;
		while (b != 0) {
			const int t = a % b;
			a = b;
			b = t;
		}
		return a;
	}

	static void fast_wht(const float *in, float *out, int n) {
		if (n == 4) {
			out[0] = in[0] + in[1] + in[2] + in[3];
			out[1] = in[0] - in[1] + in[2] - in[3];
			out[2] = in[0] + in[1] - in[2] - in[3];
			out[3] = in[0] - in[1] - in[2] + in[3];
			return;
		}
		// n == 8 via H4⊗H2
		float ta[4], tb[4];
		for (int k = 0; k < 4; k++) {
			ta[k] = in[k] + in[k + 4];
			tb[k] = in[k] - in[k + 4];
		}
		out[0] = ta[0] + ta[1] + ta[2] + ta[3];
		out[1] = ta[0] - ta[1] + ta[2] - ta[3];
		out[2] = ta[0] + ta[1] - ta[2] - ta[3];
		out[3] = ta[0] - ta[1] - ta[2] + ta[3];
		out[4] = tb[0] + tb[1] + tb[2] + tb[3];
		out[5] = tb[0] - tb[1] + tb[2] - tb[3];
		out[6] = tb[0] + tb[1] - tb[2] - tb[3];
		out[7] = tb[0] - tb[1] - tb[2] + tb[3];
	}

	// Pure design. Allocation-free. Returns false on invalid args.
	static bool design(float sr, int min_lines, float mean_delay_s, float rt60, float hf_ratio, int max_line, Design &out) {
		if (!(sr > 1.0f) || !(mean_delay_s > 0.0f) || !(rt60 > 0.01f) || max_line < 4) {
			return false;
		}
		hf_ratio = hf_ratio < 0.05f ? 0.05f : (hf_ratio > 1.0f ? 1.0f : hf_ratio);
		min_lines = (min_lines >= 8) ? 8 : 4;

		int n = min_lines;
		if ((float)n * mean_delay_s < MODE_DENSITY_THRESHOLD_S) {
			n = 8;
		}

		out = Design();
		out.num_lines = n;
		out.sr = sr;
		out.rt60 = rt60;
		out.hf_ratio = hf_ratio;
		out.mean_delay_s = mean_delay_s;

		const float mean_samp = mean_delay_s * sr;
		for (int i = 0; i < n; i++) {
			const float t = ((float)i + 0.5f) / (float)n - 0.5f;
			float target = mean_samp * std::pow(SPREAD, t);
			int d = (int)std::lround(target);
			d = d < 2 ? 2 : d;
			if (d > max_line - 2) {
				d = max_line - 2;
			}
			// Bump until gcd=1 with every earlier line.
			bool ok = false;
			for (int tries = 0; tries < 4096 && !ok; tries++) {
				ok = true;
				for (int j = 0; j < i; j++) {
					if (gcd_i(d, out.delays[j]) != 1) {
						ok = false;
						break;
					}
				}
				if (!ok) {
					d++;
					if (d > max_line - 2) {
						d = max_line - 2;
						// If stuck at max, accept best-effort (still prefer odd vs previous).
						ok = true;
						for (int j = 0; j < i; j++) {
							if (gcd_i(d, out.delays[j]) != 1) {
								ok = false;
							}
						}
						if (!ok && d > 3) {
							d -= (tries % 3) + 1;
						}
					}
				}
			}
			out.delays[i] = d;
		}

		// Sort ascending for density / test expectations.
		for (int i = 0; i < n; i++) {
			for (int j = i + 1; j < n; j++) {
				if (out.delays[j] < out.delays[i]) {
					const int tmp = out.delays[i];
					out.delays[i] = out.delays[j];
					out.delays[j] = tmp;
				}
			}
		}

		const float rt60_hf = rt60 * hf_ratio;
		float sum_d = 0.0f;
		for (int i = 0; i < n; i++) {
			const float di = (float)out.delays[i];
			sum_d += di;
			const float g_dc = std::pow(10.0f, -3.0f * di / (rt60 * sr));
			const float g_ny = std::pow(10.0f, -3.0f * di / (rt60_hf * sr));
			float r = (g_dc > 1e-20f) ? (g_ny / g_dc) : 1.0f;
			r = r < 0.0f ? 0.0f : (r > 1.0f ? 1.0f : r);
			out.g[i] = g_dc;
			out.a[i] = (1.0f - r) / (1.0f + r);
		}

		const float mean_d_s = (sum_d / (float)n) / sr;
		const float x = 6.90775527898f * mean_d_s / rt60; // ln(10)*3 * mean/rt60
		const float energy = (1.0f - std::exp(-x)) / (2.0f * (float)n);
		out.out_gain = 0.905f * std::sqrt(energy > 1e-20f ? energy : 1e-20f);

		if (n == 8) {
			for (int i = 0; i < 8; i++) {
				out.b[i] = B8[i];
				out.c_l[i] = C_L8[i];
				out.c_r[i] = C_R8[i];
			}
		} else {
			for (int i = 0; i < 4; i++) {
				out.b[i] = B4[i];
				out.c_l[i] = 1.0f;
				out.c_r[i] = 1.0f;
			}
		}
		return true;
	}

	static void bind_buffers(State &st, float *delay_memory, int num_lines, int max_delay, float sr) {
		st.num_lines = num_lines;
		st.max_delay = max_delay;
		for (int i = 0; i < num_lines; i++) {
			st.buffers[i] = delay_memory + i * max_delay;
			st.write_pos[i] = 0;
			st.damp_z[i] = 0.0f;
			st.delay_f[i] = 2.0f;
			st.delay_target[i] = 2.0f;
		}
		std::memset(delay_memory, 0, sizeof(float) * (size_t)num_lines * (size_t)max_delay);
		const float smooth_n = TAP_SMOOTH_SEC * sr;
		st.tap_smooth = (smooth_n > 1.0f) ? (1.0f - std::exp(-1.0f / smooth_n)) : 1.0f;
		st.param_smooth = (sr > 1.0f) ? (1.0f - std::exp(-1.0f / (0.020f * sr))) : 1.0f;
	}

	static void apply_design(State &st, const Design &d, bool snap) {
		st.num_lines = d.num_lines;
		for (int i = 0; i < d.num_lines; i++) {
			float tgt = (float)d.delays[i];
			if (tgt > (float)(st.max_delay - 2)) {
				tgt = (float)(st.max_delay - 2);
			}
			if (tgt < 2.0f) {
				tgt = 2.0f;
			}
			st.delay_target[i] = tgt;
			st.g_target[i] = d.g[i];
			st.a_target[i] = d.a[i];
			st.b[i] = d.b[i];
			st.c_l[i] = d.c_l[i];
			st.c_r[i] = d.c_r[i];
			if (snap) {
				st.delay_f[i] = tgt;
				st.g_cur[i] = d.g[i];
				st.a_cur[i] = d.a[i];
			}
		}
		st.out_gain_target = d.out_gain;
		if (snap) {
			st.out_gain_cur = d.out_gain;
		}
	}

	static float read_tap(const State &st, int line, float delay_samples) {
		float read_pos = (float)st.write_pos[line] - delay_samples;
		const float md = (float)st.max_delay;
		while (read_pos < 0.0f) {
			read_pos += md;
		}
		while (read_pos >= md) {
			read_pos -= md;
		}
		const int idx = (int)read_pos;
		const float frac = read_pos - (float)idx;
		int next = idx + 1;
		if (next >= st.max_delay) {
			next = 0;
		}
		const float *buf = st.buffers[line];
		return buf[idx] + frac * (buf[next] - buf[idx]);
	}

	// Process mono input → stereo (or mono if out_r == nullptr: (L+R)*0.5).
	static void process(State &st, const float *in, float *out_l, float *out_r, int frames) {
		const int n = st.num_lines;
		const float inv_sqrt_n = 1.0f / std::sqrt((float)n);
		const float ps = st.param_smooth;

		for (int i = 0; i < frames; i++) {
			for (int l = 0; l < n; l++) {
				st.g_cur[l] += (st.g_target[l] - st.g_cur[l]) * ps;
				st.a_cur[l] += (st.a_target[l] - st.a_cur[l]) * ps;
				st.delay_f[l] += (st.delay_target[l] - st.delay_f[l]) * st.tap_smooth;
			}
			st.out_gain_cur += (st.out_gain_target - st.out_gain_cur) * ps;

			float taps[MAX_LINES];
			for (int l = 0; l < n; l++) {
				taps[l] = read_tap(st, l, st.delay_f[l]);
			}

			float mixed[MAX_LINES];
			fast_wht(taps, mixed, n);
			for (int l = 0; l < n; l++) {
				mixed[l] *= inv_sqrt_n;
			}

			const float x = in ? in[i] : 0.0f;
			float acc_l = 0.0f;
			float acc_r = 0.0f;
			for (int l = 0; l < n; l++) {
				const float a = st.a_cur[l];
				const float g = st.g_cur[l];
				// H(z) = g*(1-a)/(1 - a z^-1); DC=g, Nyquist=g*(1-a)/(1+a)
				st.damp_z[l] = a * st.damp_z[l] + g * (1.0f - a) * mixed[l];
				symphony_flush_denormal(st.damp_z[l]);
				st.buffers[l][st.write_pos[l]] = st.b[l] * x + st.damp_z[l];
				st.write_pos[l]++;
				if (st.write_pos[l] >= st.max_delay) {
					st.write_pos[l] = 0;
				}
				acc_l += st.c_l[l] * taps[l];
				acc_r += st.c_r[l] * taps[l];
			}

			const float yl = acc_l * st.out_gain_cur;
			const float yr = acc_r * st.out_gain_cur;
			if (out_l) {
				out_l[i] = yl;
			}
			if (out_r) {
				out_r[i] = yr;
			} else if (out_l) {
				out_l[i] = 0.5f * (yl + yr);
			}
		}
	}

	static void clear(State &st) {
		for (int l = 0; l < st.num_lines; l++) {
			if (st.buffers[l]) {
				std::memset(st.buffers[l], 0, sizeof(float) * (size_t)st.max_delay);
			}
			st.write_pos[l] = 0;
			st.damp_z[l] = 0.0f;
		}
	}
};
