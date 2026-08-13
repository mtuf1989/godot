/**************************************************************************/
/*  test_symphony_voice.cpp                                               */
/*  Suite: [Symphony][Voice] — voice pool, stealing, RTPC, triggers.      */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_voice)

#include "modules/symphony/core/symphony_trigger.h"
#include "modules/symphony/nodes/io/symphony_trigger_input.h"
#include "modules/symphony/core/symphony_arena_allocator.h"
#include "modules/symphony/core/symphony_graph_package_retirement.h"
#include "modules/symphony/core/symphony_prepared_graph_package.h"
#include "modules/symphony/core/symphony_graph_compiler.h"
#include "modules/symphony/core/symphony_graph_description.h"
#include "modules/symphony/core/symphony_runtime_metrics.h"
#include "modules/symphony/runtime/rtpc_engine.h"
#include "modules/symphony/runtime/voice_manager.h"
#include "modules/symphony/runtime/event_dispatcher.h"
#include "modules/symphony/runtime/sound_event.h"
#include "modules/symphony/core/symphony_voice_manager.h"
#include "scene/resources/audio/audio_stream_wav.h"

namespace TestSymphonyVoice {

TEST_CASE("[Symphony][Voice] TriggerInput SPSC queue drops when full") {
	ArenaAllocator arena;
	REQUIRE(arena.init(4096));

	HashMap<StringName, Variant> params;
	SymphonyOperator *op = SymphonyTriggerInput::create(arena, params, 48000.0f);
	REQUIRE(op != nullptr);
	SymphonyTriggerInput *tin = static_cast<SymphonyTriggerInput *>(op);

	TriggerBuffer out;
	void *outs[1] = { &out };
	tin->bind_pins(nullptr, outs);

	const uint64_t before = symphony_dropped_trigger_count().load();

	int accepted = 0;
	for (int i = 0; i < 80; i++) {
		if (tin->fire(1.0f)) {
			accepted++;
		}
	}
	CHECK(accepted == 64);
	CHECK(symphony_dropped_trigger_count().load() == before + 16);

	tin->execute(SYMPHONY_MICRO_BLOCK_SIZE);
	CHECK(out.count == SYMPHONY_MAX_TRIGGERS_PER_BLOCK);
}

TEST_CASE("[Symphony][Voice] RTPCEngine register returns handle; no auto-create") {
	RTPCEngine *engine = RTPCEngine::get_singleton();
	REQUIRE(engine != nullptr);

	const uint64_t missing_before = engine->get_missing_handle_count();
	CHECK(engine->set_parameter_target("rtpc_test_unregistered", 1.0f) == false);
	CHECK(engine->get_missing_handle_count() == missing_before + 1);

	RTPCEngine::Handle h = engine->register_global_parameter("rtpc_test_param", 0.25f, 5.0f);
	REQUIRE(h != RTPCEngine::INVALID_HANDLE);
	CHECK(engine->set_target(h, 0.75f) == true);
	CHECK(engine->get_target_value(h) == doctest::Approx(0.75f));
	CHECK(engine->set_parameter_target("rtpc_test_param", 0.5f) == true);
}

TEST_CASE("[Symphony][Voice] acquire_slot is free-only; dispatcher steals at event cap") {
	SymphonyVoicePool *pool = SymphonyVoicePool::get_singleton();
	SymphonyEventDispatcher *dispatcher = SymphonyEventDispatcher::get_singleton();
	REQUIRE(pool != nullptr);
	REQUIRE(dispatcher != nullptr);

	// Fill with free acquires — no steal inside acquire_slot.
	for (int i = 0; i < pool->get_pool_size(); i++) {
		CHECK(pool->acquire_slot(50) >= 0);
	}
	CHECK(pool->acquire_slot(50) == -1);

	// Free all for clean dispatcher test.
	for (int i = 0; i < pool->get_pool_size(); i++) {
		pool->release_slot(i, true);
	}

	Ref<SoundEvent> event;
	event.instantiate();
	event->set_max_voices(1);
	event->set_steal_mode(SoundEvent::STEAL_OLDEST);
	event->set_priority(40);

	Ref<AudioStreamWAV> stream;
	stream.instantiate();
	TypedArray<AudioStream> streams;
	streams.push_back(stream);
	event->set_streams(streams);

	SymphonyEventDispatcher::PlayResult pr;
	StringName reason;
	int slot0 = dispatcher->dispatch(event, pr, reason);
	REQUIRE(slot0 >= 0);
	CHECK(pr == SymphonyEventDispatcher::RESULT_PLAYED);

	int slot1 = dispatcher->dispatch(event, pr, reason);
	REQUIRE(slot1 >= 0);
	CHECK(slot1 == slot0);
	CHECK(pr == SymphonyEventDispatcher::RESULT_STOLEN);
	CHECK(reason == StringName("oldest"));

	pool->release_slot(slot1, true);
	dispatcher->on_voice_stopped(event->get_instance_id());
}

TEST_CASE("[Symphony][Voice] play_event validates stream before steal") {
	SymphonyVoicePool *pool = SymphonyVoicePool::get_singleton();
	SymphonyEventDispatcher *dispatcher = SymphonyEventDispatcher::get_singleton();
	REQUIRE(pool != nullptr);
	REQUIRE(dispatcher != nullptr);

	for (int i = 0; i < pool->get_pool_size(); i++) {
		REQUIRE(pool->acquire_slot(10) >= 0);
	}

	Ref<SoundEvent> event;
	event.instantiate();
	event->set_priority(99);
	event->set_max_voices(0);
	TypedArray<AudioStream> streams;
	streams.push_back(Variant()); // nil stream — must reject without stealing
	event->set_streams(streams);

	const int active_before = pool->get_active_voice_count();
	Dictionary result = dispatcher->play_event(event);
	CHECK((int)result["result"] == (int)SymphonyEventDispatcher::RESULT_REJECTED_NO_STREAMS);
	CHECK((int)result["slot"] == -1);
	CHECK(pool->get_active_voice_count() == active_before);

	for (int i = 0; i < pool->get_pool_size(); i++) {
		pool->release_slot(i, true);
	}
}

TEST_CASE("[Symphony][Voice] transition cost estimate scales with units") {
	SymphonyVoiceManager *mgr = SymphonyVoiceManager::get_singleton();
	REQUIRE(mgr != nullptr);

	const float light = mgr->estimate_cpu_fraction_for_cost(64.0f, 48000.0f, 512);
	const float heavy = mgr->estimate_cpu_fraction_for_cost(64.0f * 48.0f, 48000.0f, 512);
	CHECK(light > 0.0f);
	CHECK(heavy > light);
}

TEST_CASE("[Symphony][Voice] get_debug_metrics exposes transition trigger retirement memory") {
	SymphonyVoiceManager *mgr = SymphonyVoiceManager::get_singleton();
	REQUIRE(mgr != nullptr);

	const uint64_t destroyed_before = mgr->get_packages_destroyed_count();
	const uint64_t crossfade_before = mgr->get_crossfade_transition_count();
	const uint64_t fallback_before = mgr->get_fallback_transition_count();
	const uint64_t underflow_before = mgr->get_spectral_underflow_count();

	mgr->note_crossfade_transition();
	mgr->note_fallback_transition();
	symphony_note_spectral_underflow();

	GraphDescription desc;
	NodeDesc gin;
	gin.id = 1;
	gin.type_name = "GraphInput";
	gin.params.insert("parameter_name", "x");
	gin.params.insert("default_value", 0.0f);
	desc.nodes.push_back(gin);
	NodeDesc out;
	out.id = 2;
	out.type_name = "GraphOutput";
	desc.nodes.push_back(out);
	ConnectionDesc c;
	c.from_node = 1;
	c.from_pin = 0;
	c.to_node = 2;
	c.to_pin = 0;
	desc.connections.push_back(c);

	GraphCompiler::CompileResult compiled = GraphCompiler::compile(desc, 48000.0f);
	REQUIRE(compiled.success());
	PreparedGraphPackage *pkg = PreparedGraphPackage::create_from_graph(compiled.graph, compiled.arena_bytes, compiled.total_package_bytes);
	REQUIRE(pkg != nullptr);
	GraphPackageRetirement::retire(pkg);
	CHECK(GraphPackageRetirement::get_pending_count() >= 1);
	GraphPackageRetirement::drain();

	CHECK(mgr->get_crossfade_transition_count() == crossfade_before + 1);
	CHECK(mgr->get_fallback_transition_count() == fallback_before + 1);
	CHECK(mgr->get_spectral_underflow_count() == underflow_before + 1);
	CHECK(mgr->get_packages_destroyed_count() == destroyed_before + 1);

	Dictionary metrics = mgr->get_debug_metrics();
	CHECK(metrics.has("dropped_trigger_count"));
	CHECK(metrics.has("spectral_underflow_count"));
	CHECK(metrics.has("crossfade_transition_count"));
	CHECK(metrics.has("fallback_transition_count"));
	CHECK(metrics.has("retirement_pending"));
	CHECK(metrics.has("packages_destroyed"));
	CHECK(metrics.has("memory_global_used_bytes"));
	CHECK(metrics.has("packages_active"));
	CHECK((int64_t)metrics["packages_destroyed"] == (int64_t)mgr->get_packages_destroyed_count());
}

} // namespace TestSymphonyVoice
