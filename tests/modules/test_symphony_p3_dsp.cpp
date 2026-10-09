/**************************************************************************/
/*  test_symphony_p3_dsp.cpp                                              */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_p3_dsp)

#include "modules/symphony/core/symphony_arena_allocator.h"
#include "modules/symphony/core/symphony_graph_compiler.h"
#include "modules/symphony/core/symphony_graph_description.h"
#include "modules/symphony/nodes/filters/symphony_biquad_filter.h"
#include "modules/symphony/nodes/filters/symphony_dc_blocker.h"
#include "modules/symphony/nodes/filters/symphony_saturator.h"
#include "modules/symphony/nodes/filters/symphony_waveshaper.h"
#include "modules/symphony/nodes/generators/symphony_fm_oscillator.h"
#include "modules/symphony/nodes/generators/symphony_formant_osc.h"
#include "tests/modules/symphony_test_helpers.h"

#include <algorithm>
#include <cmath>
#include <vector>

using SymphonyTestHelpers::db_ratio;
using SymphonyTestHelpers::render_graph;
using SymphonyTestHelpers::tone_level_db;

namespace TestSymphonyP3Dsp {

static constexpr float SR = 48000.0f;
static constexpr size_t ARENA_SIZE = 1 << 20;

static GraphDescription make_osc_saturator_graph(float p_hz, float p_drive, int p_mode, bool p_oversample) {
	GraphDescription desc;
	desc.smooth_parameters = false;
	desc.anti_alias_staircase = false;

	NodeDesc osc;
	osc.id = 1;
	osc.type_name = "Oscillator";
	osc.params.insert("frequency", p_hz);
	osc.params.insert("waveform", 0.0f);
	desc.nodes.push_back(osc);

	NodeDesc sat;
	sat.id = 2;
	sat.type_name = "Saturator";
	sat.params.insert("drive", p_drive);
	sat.params.insert("mode", (float)p_mode);
	sat.params.insert("oversample", p_oversample ? 1.0f : 0.0f);
	desc.nodes.push_back(sat);

	NodeDesc out;
	out.id = 3;
	out.type_name = "GraphOutput";
	desc.nodes.push_back(out);

	ConnectionDesc c1;
	c1.from_node = 1;
	c1.from_pin = 0;
	c1.to_node = 2;
	c1.to_pin = 0;
	desc.connections.push_back(c1);
	ConnectionDesc c2;
	c2.from_node = 2;
	c2.from_pin = 0;
	c2.to_node = 3;
	c2.to_pin = 0;
	desc.connections.push_back(c2);
	return desc;
}

static double tanh_antideriv64(double u) {
	double abs_u = fabs(u);
	return abs_u + log(1.0 + exp(-2.0 * abs_u)) - 0.6931471805599453;
}

static float peak_spur_db(const Vector<float> &p_buf, float p_sr, float p_fund_hz) {
	// Hann-windowed DFT; ignore ±4 bins around the fundamental (leakage skirt).
	const int n = p_buf.size();
	const double bin_hz = (double)p_sr / (double)n;
	const int fund_bin = (int)Math::round((double)p_fund_hz / bin_hz);
	auto bin_mag = [&](int k) -> double {
		double re = 0.0, im = 0.0;
		const double w = Math::TAU * (double)k / (double)n;
		for (int i = 0; i < n; i++) {
			const double hann = 0.5 * (1.0 - Math::cos(Math::TAU * (double)i / (double)(n - 1)));
			const double x = (double)p_buf[i] * hann;
			re += x * Math::cos(w * (double)i);
			im -= x * Math::sin(w * (double)i);
		}
		return Math::sqrt(re * re + im * im) * 4.0 / (double)n; // *2 for real DFT, *2 for Hann coherent gain
	};
	const double fund = bin_mag(fund_bin);
	double worst = 1e-30;
	for (int k = 1; k < n / 2; k++) {
		if (Math::abs(k - fund_bin) <= 4) {
			continue;
		}
		worst = MAX(worst, bin_mag(k));
	}
	return db_ratio((float)worst, (float)MAX(fund, 1e-30));
}

TEST_CASE("[Symphony][P3][Saturator] Half-band identity spectral purity and passthrough") {
	// Drive the saturator with a pure sinf tone (Oscillator's fast_sine floor is ~−60 dB).
	// Hard-clip with |x·drive| < 1 is an identity nonlinearity.
	const int frames = 9600;
	for (float hz : { 1000.0f, 5000.0f, 10000.0f, 12000.0f }) {
		ArenaAllocator arena;
		arena.init(ARENA_SIZE);
		HashMap<StringName, Variant> params;
		params.insert("drive", 1.0f);
		params.insert("mode", 1.0f); // hard
		params.insert("oversample", 1.0f);
		SymphonyOperator *op = SymphonySaturator::create(arena, params, SR);
		REQUIRE(op != nullptr);

		float in_buf[SYMPHONY_MICRO_BLOCK_SIZE];
		float out_buf[SYMPHONY_MICRO_BLOCK_SIZE];
		void *inputs[] = { in_buf, nullptr };
		void *outputs[] = { out_buf };
		op->bind_pins(inputs, outputs);

		Vector<float> out;
		out.resize(frames);
		const float amp = 0.25f;
		for (int off = 0; off < frames; off += SYMPHONY_MICRO_BLOCK_SIZE) {
			const int n = MIN(SYMPHONY_MICRO_BLOCK_SIZE, frames - off);
			for (int i = 0; i < n; i++) {
				in_buf[i] = amp * sinf((float)Math::TAU * hz * (float)(off + i) / SR);
			}
			op->execute(n);
			for (int i = 0; i < n; i++) {
				out.write[off + i] = out_buf[i];
			}
		}

		Vector<float> steady;
		steady.resize(frames / 2);
		for (int i = 0; i < frames / 2; i++) {
			steady.write[i] = out[frames / 2 + i];
		}

		const float fund = tone_level_db(steady, SR, hz);
		const float spur = peak_spur_db(steady, SR, hz);
		INFO("hz=", hz, " fund=", fund, " spur=", spur);
		CHECK(spur <= -65.0f);

		if (hz <= 5000.0f) {
			CHECK(fund >= -12.15f);
			CHECK(fund <= -11.85f);
		} else {
			CHECK(fund >= -13.0f);
			CHECK(fund <= -11.5f);
		}
		arena.free();
	}
}

TEST_CASE("[Symphony][P3][Saturator] 2x oversample reduces folded alias spur") {
	// Soft-clip of 5 kHz produces a 7th harmonic at 35 kHz that folds to 13 kHz.
	const int frames = 16384;
	const float hz = 5000.0f;
	Vector<float> os_off = render_graph(make_osc_saturator_graph(hz, 8.0f, 0, false), frames, SR);
	Vector<float> os_on = render_graph(make_osc_saturator_graph(hz, 8.0f, 0, true), frames, SR);
	const float spur_off = tone_level_db(os_off, SR, 13000.0f);
	const float spur_on = tone_level_db(os_on, SR, 13000.0f);
	INFO("spur_off=", spur_off, " spur_on=", spur_on);
	CHECK(spur_on < spur_off - 15.0f);
}

TEST_CASE("[Symphony][P3][Saturator] Drive modulation does not spike") {
	ArenaAllocator arena;
	arena.init(ARENA_SIZE);
	HashMap<StringName, Variant> params;
	params.insert("drive", 2.0f);
	params.insert("mode", 0.0f);
	SymphonyOperator *op = SymphonySaturator::create(arena, params, SR);
	REQUIRE(op != nullptr);

	float in_buf[SYMPHONY_MICRO_BLOCK_SIZE];
	float drive_buf = 2.0f;
	float out_buf[SYMPHONY_MICRO_BLOCK_SIZE] = {};
	void *inputs[] = { in_buf, &drive_buf };
	void *outputs[] = { out_buf };
	op->bind_pins(inputs, outputs);

	float peak = 0.0f;
	for (int b = 0; b < 200; b++) {
		// Alternate drive 2 ↔ 6 every block (LFO-like).
		drive_buf = (b & 1) ? 6.0f : 2.0f;
		for (int i = 0; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
			in_buf[i] = 0.8f * sinf((float)Math::TAU * 440.0f * (float)(b * SYMPHONY_MICRO_BLOCK_SIZE + i) / SR);
		}
		op->execute(SYMPHONY_MICRO_BLOCK_SIZE);
		for (int i = 0; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
			REQUIRE(std::isfinite(out_buf[i]));
			peak = MAX(peak, fabsf(out_buf[i]));
		}
	}
	CHECK(peak <= 1.0f + 1e-3f);
	arena.free();
}

TEST_CASE("[Symphony][P3][Saturator] ADAA matches float64 reference within -90 dBFS") {
	ArenaAllocator arena;
	arena.init(ARENA_SIZE);
	HashMap<StringName, Variant> params;
	params.insert("drive", 10.0f);
	params.insert("mode", 0.0f);
	SymphonyOperator *op = SymphonySaturator::create(arena, params, SR);
	REQUIRE(op != nullptr);

	const int frames = 4800; // 100 ms
	float in_buf[SYMPHONY_MICRO_BLOCK_SIZE];
	float out_buf[SYMPHONY_MICRO_BLOCK_SIZE];
	void *inputs[] = { in_buf, nullptr };
	void *outputs[] = { out_buf };
	op->bind_pins(inputs, outputs);

	double prev_u = 0.0;
	double prev_F = tanh_antideriv64(0.0);
	double err_energy = 0.0;
	double sig_energy = 0.0;
	const double drive = 10.0;
	const double amp = 0.9;

	for (int off = 0; off < frames; off += SYMPHONY_MICRO_BLOCK_SIZE) {
		const int n = MIN(SYMPHONY_MICRO_BLOCK_SIZE, frames - off);
		for (int i = 0; i < n; i++) {
			in_buf[i] = (float)(amp * sin(Math::TAU * 30.0 * (double)(off + i) / (double)SR));
		}
		op->execute(n);
		for (int i = 0; i < n; i++) {
			double u1 = (double)in_buf[i] * drive;
			double F1 = tanh_antideriv64(u1);
			double diff = u1 - prev_u;
			double ref;
			if (fabs(diff) > 1e-3) {
				ref = (F1 - prev_F) / diff;
			} else {
				ref = tanh(0.5 * (prev_u + u1));
			}
			prev_u = u1;
			prev_F = F1;
			double e = (double)out_buf[i] - ref;
			err_energy += e * e;
			sig_energy += ref * ref;
		}
	}
	float err_db = db_ratio((float)Math::sqrt(err_energy / frames), 1.0f);
	INFO("err_dbfs=", err_db, " sig_rms=", Math::sqrt(sig_energy / frames));
	CHECK(err_db < -90.0f);
	arena.free();
}

TEST_CASE("[Symphony][P3][Waveshaper] FOLDBACK table is continuous") {
	SymphonyWaveshaper::ensure_shared_tables();
	float table[SymphonyWaveshaper::PRESET_TABLE_SIZE];
	SymphonyWaveshaper::generate_table(table, SymphonyWaveshaper::PRESET_TABLE_SIZE, SymphonyWaveshaper::PRESET_FOLDBACK);
	float max_df = 0.0f;
	for (int i = 1; i < SymphonyWaveshaper::PRESET_TABLE_SIZE; i++) {
		max_df = MAX(max_df, fabsf(table[i] - table[i - 1]));
	}
	CHECK(max_df < 0.05f);
}

TEST_CASE("[Symphony][P3][Waveshaper] ADAA error vs exact below -85 dB") {
	SymphonyWaveshaper::ensure_shared_tables();
	ArenaAllocator arena;
	arena.init(ARENA_SIZE);
	HashMap<StringName, Variant> params;
	params.insert("preset", 0.0f); // soft clip
	params.insert("drive", 1.0f);
	SymphonyOperator *op = SymphonyWaveshaper::create(arena, params, SR);
	REQUIRE(op != nullptr);

	auto exact_adaa = [](double x0, double x1) -> double {
		// Soft clip f(x)=tanh(2x); F(x)=0.5*ln(cosh(2x))
		auto F = [](double x) {
			double u = 2.0 * x;
			double abs_u = fabs(u);
			return 0.5 * (abs_u + log(1.0 + exp(-2.0 * abs_u)) - 0.6931471805599453);
		};
		auto f = [](double x) { return tanh(2.0 * x); };
		double diff = x1 - x0;
		if (fabs(diff) < 1e-9) {
			return f(0.5 * (x0 + x1));
		}
		return (F(x1) - F(x0)) / diff;
	};

	for (float amp : { 0.001f, 0.01f, 0.05f, 0.3f }) {
		float in_buf[SYMPHONY_MICRO_BLOCK_SIZE];
		float out_buf[SYMPHONY_MICRO_BLOCK_SIZE];
		void *inputs[] = { in_buf, nullptr };
		void *outputs[] = { out_buf };
		op->bind_pins(inputs, outputs);

		// Reset by recreating.
		arena.free();
		arena.init(ARENA_SIZE);
		op = SymphonyWaveshaper::create(arena, params, SR);
		op->bind_pins(inputs, outputs);

		double prev_x = 0.0;
		double err_e = 0.0;
		const int blocks = 40;
		for (int b = 0; b < blocks; b++) {
			for (int i = 0; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
				in_buf[i] = amp * sinf((float)Math::TAU * 440.0f * (float)(b * SYMPHONY_MICRO_BLOCK_SIZE + i) / SR);
			}
			op->execute(SYMPHONY_MICRO_BLOCK_SIZE);
			for (int i = 0; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
				double x1 = CLAMP((double)in_buf[i], -1.0, 1.0);
				double ref = exact_adaa(prev_x, x1);
				double e = (double)out_buf[i] - ref;
				err_e += e * e;
				prev_x = x1;
			}
		}
		const int n = blocks * SYMPHONY_MICRO_BLOCK_SIZE;
		float err_db = db_ratio((float)Math::sqrt(err_e / n), 1.0f);
		INFO("amp=", amp, " err_db=", err_db);
		CHECK(err_db < -85.0f);
	}
	arena.free();
}

TEST_CASE("[Symphony][P3][FM] Carson clamp keeps alias energy low") {
	GraphDescription desc;
	desc.smooth_parameters = false;
	NodeDesc fm;
	fm.id = 1;
	fm.type_name = "FMOscillator";
	fm.params.insert("carrier_freq", 5000.0f);
	fm.params.insert("mod_freq", 3000.0f);
	fm.params.insert("mod_index", 10.0f);
	desc.nodes.push_back(fm);
	NodeDesc out;
	out.id = 2;
	out.type_name = "GraphOutput";
	desc.nodes.push_back(out);
	ConnectionDesc c{ 1, 0, 2, 0, false };
	desc.connections.push_back(c);

	Vector<float> buf = render_graph(desc, 48000, SR);
	// Intentional FM sidebands sit on fc±k·fm; anything else is alias residue.
	const int n = buf.size();
	const double bin_hz = (double)SR / (double)n;
	const double half_window = 2.0 * bin_hz;
	double on_grid = 0.0;
	double off_grid = 0.0;
	for (int k = 1; k < n / 2; k++) {
		const double hz = (double)k * bin_hz;
		double re = 0.0, im = 0.0;
		const double w = Math::TAU * (double)k / (double)n;
		for (int i = 0; i < n; i++) {
			re += (double)buf[i] * Math::cos(w * (double)i);
			im -= (double)buf[i] * Math::sin(w * (double)i);
		}
		const double p = re * re + im * im;
		bool on = false;
		for (int side = -12; side <= 12; side++) {
			const double target = Math::abs(5000.0 + (double)side * 3000.0);
			if (target <= 0.0 || target >= 0.5 * (double)SR) {
				continue;
			}
			if (Math::abs(hz - target) <= half_window) {
				on = true;
				break;
			}
		}
		if (on) {
			on_grid += p;
		} else {
			off_grid += p;
		}
	}
	const float alias_db = db_ratio((float)Math::sqrt(MAX(off_grid, 1e-30)), (float)Math::sqrt(MAX(on_grid, 1e-30)));
	INFO("fm_alias_db=", alias_db);
	CHECK(alias_db < -45.0f);
}

TEST_CASE("[Symphony][P3][Compiler] Staircase LPF cutoff tracks sample rate") {
	CHECK(GraphCompiler::staircase_lpf_cutoff(22050.0f) == doctest::Approx(8820.0f));
	CHECK(GraphCompiler::staircase_lpf_cutoff(48000.0f) == doctest::Approx(18000.0f));
	CHECK(GraphCompiler::staircase_lpf_cutoff(96000.0f) == doctest::Approx(18000.0f));
}

TEST_CASE("[Symphony][P3][FormantOsc] Non-integer ratio has no wrap clicks") {
	for (float ratio : { 7.27f, 8.5f }) {
		ArenaAllocator arena;
		arena.init(ARENA_SIZE);
		HashMap<StringName, Variant> params;
		params.insert("fundamental_freq", 100.0f);
		params.insert("formant_freq", 100.0f * ratio);
		params.insert("bandwidth", 80.0f);
		SymphonyOperator *op = SymphonyFormantOsc::create(arena, params, SR);
		REQUIRE(op != nullptr);

		float out_buf[SYMPHONY_MICRO_BLOCK_SIZE];
		void *inputs[] = { nullptr, nullptr, nullptr };
		void *outputs[] = { out_buf };
		op->bind_pins(inputs, outputs);

		std::vector<float> steps;
		float prev = 0.0f;
		bool have_prev = false;
		for (int b = 0; b < 100; b++) {
			op->execute(SYMPHONY_MICRO_BLOCK_SIZE);
			for (int i = 0; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
				if (have_prev) {
					steps.push_back(fabsf(out_buf[i] - prev));
				}
				prev = out_buf[i];
				have_prev = true;
			}
		}
		REQUIRE(steps.size() > 10);
		// Median over the active portion of the envelope (silence dominates otherwise).
		std::vector<float> active;
		float peak_step = 0.0f;
		for (float s : steps) {
			peak_step = MAX(peak_step, s);
		}
		for (float s : steps) {
			if (s >= 0.1f * peak_step) {
				active.push_back(s);
			}
		}
		REQUIRE(active.size() > 10);
		std::sort(active.begin(), active.end());
		float median = active[active.size() / 2];
		float max_step = active.back();
		INFO("ratio=", ratio, " max=", max_step, " median=", median);
		CHECK(max_step < 3.0f * MAX(median, 1e-6f));
		arena.free();
	}
}

TEST_CASE("[Symphony][P3][DCBlocker] -3 dB near 15 Hz across rates") {
	auto find_minus3 = [](float sr) -> float {
		ArenaAllocator arena;
		arena.init(ARENA_SIZE);
		SymphonyOperator *op = SymphonyDCBlocker::create(arena, HashMap<StringName, Variant>(), sr);
		REQUIRE(op != nullptr);

		float best_hz = 0.0f;
		float best_err = 1e9f;
		for (float hz = 5.0f; hz <= 40.0f; hz += 0.5f) {
			// Recreate for clean state.
			arena.free();
			arena.init(ARENA_SIZE);
			op = SymphonyDCBlocker::create(arena, HashMap<StringName, Variant>(), sr);
			float in_buf[SYMPHONY_MICRO_BLOCK_SIZE];
			float out_buf[SYMPHONY_MICRO_BLOCK_SIZE];
			void *inputs[] = { in_buf };
			void *outputs[] = { out_buf };
			op->bind_pins(inputs, outputs);

			double in_e = 0.0;
			double out_e = 0.0;
			const int blocks = (int)(sr / SYMPHONY_MICRO_BLOCK_SIZE); // ~1 s
			for (int b = 0; b < blocks; b++) {
				for (int i = 0; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
					in_buf[i] = sinf((float)Math::TAU * hz * (float)(b * SYMPHONY_MICRO_BLOCK_SIZE + i) / sr);
				}
				op->execute(SYMPHONY_MICRO_BLOCK_SIZE);
				// Skip first 200 ms settling.
				if (b * SYMPHONY_MICRO_BLOCK_SIZE < (int)(0.2f * sr)) {
					continue;
				}
				for (int i = 0; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
					in_e += (double)in_buf[i] * in_buf[i];
					out_e += (double)out_buf[i] * out_buf[i];
				}
			}
			float gain_db = db_ratio((float)Math::sqrt(out_e), (float)Math::sqrt(in_e));
			float err = fabsf(gain_db + 3.0f);
			if (err < best_err) {
				best_err = err;
				best_hz = hz;
			}
		}
		arena.free();
		return best_hz;
	};

	for (float sr : { 44100.0f, 48000.0f, 96000.0f }) {
		float hz = find_minus3(sr);
		INFO("sr=", sr, " minus3_hz=", hz);
		CHECK(hz >= 13.0f);
		CHECK(hz <= 17.0f);
	}
}

TEST_CASE("[Symphony][P3][Biquad] Smoothed cutoff tracks per-sample ground truth") {
	ArenaAllocator arena;
	arena.init(ARENA_SIZE);
	HashMap<StringName, Variant> params;
	params.insert("mode", 0.0f);
	params.insert("cutoff", 200.0f);
	params.insert("resonance", 4.0f);
	SymphonyOperator *op = SymphonyBiquadFilter::create(arena, params, SR);
	REQUIRE(op != nullptr);

	// Ground-truth: same RBJ LP with per-sample coefficient update, no smoothing delay
	// beyond the same one-pole on cutoff (τ=5ms) and every-sample recalc.
	float gt_x1 = 0, gt_x2 = 0, gt_y1 = 0, gt_y2 = 0;
	float s_fc = 200.0f;
	const float a = expf(-1.0f / (0.005f * SR));
	const float one_m_a = 1.0f - a;
	const float q = 4.0f;

	auto coeffs = [&](float fc, float &b0, float &b1, float &b2, float &a1, float &a2) {
		float w0 = (float)Math::TAU * CLAMP(fc, 20.0f, SR * 0.49f) / SR;
		float cos_w0 = cosf(w0);
		float sin_w0 = sinf(w0);
		float alpha = sin_w0 / (2.0f * q);
		b0 = (1.0f - cos_w0) * 0.5f;
		b1 = 1.0f - cos_w0;
		b2 = b0;
		float a0 = 1.0f + alpha;
		a1 = -2.0f * cos_w0;
		a2 = 1.0f - alpha;
		float inv = 1.0f / a0;
		b0 *= inv;
		b1 *= inv;
		b2 *= inv;
		a1 *= inv;
		a2 *= inv;
	};

	float in_buf[SYMPHONY_MICRO_BLOCK_SIZE];
	float cut_buf[SYMPHONY_MICRO_BLOCK_SIZE];
	float out_buf[SYMPHONY_MICRO_BLOCK_SIZE];
	void *inputs[] = { in_buf, cut_buf };
	void *outputs[] = { out_buf };
	op->bind_pins(inputs, outputs);

	double err_e = 0.0;
	double sig_e = 0.0;
	// ~4 s sweep so the every-16-sample coeff refresh stays within −50 dB of per-sample GT.
	const int blocks = (int)(4.0f * SR / SYMPHONY_MICRO_BLOCK_SIZE);
	for (int b = 0; b < blocks; b++) {
		for (int i = 0; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
			int n = b * SYMPHONY_MICRO_BLOCK_SIZE + i;
			float t = (float)n / (float)(blocks * SYMPHONY_MICRO_BLOCK_SIZE);
			cut_buf[i] = 200.0f + t * (4000.0f - 200.0f);
			in_buf[i] = sinf((float)Math::TAU * 800.0f * (float)n / SR);
		}
		op->execute(SYMPHONY_MICRO_BLOCK_SIZE);

		for (int i = 0; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
			s_fc += one_m_a * (cut_buf[i] - s_fc);
			float b0, b1, b2, a1c, a2c;
			coeffs(s_fc, b0, b1, b2, a1c, a2c);
			float x0 = in_buf[i];
			float y0 = b0 * x0 + b1 * gt_x1 + b2 * gt_x2 - a1c * gt_y1 - a2c * gt_y2;
			gt_x2 = gt_x1;
			gt_x1 = x0;
			gt_y2 = gt_y1;
			gt_y1 = y0;
			double e = (double)out_buf[i] - (double)y0;
			err_e += e * e;
			sig_e += (double)y0 * y0;
		}
	}
	float err_db = db_ratio((float)Math::sqrt(err_e), (float)Math::sqrt(MAX(sig_e, 1e-30)));
	INFO("err_db=", err_db);
	CHECK(err_db < -50.0f);
	arena.free();
}

} // namespace TestSymphonyP3Dsp
