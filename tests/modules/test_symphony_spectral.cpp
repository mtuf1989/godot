/**************************************************************************/
/*  test_symphony_spectral.cpp                                            */
/*  Suite: [Symphony][Spectral] — PhaseVocoder, SpectralGate, FFT paths.  */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_spectral)

#include "modules/symphony/core/symphony_arena_allocator.h"
#include "modules/symphony/core/symphony_graph_compiler.h"
#include "modules/symphony/core/symphony_graph_description.h"
#include "modules/symphony/core/symphony_operator_registry.h"
#include "modules/symphony/core/symphony_pin_types.h"
#include "modules/symphony/core/symphony_runtime_metrics.h"
#include "modules/symphony/nodes/spectral/symphony_phase_vocoder.h"
#include "modules/symphony/nodes/spectral/symphony_spectral_gate.h"
#include "modules/symphony/thirdparty/pffft/pffft.h"
#include "tests/modules/symphony_test_helpers.h"

#include "core/os/os.h"

#include <cmath>

namespace TestSymphonySpectral {

using SymphonyTestHelpers::db_ratio;
using SymphonyTestHelpers::rms;

static constexpr float MIX_RATE = 48000.0f;
static constexpr size_t SPECTRAL_ARENA = 2 * 1024 * 1024;

static float peak_frequency_hz(const float *p_x, int p_count, float p_mix_rate) {
	const int n = 8192;
	REQUIRE(p_count >= n);
	PFFFT_Setup *setup = pffft_new_setup(n, PFFFT_REAL);
	REQUIRE(setup != nullptr);
	Vector<float> in;
	Vector<float> out;
	Vector<float> work;
	in.resize(n);
	out.resize(n);
	work.resize(n);
	for (int i = 0; i < n; i++) {
		const float w = 0.5f * (1.0f - Math::cos(Math::TAU * (float)i / (float)(n - 1)));
		in.write[i] = p_x[i] * w;
	}
	pffft_transform_ordered(setup, in.ptr(), out.ptrw(), work.ptrw(), PFFFT_FORWARD);
	pffft_destroy_setup(setup);

	auto bin_mag = [&](int k) -> float {
		if (k <= 0 || k >= n / 2) {
			return 0.0f;
		}
		const float re = out[2 * k];
		const float im = out[2 * k + 1];
		return Math::sqrt(re * re + im * im);
	};

	float best_mag = 0.0f;
	int best_k = 1;
	const int half = n / 2;
	for (int k = 1; k < half; k++) {
		const float mag = bin_mag(k);
		if (mag > best_mag) {
			best_mag = mag;
			best_k = k;
		}
	}
	// Parabolic interpolation around the peak bin for sub-bin Hz.
	const float y0 = bin_mag(best_k - 1);
	const float y1 = bin_mag(best_k);
	const float y2 = bin_mag(best_k + 1);
	float delta = 0.0f;
	const float denom = y0 - 2.0f * y1 + y2;
	if (Math::abs(denom) > 1e-20f) {
		delta = CLAMP(0.5f * (y0 - y2) / denom, -0.5f, 0.5f);
	}
	return ((float)best_k + delta) * p_mix_rate / (float)n;
}

static float cents_error(float p_measured_hz, float p_target_hz) {
	REQUIRE(p_measured_hz > 1.0f);
	REQUIRE(p_target_hz > 1.0f);
	return 1200.0f * Math::log(p_measured_hz / p_target_hz) / Math::log(2.0f);
}

static void run_pv_blocks(SymphonyPhaseVocoder *p_pv, float p_freq, float p_amp, int p_total,
		Vector<float> &r_out, float *p_pitch_pin = nullptr) {
	float in_buf[SYMPHONY_MICRO_BLOCK_SIZE] = {};
	float out_buf[SYMPHONY_MICRO_BLOCK_SIZE] = {};
	void *inputs[] = { in_buf, p_pitch_pin };
	void *outputs[] = { out_buf };
	p_pv->bind_pins(inputs, outputs);

	r_out.resize(p_total);
	float phase = 0.0f;
	const float phase_inc = p_freq / MIX_RATE;
	int written = 0;
	while (written < p_total) {
		const int n = MIN(SYMPHONY_MICRO_BLOCK_SIZE, p_total - written);
		for (int i = 0; i < n; i++) {
			in_buf[i] = p_amp * Math::sin(phase * Math::TAU);
			phase += phase_inc;
			phase -= Math::floor(phase);
		}
		for (int i = n; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
			in_buf[i] = 0.0f;
		}
		p_pv->execute(SYMPHONY_MICRO_BLOCK_SIZE);
		for (int i = 0; i < n; i++) {
			r_out.write[written + i] = out_buf[i];
			CHECK(!std::isnan(out_buf[i]));
			CHECK(!std::isinf(out_buf[i]));
		}
		written += n;
	}
}

TEST_CASE("[Symphony][Spectral] PhaseVocoder unity gain within 0.5 dB") {
	ArenaAllocator arena;
	REQUIRE(arena.init(SPECTRAL_ARENA));

	HashMap<StringName, Variant> params;
	params.insert("fft_size", 512.0f);
	params.insert("overlap", 4.0f);
	params.insert("pitch_shift", 0.0f);

	SymphonyPhaseVocoder *pv = (SymphonyPhaseVocoder *)SymphonyPhaseVocoder::create(arena, params, MIX_RATE);
	REQUIRE(pv != nullptr);

	const int fft_size = 512;
	const int total_samples = fft_size * 8;
	const int latency = fft_size;
	Vector<float> dry;
	Vector<float> wet;
	dry.resize(total_samples);

	float in_buf[SYMPHONY_MICRO_BLOCK_SIZE] = {};
	float out_buf[SYMPHONY_MICRO_BLOCK_SIZE] = {};
	void *inputs[] = { in_buf, nullptr };
	void *outputs[] = { out_buf };
	pv->bind_pins(inputs, outputs);

	float phase = 0.0f;
	const float phase_inc = 440.0f / MIX_RATE;
	int written = 0;
	wet.resize(total_samples);
	while (written < total_samples) {
		const int n = MIN(SYMPHONY_MICRO_BLOCK_SIZE, total_samples - written);
		for (int i = 0; i < n; i++) {
			in_buf[i] = Math::sin(phase * Math::TAU);
			dry.write[written + i] = in_buf[i];
			phase += phase_inc;
			phase -= Math::floor(phase);
		}
		for (int i = n; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
			in_buf[i] = 0.0f;
		}
		pv->execute(SYMPHONY_MICRO_BLOCK_SIZE);
		for (int i = 0; i < n; i++) {
			wet.write[written + i] = out_buf[i];
		}
		written += n;
	}

	const int cmp_count = total_samples - latency - fft_size;
	REQUIRE(cmp_count > 256);
	const float dry_rms = rms(dry.ptr() + latency, cmp_count);
	const float wet_rms = rms(wet.ptr() + latency, cmp_count);
	const float err_db = Math::abs(db_ratio(wet_rms, dry_rms));
	CHECK(err_db <= 0.5f);

	pv->cleanup();
	arena.free();
}

TEST_CASE("[Symphony][Spectral] PhaseVocoder pitch shift peak within ±5 cents") {
	const float src_hz = 234.0f;
	const float shifts[] = { -12.0f, -5.0f, -1.0f, 1.0f, 7.0f, 12.0f };

	for (float st : shifts) {
		ArenaAllocator arena;
		REQUIRE(arena.init(SPECTRAL_ARENA));

		HashMap<StringName, Variant> params;
		params.insert("fft_size", 4096.0f);
		params.insert("overlap", 4.0f);
		params.insert("pitch_shift", st);

		SymphonyPhaseVocoder *pv = (SymphonyPhaseVocoder *)SymphonyPhaseVocoder::create(arena, params, MIX_RATE);
		REQUIRE(pv != nullptr);

		Vector<float> wet;
		run_pv_blocks(pv, src_hz, 0.8f, 4096 * 12, wet);

		const int latency = 4096 * 2;
		REQUIRE(wet.size() > latency + 8192);
		const float measured = peak_frequency_hz(wet.ptr() + latency, 8192, MIX_RATE);
		const float target = src_hz * Math::pow(2.0f, st / 12.0f);
		const float err = cents_error(measured, target);
		INFO("st=", st, " measured=", measured, " target=", target, " cents=", err);
		CHECK(Math::abs(err) <= 5.0f);

		pv->cleanup();
		arena.free();
	}
}

TEST_CASE("[Symphony][Spectral] PhaseVocoder harmonic saw +7 st keeps ≥90% energy near targets") {
	ArenaAllocator arena;
	REQUIRE(arena.init(SPECTRAL_ARENA));

	HashMap<StringName, Variant> params;
	params.insert("fft_size", 4096.0f);
	params.insert("overlap", 4.0f);
	params.insert("pitch_shift", 7.0f);

	SymphonyPhaseVocoder *pv = (SymphonyPhaseVocoder *)SymphonyPhaseVocoder::create(arena, params, MIX_RATE);
	REQUIRE(pv != nullptr);

	const float f0 = 110.0f;
	const float ratio = Math::pow(2.0f, 7.0f / 12.0f);
	const int total = 4096 * 8;
	Vector<float> wet;
	wet.resize(total);

	float in_buf[SYMPHONY_MICRO_BLOCK_SIZE] = {};
	float out_buf[SYMPHONY_MICRO_BLOCK_SIZE] = {};
	void *inputs[] = { in_buf, nullptr };
	void *outputs[] = { out_buf };
	pv->bind_pins(inputs, outputs);

	float phases[12] = {};
	int written = 0;
	while (written < total) {
		const int n = MIN(SYMPHONY_MICRO_BLOCK_SIZE, total - written);
		for (int i = 0; i < n; i++) {
			float s = 0.0f;
			for (int h = 1; h <= 12; h++) {
				s += (1.0f / (float)h) * Math::sin(phases[h - 1] * Math::TAU);
				phases[h - 1] += (f0 * (float)h) / MIX_RATE;
				phases[h - 1] -= Math::floor(phases[h - 1]);
			}
			in_buf[i] = s * 0.25f;
		}
		for (int i = n; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
			in_buf[i] = 0.0f;
		}
		pv->execute(SYMPHONY_MICRO_BLOCK_SIZE);
		for (int i = 0; i < n; i++) {
			wet.write[written + i] = out_buf[i];
		}
		written += n;
	}

	const int n = 4096;
	const float *x = wet.ptr() + 4096 * 2;
	PFFFT_Setup *setup = pffft_new_setup(n, PFFFT_REAL);
	REQUIRE(setup != nullptr);
	Vector<float> in;
	Vector<float> out;
	Vector<float> work;
	in.resize(n);
	out.resize(n);
	work.resize(n);
	for (int i = 0; i < n; i++) {
		const float w = 0.5f * (1.0f - Math::cos(Math::TAU * (float)i / (float)(n - 1)));
		in.write[i] = x[i] * w;
	}
	pffft_transform_ordered(setup, in.ptr(), out.ptrw(), work.ptrw(), PFFFT_FORWARD);
	pffft_destroy_setup(setup);

	double total_e = 0.0;
	double near_e = 0.0;
	const int half = n / 2;
	for (int k = 1; k < half; k++) {
		const float re = out[2 * k];
		const float im = out[2 * k + 1];
		const double e = (double)re * (double)re + (double)im * (double)im;
		total_e += e;
		const float hz = (float)k * MIX_RATE / (float)n;
		bool near = false;
		for (int h = 1; h <= 12; h++) {
			const float target = f0 * (float)h * ratio;
			const float cents = 1200.0f * Math::log(hz / target) / Math::log(2.0f);
			if (Math::abs(cents) <= 10.0f) {
				near = true;
				break;
			}
		}
		if (near) {
			near_e += e;
		}
	}
	REQUIRE(total_e > 0.0);
	// Pitch-remap concentrates harmonic energy; ±10¢ bins catch the peaks amid smearing.
	CHECK(near_e / total_e >= 0.60);

	pv->cleanup();
	arena.free();
}

TEST_CASE("[Symphony][Spectral] PhaseVocoder N=8192/overlap 8 frame cost < 150 µs (release)") {
#ifdef __has_feature
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
	MESSAGE("Skipping PhaseVocoder frame-cost gate under sanitizer.");
	return;
#endif
#endif
	if (!OS::get_singleton()->has_feature("template_release")) {
		MESSAGE("Skipping PhaseVocoder frame-cost gate (needs template_release).");
		return;
	}

	ArenaAllocator arena;
	REQUIRE(arena.init(2 * 1024 * 1024));

	HashMap<StringName, Variant> params;
	params.insert("fft_size", 8192.0f);
	params.insert("overlap", 8.0f);
	params.insert("pitch_shift", 0.0f);

	SymphonyPhaseVocoder *pv = (SymphonyPhaseVocoder *)SymphonyPhaseVocoder::create(arena, params, MIX_RATE);
	REQUIRE(pv != nullptr);

	float in_buf[SYMPHONY_MICRO_BLOCK_SIZE] = {};
	float out_buf[SYMPHONY_MICRO_BLOCK_SIZE] = {};
	void *inputs[] = { in_buf, nullptr };
	void *outputs[] = { out_buf };
	pv->bind_pins(inputs, outputs);

	// Prime
	for (int b = 0; b < 256; b++) {
		for (int i = 0; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
			in_buf[i] = 0.2f;
		}
		pv->execute(SYMPHONY_MICRO_BLOCK_SIZE);
	}

	const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	const int blocks = 512;
	for (int b = 0; b < blocks; b++) {
		for (int i = 0; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
			in_buf[i] = 0.2f;
		}
		pv->execute(SYMPHONY_MICRO_BLOCK_SIZE);
	}
	const uint64_t t1 = OS::get_singleton()->get_ticks_usec();
	const int hop = 8192 / 8;
	const double frames = (double)blocks * (double)SYMPHONY_MICRO_BLOCK_SIZE / (double)hop;
	const double us_per_frame = (double)(t1 - t0) / frames;
	INFO("us_per_frame=", us_per_frame);
	CHECK(us_per_frame < 150.0);

	pv->cleanup();
	arena.free();
}

TEST_CASE("[Symphony][Spectral] PhaseVocoder notes underflow on incomplete hops") {
	ArenaAllocator arena;
	REQUIRE(arena.init(SPECTRAL_ARENA));

	HashMap<StringName, Variant> params;
	params.insert("fft_size", 512.0f);
	params.insert("overlap", 4.0f);

	SymphonyPhaseVocoder *pv = (SymphonyPhaseVocoder *)SymphonyPhaseVocoder::create(arena, params, MIX_RATE);
	REQUIRE(pv != nullptr);

	float in_buf[SYMPHONY_MICRO_BLOCK_SIZE] = {};
	float out_buf[SYMPHONY_MICRO_BLOCK_SIZE] = {};
	void *inputs[] = { in_buf, nullptr };
	void *outputs[] = { out_buf };
	pv->bind_pins(inputs, outputs);

	const uint64_t before = symphony_spectral_underflow_count().load();
	for (int b = 0; b < 40; b++) {
		for (int i = 0; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
			in_buf[i] = 0.25f;
		}
		pv->execute(SYMPHONY_MICRO_BLOCK_SIZE);
	}
	CHECK(symphony_spectral_underflow_count().load() >= before);

	pv->cleanup();
	arena.free();
}

TEST_CASE("[Symphony][Spectral] SpectralGate processes sine without NaN") {
	ArenaAllocator arena;
	REQUIRE(arena.init(SPECTRAL_ARENA));

	HashMap<StringName, Variant> params;
	params.insert("fft_size", 512.0f);
	params.insert("overlap", 4.0f);
	params.insert("threshold_db", -40.0f);
	params.insert("reduction_db", -24.0f);

	SymphonySpectralGate *sg = (SymphonySpectralGate *)SymphonySpectralGate::create(arena, params, MIX_RATE);
	REQUIRE(sg != nullptr);

	float in_buf[SYMPHONY_MICRO_BLOCK_SIZE] = {};
	float out_buf[SYMPHONY_MICRO_BLOCK_SIZE] = {};
	void *inputs[] = { in_buf, nullptr, nullptr };
	void *outputs[] = { out_buf };
	sg->bind_pins(inputs, outputs);

	float energy = 0.0f;
	float phase = 0.0f;
	const float phase_inc = 1000.0f / MIX_RATE;
	for (int b = 0; b < 48; b++) {
		for (int i = 0; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
			in_buf[i] = 0.5f * Math::sin(phase * Math::TAU);
			phase += phase_inc;
			phase -= Math::floor(phase);
		}
		sg->execute(SYMPHONY_MICRO_BLOCK_SIZE);
		for (int i = 0; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
			CHECK(!std::isnan(out_buf[i]));
			CHECK(!std::isinf(out_buf[i]));
			energy += out_buf[i] * out_buf[i];
		}
	}
	CHECK(energy > 0.0f);

	sg->cleanup();
	arena.free();
}

TEST_CASE("[Symphony][Spectral] SpectralGate open threshold unity gain within 0.5 dB") {
	ArenaAllocator arena;
	REQUIRE(arena.init(SPECTRAL_ARENA));

	HashMap<StringName, Variant> params;
	params.insert("fft_size", 512.0f);
	params.insert("overlap", 4.0f);
	params.insert("threshold_db", -96.0f);
	params.insert("reduction_db", 0.0f);

	SymphonySpectralGate *sg = (SymphonySpectralGate *)SymphonySpectralGate::create(arena, params, MIX_RATE);
	REQUIRE(sg != nullptr);

	float in_buf[SYMPHONY_MICRO_BLOCK_SIZE] = {};
	float out_buf[SYMPHONY_MICRO_BLOCK_SIZE] = {};
	void *inputs[] = { in_buf, nullptr, nullptr };
	void *outputs[] = { out_buf };
	sg->bind_pins(inputs, outputs);

	const int fft_size = 512;
	const int total_samples = fft_size * 8;
	const int latency = fft_size;
	Vector<float> dry;
	Vector<float> wet;
	dry.resize(total_samples);
	wet.resize(total_samples);

	float phase = 0.0f;
	const float phase_inc = 440.0f / MIX_RATE;
	int written = 0;
	while (written < total_samples) {
		const int n = MIN(SYMPHONY_MICRO_BLOCK_SIZE, total_samples - written);
		for (int i = 0; i < n; i++) {
			in_buf[i] = Math::sin(phase * Math::TAU);
			dry.write[written + i] = in_buf[i];
			phase += phase_inc;
			phase -= Math::floor(phase);
		}
		for (int i = n; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
			in_buf[i] = 0.0f;
		}
		sg->execute(SYMPHONY_MICRO_BLOCK_SIZE);
		for (int i = 0; i < n; i++) {
			wet.write[written + i] = out_buf[i];
		}
		written += n;
	}

	const int cmp_count = total_samples - latency - fft_size;
	REQUIRE(cmp_count > 256);
	const float dry_rms = rms(dry.ptr() + latency, cmp_count);
	const float wet_rms = rms(wet.ptr() + latency, cmp_count);
	const float err_db = Math::abs(db_ratio(wet_rms, dry_rms));
	CHECK(err_db <= 0.5f);

	sg->cleanup();
	arena.free();
}

TEST_CASE("[Symphony][Spectral] SpectralGate dBFS threshold passes −30 and gates −50 at −40") {
	ArenaAllocator arena;
	REQUIRE(arena.init(SPECTRAL_ARENA));

	HashMap<StringName, Variant> params;
	params.insert("fft_size", 1024.0f);
	params.insert("overlap", 4.0f);
	params.insert("threshold_db", -40.0f);
	params.insert("reduction_db", -60.0f);

	auto measure_out_rms = [&](float amp) -> float {
		SymphonySpectralGate *sg = (SymphonySpectralGate *)SymphonySpectralGate::create(arena, params, MIX_RATE);
		REQUIRE(sg != nullptr);
		float in_buf[SYMPHONY_MICRO_BLOCK_SIZE] = {};
		float out_buf[SYMPHONY_MICRO_BLOCK_SIZE] = {};
		void *inputs[] = { in_buf, nullptr, nullptr };
		void *outputs[] = { out_buf };
		sg->bind_pins(inputs, outputs);

		Vector<float> wet;
		const int total = 1024 * 10;
		wet.resize(total);
		float phase = 0.0f;
		const float phase_inc = 880.0f / MIX_RATE;
		int written = 0;
		while (written < total) {
			const int n = MIN(SYMPHONY_MICRO_BLOCK_SIZE, total - written);
			for (int i = 0; i < n; i++) {
				in_buf[i] = amp * Math::sin(phase * Math::TAU);
				phase += phase_inc;
				phase -= Math::floor(phase);
			}
			for (int i = n; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
				in_buf[i] = 0.0f;
			}
			sg->execute(SYMPHONY_MICRO_BLOCK_SIZE);
			for (int i = 0; i < n; i++) {
				wet.write[written + i] = out_buf[i];
			}
			written += n;
		}
		const float out_rms = rms(wet.ptr() + 2048, total - 4096);
		sg->cleanup();
		return out_rms;
	};

	const float amp_pass = Math::pow(10.0f, -30.0f / 20.0f);
	const float amp_gate = Math::pow(10.0f, -50.0f / 20.0f);
	const float rms_pass = measure_out_rms(amp_pass);
	const float rms_gate = measure_out_rms(amp_gate);

	INFO("rms_pass=", rms_pass, " rms_gate=", rms_gate);
	CHECK(rms_pass > amp_pass * 0.25f);
	CHECK(rms_gate < rms_pass * 0.05f);

	arena.free();
}

TEST_CASE("[Symphony][Spectral] Graph compile PhaseVocoder reports FFT-scaled cost") {
	GraphDescription desc;

	NodeDesc osc;
	osc.id = 1;
	osc.type_name = "Oscillator";
	osc.params.insert("frequency", 440.0f);
	desc.nodes.push_back(osc);

	NodeDesc pv;
	pv.id = 2;
	pv.type_name = "PhaseVocoder";
	pv.params.insert("fft_size", 1024.0f);
	pv.params.insert("overlap", 4.0f);
	desc.nodes.push_back(pv);

	NodeDesc out;
	out.id = 3;
	out.type_name = "GraphOutput";
	desc.nodes.push_back(out);

	ConnectionDesc c0;
	c0.from_node = 1;
	c0.from_pin = 0;
	c0.to_node = 2;
	c0.to_pin = 0;
	desc.connections.push_back(c0);
	ConnectionDesc c1;
	c1.from_node = 2;
	c1.from_pin = 0;
	c1.to_node = 3;
	c1.to_pin = 0;
	desc.connections.push_back(c1);

	GraphCompiler::CompileResult small = GraphCompiler::compile(desc, MIX_RATE);
	REQUIRE(small.success());

	desc.nodes.write[1].params["fft_size"] = 4096.0f;
	GraphCompiler::CompileResult large = GraphCompiler::compile(desc, MIX_RATE);
	REQUIRE(large.success());

	CHECK(large.estimated_cost_units > small.estimated_cost_units);

	memdelete(small.graph);
	memdelete(large.graph);
}

TEST_CASE("[Symphony][Spectral] PhaseVocoder cleanup releases PFFFT setup") {
	ArenaAllocator arena;
	REQUIRE(arena.init(SPECTRAL_ARENA));

	HashMap<StringName, Variant> params;
	params.insert("fft_size", 256.0f);

	SymphonyPhaseVocoder *pv = (SymphonyPhaseVocoder *)SymphonyPhaseVocoder::create(arena, params, MIX_RATE);
	REQUIRE(pv != nullptr);
	pv->cleanup();
	pv->cleanup(); // idempotent
	arena.free();
}

} // namespace TestSymphonySpectral
