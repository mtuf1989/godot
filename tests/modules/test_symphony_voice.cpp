/**************************************************************************/
/*  test_symphony_voice.cpp                                               */
/*  Suite: [Symphony][Voice] — voice pool, stealing, RTPC, triggers.      */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_voice)

#include "modules/symphony/core/symphony_trigger.h"
#include "modules/symphony/nodes/io/symphony_trigger_input.h"
#include "modules/symphony/core/symphony_arena_allocator.h"
#include "modules/symphony/runtime/rtpc_engine.h"
#include "modules/symphony/runtime/voice_manager.h"
#include "modules/symphony/runtime/event_dispatcher.h"
#include "modules/symphony/runtime/sound_event.h"
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

} // namespace TestSymphonyVoice
