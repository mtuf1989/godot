/**************************************************************************/
/*  test_symphony_stress.cpp                                              */
/*  Suite: [Symphony][Stress] — memory budget, retirement, mix timing.    */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_stress)

#include "modules/symphony/core/symphony_graph_compiler.h"
#include "modules/symphony/core/symphony_graph_description.h"
#include "modules/symphony/core/symphony_graph_package_retirement.h"
#include "modules/symphony/core/symphony_memory_budget.h"
#include "modules/symphony/core/symphony_platform_time.h"
#include "modules/symphony/core/symphony_prepared_graph_package.h"
#include "modules/symphony/core/symphony_realtime_scope.h"
#include "modules/symphony/core/symphony_voice_manager.h"
#include "modules/symphony/stream/audio_stream_playback_symphony.h"
#include "modules/symphony/stream/audio_stream_symphony.h"

#include "core/os/os.h"
#include "core/os/thread.h"
#include "core/templates/vector.h"

#include <atomic>

namespace TestSymphonyStress {

struct BudgetGuard {
	SymphonyMemoryBudget *budget = nullptr;
	size_t global = 0;
	size_t per_graph = 0;

	BudgetGuard() {
		budget = SymphonyMemoryBudget::get_singleton();
		if (budget) {
			// Recover from aborted runs that left a tiny limit installed.
			if (budget->get_global_limit_bytes() < size_t(1024) * 1024) {
				budget->set_global_limit_bytes(SymphonyMemoryBudget::DEFAULT_GLOBAL_BYTES);
			}
			if (budget->get_per_graph_limit_bytes() < size_t(1024) * 1024) {
				budget->set_per_graph_limit_bytes(SymphonyMemoryBudget::DEFAULT_PER_GRAPH_BYTES);
			}
			global = budget->get_global_limit_bytes();
			per_graph = budget->get_per_graph_limit_bytes();
		}
	}

