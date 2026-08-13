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
#include "modules/symphony/stream/audio_stream_playback_symphony.h"
#include "modules/symphony/stream/audio_stream_symphony.h"

#include "core/templates/vector.h"

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

TEST_CASE("[Symphony][Stress] Mix timing median/p99 for 10/30/50-node graphs") {
	BudgetGuard guard;

	struct Case {
		const char *label;
		GraphDescription (*builder)();
	};
	const Case cases[] = {
		{ "10-node", &AudioStreamSymphony::build_test_graph_10_nodes },
		{ "30-node", &AudioStreamSymphony::build_test_graph_30_nodes },
		{ "50-node", &AudioStreamSymphony::build_test_graph_50_nodes },
	};

	AudioFrame buf[512];
	const int frames = 512;
	const int warmup = 8;
	const int samples = 64;

	for (const Case &c : cases) {
		GraphCompiler::CompileResult result = GraphCompiler::compile(c.builder(), 48000.0f);
		REQUIRE(result.success());
		PreparedGraphPackage *pkg = PreparedGraphPackage::create_from_graph(result.graph, result.arena_bytes, result.total_package_bytes);
		REQUIRE(pkg != nullptr);
		REQUIRE(pkg->graph_output != nullptr);

		for (int i = 0; i < warmup; i++) {
			pkg->graph_output->set_output(buf, 0);
			pkg->graph->execute(SYMPHONY_MICRO_BLOCK_SIZE);
			// Drain remaining frames in the callback-sized buffer via micro-blocks.
			for (int off = SYMPHONY_MICRO_BLOCK_SIZE; off < frames; off += SYMPHONY_MICRO_BLOCK_SIZE) {
				pkg->graph_output->set_output(buf, off);
				pkg->graph->execute(SYMPHONY_MICRO_BLOCK_SIZE);
			}
		}

		Vector<uint64_t> times_us;
		times_us.resize(samples);
		for (int i = 0; i < samples; i++) {
			const uint64_t t0 = symphony_time_usec();
			for (int off = 0; off < frames; off += SYMPHONY_MICRO_BLOCK_SIZE) {
				pkg->graph_output->set_output(buf, off);
				pkg->graph->execute(SYMPHONY_MICRO_BLOCK_SIZE);
			}
			const uint64_t t1 = symphony_time_usec();
			times_us.write[i] = t1 >= t0 ? (t1 - t0) : 0;
		}

		uint64_t median = 0;
		uint64_t p99 = 0;
		_percentile_us(times_us, median, p99);
		CHECK(median > 0);
		CHECK(p99 >= median);
		CHECK(p99 < 5000);

		MESSAGE(String(c.label), " mix 512f median_us=", median, " p99_us=", p99,
				" arena_bytes=", (uint64_t)pkg->arena_bytes,
				" cost_units=", pkg->estimated_cost_units);

		PreparedGraphPackage::destroy(pkg);
	}
}

} // namespace TestSymphonyStress
