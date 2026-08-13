/**************************************************************************/
/*  test_symphony_playback.cpp                                            */
/*  Suite: [Symphony][Playback] — stream playback, swaps, retirement.     */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_playback)

#include "modules/symphony/core/shared_pcm_cache.h"
#include "modules/symphony/core/symphony_arena_allocator.h"
#include "modules/symphony/core/symphony_graph_compiler.h"
#include "modules/symphony/core/symphony_graph_description.h"
#include "modules/symphony/core/symphony_graph_package_retirement.h"
#include "modules/symphony/core/symphony_prepared_graph_package.h"
#include "modules/symphony/core/symphony_operator.h"
#include "modules/symphony/core/symphony_realtime_scope.h"
#include "modules/symphony/core/symphony_voice_manager.h"
#include "modules/symphony/stream/audio_stream_symphony.h"
#include "modules/symphony/stream/audio_stream_playback_symphony.h"

#include "core/os/memory.h"

#include <cstring>

namespace TestSymphonyPlayback {

static GraphDescription _make_io_graph() {
	GraphDescription desc;

	NodeDesc gin;
	gin.id = 1;
	gin.type_name = "GraphInput";
	gin.params.insert("parameter_name", "freq");
	gin.params.insert("default_value", 440.0f);
	desc.nodes.push_back(gin);

	NodeDesc tin;
	tin.id = 2;
	tin.type_name = "TriggerInput";
	tin.params.insert("trigger_name", "gate");
	desc.nodes.push_back(tin);

	NodeDesc osc;
	osc.id = 3;
	osc.type_name = "Oscillator";
	osc.params.insert("frequency", 440.0f);
	osc.params.insert("waveform", 0.0f);
	desc.nodes.push_back(osc);

	NodeDesc out;
	out.id = 4;
	out.type_name = "GraphOutput";
	desc.nodes.push_back(out);

	ConnectionDesc c0;
	c0.from_node = 1;
	c0.from_pin = 0;
	c0.to_node = 3;
	c0.to_pin = 0;
	desc.connections.push_back(c0);

	ConnectionDesc c1;
	c1.from_node = 3;
	c1.from_pin = 0;
	c1.to_node = 4;
	c1.to_pin = 0;
	desc.connections.push_back(c1);

	return desc;
}

TEST_CASE("[Symphony][Playback] PreparedGraphPackage builds sorted routes") {
	GraphCompiler::CompileResult result = GraphCompiler::compile(_make_io_graph(), 48000.0f);
	REQUIRE(result.success());
	REQUIRE(result.graph != nullptr);

	PreparedGraphPackage *pkg = PreparedGraphPackage::create_from_graph(result.graph, result.arena_bytes, result.total_package_bytes, 0, result.estimated_cost_units);
	REQUIRE(pkg != nullptr);
	CHECK(pkg->graph_output != nullptr);
	CHECK(pkg->param_routes.size() >= 1);
	CHECK(pkg->find_param(StringName("freq")) != nullptr);
	CHECK(pkg->find_param(StringName("missing")) == nullptr);
	CHECK(pkg->find_trigger(StringName("gate")) != nullptr);
	CHECK(pkg->find_trigger(StringName("missing")) == nullptr);
	CHECK(result.estimated_cost_units > 0.0f);
	CHECK(pkg->estimated_cost_units == doctest::Approx(result.estimated_cost_units));

	PreparedGraphPackage::destroy(pkg);
}

TEST_CASE("[Symphony][Playback] GraphPackageRetirement drain destroys packages") {
	GraphCompiler::CompileResult result = GraphCompiler::compile(_make_io_graph(), 48000.0f);
	REQUIRE(result.success());

	PreparedGraphPackage *pkg = PreparedGraphPackage::create_from_graph(result.graph);
	REQUIRE(pkg != nullptr);

	const uint32_t before = GraphPackageRetirement::get_pending_count();
	GraphPackageRetirement::retire(pkg);
	CHECK(GraphPackageRetirement::get_pending_count() == before + 1);

	GraphPackageRetirement::drain();
	CHECK(GraphPackageRetirement::get_pending_count() == before);
}

TEST_CASE("[Symphony][Playback] Package fingerprints match node id + type") {
	GraphCompiler::CompileResult result = GraphCompiler::compile(_make_io_graph(), 48000.0f);
	REQUIRE(result.success());

	PreparedGraphPackage *pkg = PreparedGraphPackage::create_from_graph(result.graph, result.arena_bytes, result.total_package_bytes);
	REQUIRE(pkg != nullptr);
	REQUIRE(pkg->fingerprints.size() == result.graph->operator_count);

	for (int i = 1; i < pkg->fingerprints.size(); i++) {
		CHECK(pkg->fingerprints[i - 1].node_id <= pkg->fingerprints[i].node_id);
	}

	const PreparedGraphPackage::OperatorFingerprint *osc_fp = pkg->find_fingerprint(3);
	REQUIRE(osc_fp != nullptr);
	CHECK(osc_fp->type_hash == StringName("Oscillator").hash());
	CHECK(osc_fp->structural_hash != 0);
	CHECK(pkg->find_fingerprint(999) == nullptr);

	PreparedGraphPackage::destroy(pkg);
}

TEST_CASE("[Symphony][Playback] migrate_compatible_state copies bounded operator state") {
	GraphCompiler::CompileResult a = GraphCompiler::compile(_make_io_graph(), 48000.0f);
	GraphCompiler::CompileResult b = GraphCompiler::compile(_make_io_graph(), 48000.0f);
	REQUIRE(a.success());
	REQUIRE(b.success());

	PreparedGraphPackage *from = PreparedGraphPackage::create_from_graph(a.graph, a.arena_bytes, a.total_package_bytes);
	PreparedGraphPackage *to = PreparedGraphPackage::create_from_graph(b.graph, b.arena_bytes, b.total_package_bytes);
	REQUIRE(from != nullptr);
	REQUIRE(to != nullptr);

	const PreparedGraphPackage::OperatorFingerprint *osc_fp = from->find_fingerprint(3);
	REQUIRE(osc_fp != nullptr);
	SymphonyOperator *src_osc = from->graph->operators[osc_fp->exec_index];
	SymphonyOperator *dst_osc = to->graph->operators[to->find_fingerprint(3)->exec_index];
	REQUIRE(src_osc != nullptr);
	REQUIRE(dst_osc != nullptr);

	uint8_t seed[16];
	const size_t seed_size = src_osc->export_state(nullptr, 0);
	REQUIRE(seed_size > 0);
	REQUIRE(seed_size <= sizeof(seed));
	// Drive distinct phase via import of a known buffer, then migrate.
	for (size_t i = 0; i < seed_size; i++) {
		seed[i] = (uint8_t)(0xA5 ^ (uint8_t)i);
	}
	src_osc->import_state(seed, seed_size);

	uint8_t before_dst[16] = {};
	dst_osc->export_state(before_dst, sizeof(before_dst));

	PreparedGraphPackage::migrate_compatible_state(from, to);

	uint8_t after_src[16] = {};
	uint8_t after_dst[16] = {};
	CHECK(src_osc->export_state(after_src, sizeof(after_src)) == seed_size);
	CHECK(dst_osc->export_state(after_dst, sizeof(after_dst)) == seed_size);
	CHECK(memcmp(after_src, seed, seed_size) == 0);
	CHECK(memcmp(after_dst, seed, seed_size) == 0);
	CHECK(memcmp(before_dst, after_dst, seed_size) != 0);

	PreparedGraphPackage::destroy(from);
	PreparedGraphPackage::destroy(to);
}

TEST_CASE("[Symphony][Playback] migrate skips mismatched structural fingerprints") {
	GraphDescription desc_a = _make_io_graph();
	GraphDescription desc_b = _make_io_graph();
	// Same node id 3, different operator type → type/structural mismatch.
	for (int i = 0; i < desc_b.nodes.size(); i++) {
		if (desc_b.nodes[i].id == 3) {
			desc_b.nodes.write[i].type_name = "LFO";
			desc_b.nodes.write[i].params.clear();
			desc_b.nodes.write[i].params.insert("frequency", 1.0f);
			desc_b.nodes.write[i].params.insert("waveform", 0.0f);
			break;
		}
	}
	// LFO expects float pin wiring similar enough to compile with GraphInput→LFO→GraphOutput.
	GraphCompiler::CompileResult a = GraphCompiler::compile(desc_a, 48000.0f);
	GraphCompiler::CompileResult b = GraphCompiler::compile(desc_b, 48000.0f);
	REQUIRE(a.success());
	REQUIRE(b.success());

	PreparedGraphPackage *from = PreparedGraphPackage::create_from_graph(a.graph);
	PreparedGraphPackage *to = PreparedGraphPackage::create_from_graph(b.graph);
	REQUIRE(from != nullptr);
	REQUIRE(to != nullptr);

	const auto *from_fp = from->find_fingerprint(3);
	const auto *to_fp = to->find_fingerprint(3);
	REQUIRE(from_fp != nullptr);
	REQUIRE(to_fp != nullptr);
	CHECK(from_fp->type_hash != to_fp->type_hash);

	SymphonyOperator *src = from->graph->operators[from_fp->exec_index];
	SymphonyOperator *dst = to->graph->operators[to_fp->exec_index];
	uint8_t seed[16];
	size_t seed_size = src->export_state(nullptr, 0);
	REQUIRE(seed_size > 0);
	REQUIRE(seed_size <= sizeof(seed));
	for (size_t i = 0; i < seed_size; i++) {
		seed[i] = 0x3C;
	}
	src->import_state(seed, seed_size);

	uint8_t dst_before[16] = {};
	size_t dst_size = dst->export_state(dst_before, sizeof(dst_before));

	PreparedGraphPackage::migrate_compatible_state(from, to);

	uint8_t dst_after[16] = {};
	CHECK(dst->export_state(dst_after, sizeof(dst_after)) == dst_size);
	CHECK(memcmp(dst_before, dst_after, dst_size) == 0);

	PreparedGraphPackage::destroy(from);
	PreparedGraphPackage::destroy(to);
}

TEST_CASE("[Symphony][Playback] set_parameter and trigger target published control package") {
	Ref<AudioStreamSymphony> stream;
	stream.instantiate();
	stream->set_mix_rate(48000.0f);
	stream->set_graph_description(_make_io_graph());

	Ref<AudioStreamPlayback> base = stream->instantiate_playback();
	Ref<AudioStreamPlaybackSymphony> playback = base;
	REQUIRE(playback.is_valid());

	// Pending only until start() publishes control_package.
	CHECK(playback->trigger(StringName("gate"), 1.0f) == false);
	playback->set_parameter(StringName("freq"), 880.0f);

	playback->start();
	CHECK(playback->trigger(StringName("gate"), 1.0f) == true);
	playback->set_parameter(StringName("freq"), 660.0f);
	CHECK(playback->get_estimated_cost_units() > 0.0f);

	AudioFrame buf[64];
	CHECK(playback->mix(buf, 1.0f, 64) == 64);

	// Hot-swap: control stays on current until mix adopts pending.
	CompiledGraph *replacement = stream->compile_graph();
	REQUIRE(replacement != nullptr);
	playback->swap_graph(replacement);
	CHECK(playback->trigger(StringName("gate"), 0.5f) == true);
	playback->set_parameter(StringName("freq"), 220.0f);

	CHECK(playback->mix(buf, 1.0f, 64) == 64);
	CHECK(playback->trigger(StringName("gate"), 1.0f) == true);
	playback->set_parameter(StringName("freq"), 110.0f);
	CHECK(playback->trigger(StringName("missing"), 1.0f) == false);

	playback->stop();
	GraphPackageRetirement::drain();
}

TEST_CASE("[Symphony][Playback] RT-scope mix and execute report no violations") {
	SymphonyRealtimeScope::reset_violations();

	GraphCompiler::CompileResult result = GraphCompiler::compile(_make_io_graph(), 48000.0f);
	REQUIRE(result.success());
	PreparedGraphPackage *pkg = PreparedGraphPackage::create_from_graph(result.graph, result.arena_bytes, result.total_package_bytes);
	REQUIRE(pkg != nullptr);
	REQUIRE(pkg->graph != nullptr);

	AudioFrame exec_buf[SYMPHONY_MICRO_BLOCK_SIZE];
	pkg->graph_output->set_output(exec_buf, 0);
	pkg->graph->execute(SYMPHONY_MICRO_BLOCK_SIZE);
	CHECK(SymphonyRealtimeScope::violation_count() == 0);

	Ref<AudioStreamSymphony> stream;
	stream.instantiate();
	stream->set_mix_rate(48000.0f);
	stream->set_graph_description(_make_io_graph());
	Ref<AudioStreamPlayback> base = stream->instantiate_playback();
	Ref<AudioStreamPlaybackSymphony> playback = base;
	REQUIRE(playback.is_valid());
	playback->start();

	AudioFrame mix_buf[64];
	CHECK(playback->mix(mix_buf, 1.0f, 64) == 64);

	CompiledGraph *replacement = stream->compile_graph();
	REQUIRE(replacement != nullptr);
	playback->swap_graph(replacement);
	CHECK(playback->mix(mix_buf, 1.0f, 64) == 64);

	playback->stop();
	CHECK(playback->mix(mix_buf, 1.0f, 64) == 0);
	GraphPackageRetirement::drain();
	PreparedGraphPackage::destroy(pkg);

	CHECK(SymphonyRealtimeScope::violation_count() == 0);
}

TEST_CASE("[Symphony][Playback] RT-scope flags compile alloc free mutex ObjectDB container") {
	SymphonyRealtimeAssertSuppressor suppress;
	SymphonyRealtimeScope::reset_violations();
	GraphPackageRetirement::drain();

	CompiledGraph *compiled_in_scope = nullptr;
	{
		SymphonyRealtimeScope rt_scope;
		GraphCompiler::CompileResult ignored = GraphCompiler::compile(_make_io_graph(), 48000.0f);
		compiled_in_scope = ignored.graph;
	}
	CHECK(SymphonyRealtimeScope::violation_count(SymphonyRTViolation::Compile) >= 1);
	if (compiled_in_scope) {
		memdelete(compiled_in_scope);
	}

	SymphonyRealtimeScope::reset_violations();
	{
		SymphonyRealtimeScope rt_scope;
		ArenaAllocator arena;
		CHECK(arena.init(1024));
		arena.free();
	}
	CHECK(SymphonyRealtimeScope::violation_count(SymphonyRTViolation::Alloc) >= 1);
	CHECK(SymphonyRealtimeScope::violation_count(SymphonyRTViolation::Free) >= 1);

	SymphonyRealtimeScope::reset_violations();
	{
		SymphonyRealtimeScope rt_scope;
		GraphPackageRetirement::drain();
	}
	CHECK(SymphonyRealtimeScope::violation_count(SymphonyRTViolation::Free) >= 1);

	REQUIRE(SharedPCMCache::get_singleton() != nullptr);
	SymphonyRealtimeScope::reset_violations();
	{
		SymphonyRealtimeScope rt_scope;
		(void)SharedPCMCache::get_singleton()->get_entry_count();
	}
	CHECK(SymphonyRealtimeScope::violation_count(SymphonyRTViolation::Mutex) >= 1);

	Ref<AudioStreamSymphony> stream;
	stream.instantiate();
	stream->set_mix_rate(48000.0f);
	stream->set_graph_description(_make_io_graph());
	Ref<AudioStreamPlayback> base = stream->instantiate_playback();
	Ref<AudioStreamPlaybackSymphony> playback = base;
	REQUIRE(playback.is_valid());

	SymphonyRealtimeScope::reset_violations();
	{
		SymphonyRealtimeScope rt_scope;
		playback->process_manager_requests();
	}
	CHECK(SymphonyRealtimeScope::violation_count(SymphonyRTViolation::ObjectDB) >= 1);

	SymphonyVoiceManager *mgr = SymphonyVoiceManager::get_singleton();
	REQUIRE(mgr != nullptr);
	SymphonyRealtimeScope::reset_violations();
	{
		SymphonyRealtimeScope rt_scope;
		(void)mgr->get_debug_metrics();
	}
	CHECK(SymphonyRealtimeScope::violation_count(SymphonyRTViolation::ContainerMutation) >= 1);
	CHECK(mgr->get_rt_violation_count() >= 1);
}

} // namespace TestSymphonyPlayback