	~BudgetGuard() {
		if (budget) {
			budget->set_global_limit_bytes(global);
			budget->set_per_graph_limit_bytes(per_graph);
		}
	}
};

static GraphDescription _make_delay_graph(float p_max_delay_ms) {
	GraphDescription desc;

	NodeDesc osc;
	osc.id = 1;
	osc.type_name = "Oscillator";
	osc.params.insert("frequency", 440.0f);
	osc.params.insert("waveform", 0.0f);
	desc.nodes.push_back(osc);

	NodeDesc delay;
	delay.id = 2;
	delay.type_name = "DelayLine";
	delay.params.insert("max_delay_ms", p_max_delay_ms);
	delay.params.insert("delay_ms", MIN(p_max_delay_ms * 0.5f, 100.0f));
	desc.nodes.push_back(delay);

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
	return desc;
}

static void _percentile_us(Vector<uint64_t> &p_samples, uint64_t &r_median, uint64_t &r_p99) {
	REQUIRE(p_samples.size() > 0);
	p_samples.sort();
	r_median = p_samples[p_samples.size() / 2];
	const int p99_idx = (int)((p_samples.size() - 1) * 99 / 100);
	r_p99 = p_samples[p99_idx];
}

// Release regression baselines: macos arm64 template_release.
// Workload: 3 trials × 64 samples × (32 × 512 frames) @ 48 kHz; gated value = median of trials.
// Calibrated 2026-08-13 (median of five outer runs; p99 = max observed across those runs).
// Strict plan gates on template_release: median ≤ +5%, p99 ≤ +10% (plus tiny µs floor).
struct MixTimingBaseline {
	const char *label;
	GraphDescription (*builder)();
	uint64_t median_us;
	uint64_t p99_us;
};

static uint64_t _median_limit_us(uint64_t p_baseline) {
	const uint64_t pct = (p_baseline * 105ull + 99ull) / 100ull; // ceil(+5%)
	const uint64_t abs = p_baseline + 2ull;
	return pct > abs ? pct : abs;
}

static uint64_t _p99_limit_us(uint64_t p_baseline) {
	const uint64_t pct = (p_baseline * 110ull + 99ull) / 100ull; // ceil(+10%)
	const uint64_t abs = p_baseline + 5ull;
	return pct > abs ? pct : abs;
}

static void _execute_mix_frames(PreparedGraphPackage *p_pkg, AudioFrame *p_buf, int p_frames) {
	for (int off = 0; off < p_frames; off += SYMPHONY_MICRO_BLOCK_SIZE) {
		p_pkg->graph_output->set_output(p_buf, off);
		p_pkg->graph->execute(SYMPHONY_MICRO_BLOCK_SIZE);
	}
}

TEST_CASE("[Symphony][Stress] Global memory budget rejects without leaking reservation") {
	BudgetGuard guard;
	REQUIRE(guard.budget != nullptr);

	const size_t reserved_before = guard.budget->get_snapshot().reserved_bytes;

	// Tiny global budget forces rejection after a few 96 kHz delay packages.
	guard.budget->set_global_limit_bytes(size_t(2) * 1024 * 1024);
	guard.budget->set_per_graph_limit_bytes(size_t(8) * 1024 * 1024);

	Vector<PreparedGraphPackage *> live;
	int rejected = 0;
	for (int i = 0; i < 32; i++) {
		GraphCompiler::CompileResult result = GraphCompiler::compile(_make_delay_graph(500.0f), 96000.0f);
		if (!result.success()) {
			rejected++;
			CHECK(result.graph == nullptr);
			continue;
		}
		PreparedGraphPackage *pkg = PreparedGraphPackage::create_from_graph(result.graph, result.arena_bytes, result.total_package_bytes);
		REQUIRE(pkg != nullptr);
		live.push_back(pkg);
	}

	CHECK(live.size() > 0);
	CHECK(rejected > 0);

	for (int i = 0; i < live.size(); i++) {
		PreparedGraphPackage::destroy(live[i]);
	}
	GraphPackageRetirement::drain();

	CHECK(guard.budget->get_snapshot().reserved_bytes == reserved_before);
}

TEST_CASE("[Symphony][Stress] Failed compile preserves audible package on playback") {
	BudgetGuard guard;
	REQUIRE(guard.budget != nullptr);

	Ref<AudioStreamSymphony> stream;
	stream.instantiate();
	stream->set_mix_rate(48000.0f);
	stream->set_graph_description(_make_delay_graph(50.0f));

	Ref<AudioStreamPlayback> base = stream->instantiate_playback();
	Ref<AudioStreamPlaybackSymphony> playback = base;
	REQUIRE(playback.is_valid());
	playback->start();

	AudioFrame buf[64];
	REQUIRE(playback->mix(buf, 1.0f, 64) == 64);
	const float cost_before = playback->get_estimated_cost_units();
	CHECK(cost_before > 0.0f);

	guard.budget->set_global_limit_bytes(1); // Force next compile to fail reservation.

	CompiledGraph *failed = stream->compile_graph();
	CHECK(failed == nullptr);

	// Still audible / controllable after rejected compile.
	CHECK(playback->mix(buf, 1.0f, 64) == 64);
	CHECK(playback->get_estimated_cost_units() == doctest::Approx(cost_before));

	playback->stop();
	GraphPackageRetirement::drain();
}

TEST_CASE("[Symphony][Stress] Peak live packages stay within current+outgoing+pending") {
	BudgetGuard guard;

	Ref<AudioStreamSymphony> stream;
	stream.instantiate();
	stream->set_mix_rate(48000.0f);
	stream->set_graph_description(AudioStreamSymphony::build_test_graph_10_nodes());

	Ref<AudioStreamPlayback> base = stream->instantiate_playback();
	Ref<AudioStreamPlaybackSymphony> playback = base;
	REQUIRE(playback.is_valid());
	playback->start();

	AudioFrame buf[128];
	playback->mix(buf, 1.0f, 128);

	REQUIRE(guard.budget != nullptr);
	const SymphonyMemoryBudget::Snapshot snap0 = guard.budget->get_snapshot();
	const uint32_t base_live = snap0.active_packages + snap0.pending_packages + snap0.outgoing_packages;

	for (int i = 0; i < 8; i++) {
		CompiledGraph *g = stream->compile_graph();
		REQUIRE(g != nullptr);
		playback->swap_graph(g);
		playback->mix(buf, 1.0f, 128);

		const SymphonyMemoryBudget::Snapshot snap = guard.budget->get_snapshot();
		const uint32_t live = snap.active_packages + snap.pending_packages + snap.outgoing_packages;
		CHECK(live <= base_live + 3);
	}

	playback->stop();
	GraphPackageRetirement::drain();
	CHECK(GraphPackageRetirement::get_pending_count() == 0);
}

TEST_CASE("[Symphony][Stress] Retirement and reserved bytes return to baseline after teardown") {
	BudgetGuard guard;
	REQUIRE(guard.budget != nullptr);

	const size_t reserved_before = guard.budget->get_snapshot().reserved_bytes;
	const uint32_t retired_before = GraphPackageRetirement::get_pending_count();

	Vector<PreparedGraphPackage *> pkgs;
	for (int i = 0; i < 6; i++) {
		GraphCompiler::CompileResult result = GraphCompiler::compile(AudioStreamSymphony::build_test_graph_10_nodes(), 48000.0f);
		REQUIRE(result.success());
		PreparedGraphPackage *pkg = PreparedGraphPackage::create_from_graph(result.graph, result.arena_bytes, result.total_package_bytes);
		REQUIRE(pkg != nullptr);
		pkgs.push_back(pkg);
	}

	for (int i = 0; i < pkgs.size(); i++) {
		GraphPackageRetirement::retire(pkgs[i]);
	}
	CHECK(GraphPackageRetirement::get_pending_count() == retired_before + (uint32_t)pkgs.size());

	GraphPackageRetirement::drain();
	CHECK(GraphPackageRetirement::get_pending_count() == retired_before);
	CHECK(guard.budget->get_snapshot().reserved_bytes == reserved_before);
}

TEST_CASE("[Symphony][Stress] GrainCloud cost units stay conservative vs measured mix") {
	// If µs/cost_unit for GrainCloud ≫ oscillator reference, admission would under-estimate.
	BudgetGuard guard;

	auto time_graph_us = [](const GraphDescription &p_desc, float &r_cost_units) -> uint64_t {
		GraphCompiler::CompileResult result = GraphCompiler::compile(p_desc, 48000.0f);
		REQUIRE(result.success());
		PreparedGraphPackage *pkg = PreparedGraphPackage::create_from_graph(result.graph, result.arena_bytes, result.total_package_bytes);
		REQUIRE(pkg != nullptr);
		REQUIRE(pkg->graph_output != nullptr);
		r_cost_units = pkg->estimated_cost_units;

		AudioFrame buf[512];
		const int frames = 512;
		const int reps = 16;
		for (int w = 0; w < 4; w++) {
			for (int r = 0; r < reps; r++) {
				for (int off = 0; off < frames; off += SYMPHONY_MICRO_BLOCK_SIZE) {
					pkg->graph_output->set_output(buf, off);
					pkg->graph->execute(SYMPHONY_MICRO_BLOCK_SIZE);
				}
			}
		}

		Vector<uint64_t> samples;
		samples.resize(32);
		for (int i = 0; i < samples.size(); i++) {
			const uint64_t t0 = symphony_time_usec();
			for (int r = 0; r < reps; r++) {
				for (int off = 0; off < frames; off += SYMPHONY_MICRO_BLOCK_SIZE) {
					pkg->graph_output->set_output(buf, off);
					pkg->graph->execute(SYMPHONY_MICRO_BLOCK_SIZE);
				}
			}
			const uint64_t t1 = symphony_time_usec();
			samples.write[i] = t1 >= t0 ? (t1 - t0) : 0;
		}
		uint64_t median = 0;
		uint64_t p99 = 0;
		_percentile_us(samples, median, p99);
		PreparedGraphPackage::destroy(pkg);
		return median;
	};

	GraphDescription osc_desc;
	{
		NodeDesc osc;
		osc.id = 1;
		osc.type_name = "Oscillator";
		osc.params.insert("frequency", 440.0f);
		osc_desc.nodes.push_back(osc);
		NodeDesc out;
		out.id = 2;
		out.type_name = "GraphOutput";
		osc_desc.nodes.push_back(out);
		ConnectionDesc c;
		c.from_node = 1;
		c.from_pin = 0;
		c.to_node = 2;
		c.to_pin = 0;
		osc_desc.connections.push_back(c);
	}

	GraphDescription grain_desc;
	{
		NodeDesc osc;
		osc.id = 1;
		osc.type_name = "Oscillator";
		osc.params.insert("frequency", 220.0f);
		grain_desc.nodes.push_back(osc);
		NodeDesc grain;
		grain.id = 2;
		grain.type_name = "GrainCloud";
		grain.params.insert("density", 50.0f);
		grain.params.insert("grain_size_ms", 100.0f);
		grain.params.insert("pitch_tracking", 1.0f);
		grain.params.insert("capture_seconds", 1.0f);
		grain_desc.nodes.push_back(grain);
		NodeDesc out;
		out.id = 3;
		out.type_name = "GraphOutput";
		grain_desc.nodes.push_back(out);
		ConnectionDesc c0;
		c0.from_node = 1;
		c0.from_pin = 0;
		c0.to_node = 2;
		c0.to_pin = 0;
		grain_desc.connections.push_back(c0);
		ConnectionDesc c1;
		c1.from_node = 2;
		c1.from_pin = 0;
		c1.to_node = 3;
		c1.to_pin = 0;
		grain_desc.connections.push_back(c1);
	}

	float osc_cost = 0.0f;
	float grain_cost = 0.0f;
	const uint64_t osc_us = time_graph_us(osc_desc, osc_cost);
	const uint64_t grain_us = time_graph_us(grain_desc, grain_cost);
	REQUIRE(osc_cost > 0.0f);
	REQUIRE(grain_cost > 0.0f);
	REQUIRE(osc_us > 0);
	REQUIRE(grain_us > 0);

	const double osc_us_per_unit = (double)osc_us / (double)osc_cost;
	const double grain_us_per_unit = (double)grain_us / (double)grain_cost;
	MESSAGE("osc_us=", osc_us, " cost=", osc_cost, " us_per_unit=", osc_us_per_unit);
	MESSAGE("grain_us=", grain_us, " cost=", grain_cost, " us_per_unit=", grain_us_per_unit);

	// Conservative admission: GrainCloud must not be >2× “more expensive per unit”
	// than a cheap oscillator reference (would mean cost units under-count GrainCloud).
	// TSan/ASan instrumentation distorts relative µs/unit; keep the gate for unsanitized builds.
#if defined(TSAN_ENABLED) || defined(ASAN_ENABLED)
	MESSAGE("Skipping GrainCloud µs/unit gate under sanitizer.");
#else
	CHECK(grain_us_per_unit <= osc_us_per_unit * 2.0);
#endif
}

TEST_CASE("[Symphony][Stress] Mix timing median/p99 for 10/30/50-node graphs") {
	BudgetGuard guard;

	// macos arm64 template_release; each trial = 64 samples × (32 × 512 frames) @ 48 kHz.
	// Reported/gated values are the median across 3 independent trials.
	// Calibrated 2026-08-13 (median of five outer runs; p99 = max observed across those runs).
	const MixTimingBaseline cases[] = {
		{ "10-node", &AudioStreamSymphony::build_test_graph_10_nodes, 209, 335 },
		{ "30-node", &AudioStreamSymphony::build_test_graph_30_nodes, 1254, 1747 },
		{ "50-node", &AudioStreamSymphony::build_test_graph_50_nodes, 2420, 2790 },
	};

	AudioFrame buf[512];
	const int frames = 512;
	const int reps_per_sample = 32; // Amortize scheduler noise on short graphs.
	const int warmup = 8;
	const int samples = 64;
	const int trials = 3;
	const bool release_gates = OS::get_singleton()->has_feature("template_release");

	for (const MixTimingBaseline &c : cases) {
		GraphCompiler::CompileResult result = GraphCompiler::compile(c.builder(), 48000.0f);
		REQUIRE(result.success());
		PreparedGraphPackage *pkg = PreparedGraphPackage::create_from_graph(result.graph, result.arena_bytes, result.total_package_bytes);
		REQUIRE(pkg != nullptr);
		REQUIRE(pkg->graph_output != nullptr);

		for (int i = 0; i < warmup; i++) {
			for (int r = 0; r < reps_per_sample; r++) {
				_execute_mix_frames(pkg, buf, frames);
			}
		}

		Vector<uint64_t> trial_medians;
		Vector<uint64_t> trial_p99s;
		trial_medians.resize(trials);
		trial_p99s.resize(trials);

		for (int t = 0; t < trials; t++) {
			Vector<uint64_t> times_us;
			times_us.resize(samples);
			for (int i = 0; i < samples; i++) {
				const uint64_t t0 = symphony_time_usec();
				for (int r = 0; r < reps_per_sample; r++) {
					_execute_mix_frames(pkg, buf, frames);
				}
				const uint64_t t1 = symphony_time_usec();
				times_us.write[i] = t1 >= t0 ? (t1 - t0) : 0;
			}

			uint64_t median = 0;
			uint64_t p99 = 0;
			_percentile_us(times_us, median, p99);
			trial_medians.write[t] = median;
			trial_p99s.write[t] = p99;
		}

		uint64_t median = 0;
		uint64_t p99 = 0;
		uint64_t unused = 0;
		_percentile_us(trial_medians, median, unused);
		_percentile_us(trial_p99s, p99, unused);

		CHECK(median > 0);
		CHECK(p99 >= median);
		CHECK(p99 < 5000ull * (uint64_t)reps_per_sample); // Soft absolute ceiling scaled by reps.

		MESSAGE(String(c.label), " mix ", reps_per_sample, "x512f median_us=", median, " p99_us=", p99,
				" baseline_median_us=", c.median_us, " baseline_p99_us=", c.p99_us,
				" arena_bytes=", (uint64_t)pkg->arena_bytes,
				" cost_units=", pkg->estimated_cost_units,
				release_gates ? " gates=release" : " gates=soft");

		if (release_gates) {
			REQUIRE(c.median_us > 0);
			REQUIRE(c.p99_us > 0);
			const uint64_t median_limit = _median_limit_us(c.median_us);
			const uint64_t p99_limit = _p99_limit_us(c.p99_us);
			MESSAGE(String(c.label), " limits median_us=", median_limit, " p99_us=", p99_limit);
			CHECK(median <= median_limit);
			CHECK(p99 <= p99_limit);
		}

		PreparedGraphPackage::destroy(pkg);
	}
}

#ifdef THREADS_ENABLED
TEST_CASE("[Symphony][Stress] Concurrent mix with swap parameter trigger teardown") {
	SymphonyRealtimeScope::reset_violations();

	Ref<AudioStreamSymphony> stream;
	stream.instantiate();
	stream->set_mix_rate(48000.0f);
	stream->set_graph_description(AudioStreamSymphony::build_test_graph_10_nodes());
	CHECK(stream->duplicate_main_to_lod() >= 1);

	Ref<AudioStreamPlayback> base = stream->instantiate_playback();
	Ref<AudioStreamPlaybackSymphony> playback = base;
	REQUIRE(playback.is_valid());
	playback->start();

	struct MixContext {
		AudioStreamPlaybackSymphony *playback = nullptr;
		std::atomic<bool> running{ true };
		std::atomic<uint32_t> mixes{ 0 };
	} ctx;
	ctx.playback = playback.ptr();

	Thread audio_thread;
	audio_thread.start(
			[](void *p_userdata) {
				MixContext *mix_ctx = static_cast<MixContext *>(p_userdata);
				AudioFrame buf[64];
				while (mix_ctx->running.load(std::memory_order_relaxed)) {
					mix_ctx->playback->mix(buf, 1.0f, 64);
					mix_ctx->mixes.fetch_add(1, std::memory_order_relaxed);
				}
			},
			&ctx);

	SymphonyVoiceManager *mgr = SymphonyVoiceManager::get_singleton();
	for (int i = 0; i < 64; i++) {
		playback->set_parameter(StringName("freq"), 220.0f + (float)i);
		(void)playback->trigger(StringName("gate"), 1.0f);
		if ((i % 8) == 0) {
			CompiledGraph *replacement = stream->compile_graph();
			if (replacement) {
				playback->swap_graph(replacement);
			}
		}
		if ((i % 12) == 0) {
			playback->transition_to_lod((i / 12) % 2);
			if (mgr) {
				mgr->process_deferred_lod();
			}
		}
		if ((i % 16) == 0) {
			GraphPackageRetirement::drain();
		}
		OS::get_singleton()->delay_usec(200);
	}

	playback->stop();
	ctx.running.store(false, std::memory_order_relaxed);
	audio_thread.wait_to_finish();

	AudioFrame tail[64];
	(void)playback->mix(tail, 1.0f, 64);
	GraphPackageRetirement::drain();

	CHECK(ctx.mixes.load(std::memory_order_relaxed) > 0);
	CHECK(SymphonyRealtimeScope::violation_count() == 0);
}
#endif // THREADS_ENABLED

} // namespace TestSymphonyStress
