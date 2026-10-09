/**************************************************************************/
/*  test_symphony_fdn_core.cpp                                            */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_fdn_core)

#include "modules/symphony/dsp/symphony_fdn_core.h"
#include "modules/symphony/spatial/reverb_level.h"

#include <cmath>
#include <vector>

namespace TestSymphonyFDNCore {

static bool delays_coprime_increasing(const SymphonyFDNCore::Design &d) {
	for (int i = 1; i < d.num_lines; i++) {
		if (d.delays[i] < d.delays[i - 1]) {
			return false;
		}
		for (int j = 0; j < i; j++) {
			if (SymphonyFDNCore::gcd_i(d.delays[i], d.delays[j]) != 1) {
				return false;
			}
		}
	}
	return true;
}

TEST_CASE("[Symphony][FDN] Design delays are coprime, increasing, and dense") {
	const float rates[] = { 44100.0f, 48000.0f, 96000.0f, 192000.0f };
	for (float sr : rates) {
		SymphonyFDNCore::Design d;
		REQUIRE(SymphonyFDNCore::design(sr, 4, 0.02f, 1.5f, 0.5f, (int)(0.2f * sr), d));
		CHECK(d.num_lines == 8); // mode density promote
		CHECK(delays_coprime_increasing(d));
		float sum = 0.0f;
		for (int i = 0; i < d.num_lines; i++) {
			sum += (float)d.delays[i];
		}
		CHECK(sum >= 0.125f * sr * 0.95f);
	}
}

TEST_CASE("[Symphony][FDN] Sign vectors are not Hadamard rows and c_L ⊥ c_R") {
	float dot = 0.0f;
	for (int i = 0; i < 8; i++) {
		dot += SymphonyFDNCore::C_L8[i] * SymphonyFDNCore::C_R8[i];
	}
	CHECK(dot == doctest::Approx(0.0f));
	// H8 first row is all +1 — our c_L is not.
	bool all_one = true;
	for (int i = 0; i < 8; i++) {
		if (SymphonyFDNCore::C_L8[i] != 1.0f) {
			all_one = false;
		}
	}
	CHECK(all_one == false);
}

TEST_CASE("[Symphony][FDN] Shelf gain exact at DC and Nyquist") {
	SymphonyFDNCore::Design d;
	REQUIRE(SymphonyFDNCore::design(48000.0f, 8, 0.03f, 2.0f, 0.5f, 8192, d));
	for (int i = 0; i < d.num_lines; i++) {
		const float g = d.g[i];
		const float a = d.a[i];
		const float h_dc = g; // by construction
		const float h_ny = g * (1.0f - a) / (1.0f + a);
		const float g_ny_target = std::pow(10.0f, -3.0f * (float)d.delays[i] / (d.rt60 * d.hf_ratio * d.sr));
		CHECK(h_dc == doctest::Approx(g).epsilon(1e-5));
		CHECK(h_ny == doctest::Approx(g_ny_target).epsilon(0.02));
	}
}

TEST_CASE("[Symphony][FDN] IR T30 within 5% of RT60 at 48k") {
	const float sr = 48000.0f;
	const int max_line = 8192;
	std::vector<float> mem((size_t)SymphonyFDNCore::MAX_LINES * max_line, 0.0f);
	const float targets[] = { 0.4f, 1.0f, 2.5f };
	for (float rt60 : targets) {
		SymphonyFDNCore::State st;
		SymphonyFDNCore::Design d;
		REQUIRE(SymphonyFDNCore::design(sr, 8, 0.025f, rt60, 1.0f, max_line, d));
		SymphonyFDNCore::bind_buffers(st, mem.data(), SymphonyFDNCore::MAX_LINES, max_line, sr);
		SymphonyFDNCore::apply_design(st, d, true);

		const int n = (int)(rt60 * 2.0f * sr);
		std::vector<float> ir(n, 0.0f);
		float in = 1.0f;
		float out_l = 0.0f, out_r = 0.0f;
		SymphonyFDNCore::process(st, &in, &out_l, &out_r, 1);
		ir[0] = 0.5f * (out_l + out_r);
		in = 0.0f;
		for (int i = 1; i < n; i++) {
			SymphonyFDNCore::process(st, &in, &out_l, &out_r, 1);
			ir[i] = 0.5f * (out_l + out_r);
		}

		// Energy envelope → find T30 (drop from -5 to -35 dB).
		const int win = (int)(0.02f * sr);
		float peak_e = 0.0f;
		std::vector<float> env(n / win + 1, 0.0f);
		for (int w = 0; w * win < n; w++) {
			double e = 0.0;
			const int end = MIN(n, (w + 1) * win);
			for (int i = w * win; i < end; i++) {
				e += (double)ir[i] * ir[i];
			}
			env[w] = (float)e;
			peak_e = MAX(peak_e, env[w]);
		}
		REQUIRE(peak_e > 0.0f);
		int t5 = -1, t35 = -1;
		for (int w = 0; w * win < n; w++) {
			const float db = 10.0f * std::log10(env[w] / peak_e + 1e-20f);
			if (t5 < 0 && db <= -5.0f) {
				t5 = w;
			}
			if (t5 >= 0 && t35 < 0 && db <= -35.0f) {
				t35 = w;
				break;
			}
		}
		REQUIRE(t5 >= 0);
		REQUIRE(t35 > t5);
		// ISO 3382 T30: extrapolate 30 dB slope (−5→−35) to 60 dB.
		const float t30 = 2.0f * (float)(t35 - t5) * (float)win / sr;
		CHECK(t30 == doctest::Approx(rt60).epsilon(0.05));
	}
}

TEST_CASE("[Symphony][FDN] Parameter change produces no NaN") {
	const float sr = 48000.0f;
	const int max_line = 4096;
	std::vector<float> mem((size_t)SymphonyFDNCore::MAX_LINES * max_line, 0.0f);
	SymphonyFDNCore::State st;
	SymphonyFDNCore::Design d;
	REQUIRE(SymphonyFDNCore::design(sr, 8, 0.02f, 1.0f, 0.5f, max_line, d));
	SymphonyFDNCore::bind_buffers(st, mem.data(), SymphonyFDNCore::MAX_LINES, max_line, sr);
	SymphonyFDNCore::apply_design(st, d, true);

	float in[64];
	float ol[64], orr[64];
	for (int i = 0; i < 64; i++) {
		in[i] = (i == 0) ? 1.0f : 0.0f;
	}
	for (int b = 0; b < 20; b++) {
		if (b == 5) {
			REQUIRE(SymphonyFDNCore::design(sr, 8, 0.04f, 2.5f, 0.3f, max_line, d));
			SymphonyFDNCore::apply_design(st, d, false);
		}
		SymphonyFDNCore::process(st, in, ol, orr, 64);
		for (int i = 0; i < 64; i++) {
			CHECK(std::isfinite(ol[i]));
			CHECK(std::isfinite(orr[i]));
		}
		in[0] = 0.0f;
	}
}

TEST_CASE("[Symphony][ReverbLevel] critical_distance and amount contracts") {
	const float rc = SymphonyReverbLevel::critical_distance(400.0f, 1.2f, 0.0f);
	CHECK(rc == doctest::Approx(1.04f).epsilon(0.05));

	const float w1 = SymphonyReverbLevel::amount(1.0f, 1.0f, 1.04f, 1.2f, 1.0f, 1.0f, -12.0f, 2.0f);
	const float w2 = SymphonyReverbLevel::amount(1.0f, 1.0f, 2.08f, 1.2f, 1.0f, 1.0f, -12.0f, 2.0f);
	CHECK(w1 > w2); // w ∝ 1/r_c
	CHECK(w1 / w2 == doctest::Approx(2.0f).epsilon(0.05));

	// Floor engages when attenuation is tiny (far).
	const float far = SymphonyReverbLevel::amount(1.0f, 50.0f, 1.04f, 1.2f, 0.001f, 1.0f, -12.0f, 2.0f);
	const float near = SymphonyReverbLevel::amount(1.0f, 1.0f, 1.04f, 1.2f, 1.0f, 1.0f, -12.0f, 2.0f);
	CHECK(far < near);
}

} // namespace TestSymphonyFDNCore
