#pragma once

// Pure DRR / reverb-send amount helpers (P6). Main-thread only; no Object deps.

#include <cmath>

struct SymphonyReverbLevel {
	// r_c = clamp(0.057·√(V/T)·10^(bias/20), 0.05, 50)
	static float critical_distance(float volume_m3, float rt60_s, float bias_db = 0.0f) {
		if (!(volume_m3 > 0.0f) || !(rt60_s > 0.01f)) {
			return 1.0f;
		}
		const float base = 0.057f * std::sqrt(volume_m3 / rt60_s);
		const float scaled = base * std::pow(10.0f, bias_db / 20.0f);
		if (scaled < 0.05f) {
			return 0.05f;
		}
		if (scaled > 50.0f) {
			return 50.0f;
		}
		return scaled;
	}

	// w = send · min((r_ref/r_c)·exp(−0.02·r/T), a_r·10^(−min_drr/20))
	// amount = clamp(w, 0, max_amount)
	static float amount(float send, float distance_m, float r_c, float rt60_s, float atten_linear,
			float r_ref = 1.0f, float min_drr_db = -12.0f, float max_amount = 2.0f) {
		if (!(send > 0.0f) || !(r_c > 0.0f) || !(rt60_s > 0.01f)) {
			return 0.0f;
		}
		const float r = distance_m > 0.0f ? distance_m : 0.0f;
		const float geometric = (r_ref / r_c) * std::exp(-0.02f * r / rt60_s);
		const float floor_lin = atten_linear * std::pow(10.0f, -min_drr_db / 20.0f);
		float w = send * (geometric < floor_lin ? geometric : floor_lin);
		if (w < 0.0f) {
			w = 0.0f;
		}
		if (w > max_amount) {
			w = max_amount;
		}
		return w;
	}
};
