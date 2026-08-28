/**************************************************************************/
/*  test_symphony_propagation.cpp                                         */
/*  Suite: [Symphony][Spatial][Propagation] — propagation delay (Task 11).*/
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_propagation)

#include "modules/symphony/runtime/sound_event.h"
#include "modules/symphony/runtime/voice_manager.h"
#include "modules/symphony/runtime/event_dispatcher.h"
#include "scene/resources/audio/audio_stream_wav.h"

namespace TestSymphonyPropagation {

static Ref<SoundEvent> make_event(bool p_enable, bool p_loop, float p_speed = 343.0f) {
	Ref<SoundEvent> event;
	event.instantiate();
	event->set_enable_propagation_delay(p_enable);
	event->set_loop(p_loop);
	event->set_speed_of_sound(p_speed);
	Ref<AudioStreamWAV> stream;
	stream.instantiate();
	TypedArray<AudioStream> streams;
	streams.push_back(stream);
	event->set_streams(streams);
	return event;
}

// --- SoundEvent::compute_propagation_delay ------------------------------

TEST_CASE("[Symphony][Spatial][Propagation] Delay matches distance / speed_of_sound") {
	Ref<SoundEvent> event = make_event(true, false, 343.0f);
	// 343 m at 343 m/s → exactly 1.0 s.
	CHECK(event->compute_propagation_delay(343.0f) == doctest::Approx(1.0f).epsilon(0.001f));
	// 686 m → 2.0 s.
	CHECK(event->compute_propagation_delay(686.0f) == doctest::Approx(2.0f).epsilon(0.001f));
}

TEST_CASE("[Symphony][Spatial][Propagation] Sub-threshold distances start immediately") {
	Ref<SoundEvent> event = make_event(true, false, 343.0f);
	// 343 m/s * 0.01 s = 3.43 m threshold. Anything closer → 0.
	CHECK(event->compute_propagation_delay(1.0f) == doctest::Approx(0.0f));
	CHECK(event->compute_propagation_delay(3.0f) == doctest::Approx(0.0f));
	// Just above threshold → non-zero.
	CHECK(event->compute_propagation_delay(4.0f) > 0.0f);
}

TEST_CASE("[Symphony][Spatial][Propagation] Disabled setting yields zero delay") {
	Ref<SoundEvent> event = make_event(false, false, 343.0f);
	CHECK(event->compute_propagation_delay(1000.0f) == doctest::Approx(0.0f));
}

TEST_CASE("[Symphony][Spatial][Propagation] Looping events ignore the setting") {
	Ref<SoundEvent> event = make_event(true, true, 343.0f);
	CHECK(event->compute_propagation_delay(1000.0f) == doctest::Approx(0.0f));
}

TEST_CASE("[Symphony][Spatial][Propagation] Non-positive speed or distance yields zero") {
	Ref<SoundEvent> zero_speed = make_event(true, false, 0.0f);
	CHECK(zero_speed->compute_propagation_delay(1000.0f) == doctest::Approx(0.0f));

	Ref<SoundEvent> ok = make_event(true, false, 343.0f);
	CHECK(ok->compute_propagation_delay(0.0f) == doctest::Approx(0.0f));
	CHECK(ok->compute_propagation_delay(-50.0f) == doctest::Approx(0.0f));
}

// --- VoicePool deferred start ------------------------------------------

TEST_CASE("[Symphony][Spatial][Propagation] Voice held in TO_PLAY until delay elapses") {
	SymphonyVoicePool *pool = SymphonyVoicePool::get_singleton();
	REQUIRE(pool != nullptr);

	// Clean pool.
	for (int i = 0; i < pool->get_pool_size(); i++) {
		pool->release_slot(i, true);
	}

	int slot = pool->acquire_slot(50);
	REQUIRE(slot >= 0);
	CHECK(pool->get_slot_state(slot) == SymphonyVoicePool::VOICE_TO_PLAY);

	pool->set_slot_start_delay(slot, 0.5f);
	CHECK(pool->is_slot_start_pending(slot));
	CHECK(pool->get_slot_start_delay(slot) == doctest::Approx(0.5f));

	// Advance 0.2 s — still pending, still TO_PLAY.
	pool->tick_deferred_starts(0.2f);
	CHECK(pool->get_slot_start_delay(slot) == doctest::Approx(0.3f).epsilon(0.001f));
	CHECK(pool->is_slot_start_pending(slot));
	CHECK(pool->get_slot_state(slot) == SymphonyVoicePool::VOICE_TO_PLAY);

	// Advance past the remaining delay — countdown hits 0.
	pool->tick_deferred_starts(0.4f);
	CHECK(pool->get_slot_start_delay(slot) == doctest::Approx(0.0f));
	CHECK(pool->is_slot_start_pending(slot) == false);
	// Still TO_PLAY until process_frame() runs the transition.
	CHECK(pool->get_slot_state(slot) == SymphonyVoicePool::VOICE_TO_PLAY);

	// process_frame() now flips it to PLAYING.
	pool->process_frame();
	CHECK(pool->get_slot_state(slot) == SymphonyVoicePool::VOICE_PLAYING);

	pool->release_slot(slot, true);
}

TEST_CASE("[Symphony][Spatial][Propagation] Zero delay starts on the next frame") {
	SymphonyVoicePool *pool = SymphonyVoicePool::get_singleton();
	REQUIRE(pool != nullptr);
	for (int i = 0; i < pool->get_pool_size(); i++) {
		pool->release_slot(i, true);
	}

	int slot = pool->acquire_slot(50);
	REQUIRE(slot >= 0);
	pool->set_slot_start_delay(slot, 0.0f);
	CHECK(pool->is_slot_start_pending(slot) == false);

	pool->process_frame();
	CHECK(pool->get_slot_state(slot) == SymphonyVoicePool::VOICE_PLAYING);

	pool->release_slot(slot, true);
}

TEST_CASE("[Symphony][Spatial][Propagation] Releasing a pending slot does not leak") {
	SymphonyVoicePool *pool = SymphonyVoicePool::get_singleton();
	REQUIRE(pool != nullptr);
	for (int i = 0; i < pool->get_pool_size(); i++) {
		pool->release_slot(i, true);
	}

	int slot = pool->acquire_slot(50);
	REQUIRE(slot >= 0);
	pool->set_slot_start_delay(slot, 5.0f);
	CHECK(pool->is_slot_start_pending(slot));

	// Cancel the pending play by releasing immediately.
	pool->release_slot(slot, true);
	CHECK(pool->get_slot_state(slot) == SymphonyVoicePool::VOICE_FREE);
	CHECK(pool->get_slot_start_delay(slot) == doctest::Approx(0.0f));

	// The freed slot is re-acquirable and carries no stale delay.
	int reacquired = pool->acquire_slot(50);
	REQUIRE(reacquired >= 0);
	CHECK(pool->get_slot_start_delay(reacquired) == doctest::Approx(0.0f));
	CHECK(pool->is_slot_start_pending(reacquired) == false);

	pool->release_slot(reacquired, true);
}

// --- Dispatcher integration --------------------------------------------

TEST_CASE("[Symphony][Spatial][Propagation] play_event sets delay for distant 3D one-shot") {
	SymphonyVoicePool *pool = SymphonyVoicePool::get_singleton();
	SymphonyEventDispatcher *dispatcher = SymphonyEventDispatcher::get_singleton();
	REQUIRE(pool != nullptr);
	REQUIRE(dispatcher != nullptr);
	for (int i = 0; i < pool->get_pool_size(); i++) {
		pool->release_slot(i, true);
	}

	pool->set_listener_position(Vector3(0, 0, 0));

	Ref<SoundEvent> event = make_event(true, false, 343.0f);
	// Source 343 m away → 1.0 s expected delay.
	Dictionary result = dispatcher->play_event(event, Vector3(343.0f, 0, 0), true);

	int slot = (int)result["slot"];
	REQUIRE(slot >= 0);
	float delay_s = (float)result["delay_s"];
	CHECK(delay_s == doctest::Approx(1.0f).epsilon(0.01f));
	CHECK(pool->get_slot_start_delay(slot) == doctest::Approx(1.0f).epsilon(0.01f));
	CHECK(pool->is_slot_start_pending(slot));

	pool->release_slot(slot, true);
	dispatcher->on_voice_stopped(event->get_instance_id());
}

TEST_CASE("[Symphony][Spatial][Propagation] play_event without position yields no delay") {
	SymphonyVoicePool *pool = SymphonyVoicePool::get_singleton();
	SymphonyEventDispatcher *dispatcher = SymphonyEventDispatcher::get_singleton();
	REQUIRE(pool != nullptr);
	REQUIRE(dispatcher != nullptr);
	for (int i = 0; i < pool->get_pool_size(); i++) {
		pool->release_slot(i, true);
	}

	Ref<SoundEvent> event = make_event(true, false, 343.0f);
	// No position argument → non-positional path, no delay even if enabled.
	Dictionary result = dispatcher->play_event(event);
	int slot = (int)result["slot"];
	REQUIRE(slot >= 0);
	CHECK((float)result["delay_s"] == doctest::Approx(0.0f));
	CHECK(pool->is_slot_start_pending(slot) == false);

	pool->release_slot(slot, true);
	dispatcher->on_voice_stopped(event->get_instance_id());
}

TEST_CASE("[Symphony][Spatial][Propagation] play_event with loop ignores delay") {
	SymphonyVoicePool *pool = SymphonyVoicePool::get_singleton();
	SymphonyEventDispatcher *dispatcher = SymphonyEventDispatcher::get_singleton();
	REQUIRE(pool != nullptr);
	REQUIRE(dispatcher != nullptr);
	for (int i = 0; i < pool->get_pool_size(); i++) {
		pool->release_slot(i, true);
	}
	pool->set_listener_position(Vector3(0, 0, 0));

	Ref<SoundEvent> event = make_event(true, /*loop=*/true, 343.0f);
	Dictionary result = dispatcher->play_event(event, Vector3(1000.0f, 0, 0), true);
	int slot = (int)result["slot"];
	REQUIRE(slot >= 0);
	CHECK((float)result["delay_s"] == doctest::Approx(0.0f));
	CHECK(pool->is_slot_start_pending(slot) == false);

	pool->release_slot(slot, true);
	dispatcher->on_voice_stopped(event->get_instance_id());
}

TEST_CASE("[Symphony][Spatial][Propagation] Re-trigger within cooldown does not stack delay") {
	SymphonyVoicePool *pool = SymphonyVoicePool::get_singleton();
	SymphonyEventDispatcher *dispatcher = SymphonyEventDispatcher::get_singleton();
	REQUIRE(pool != nullptr);
	REQUIRE(dispatcher != nullptr);
	for (int i = 0; i < pool->get_pool_size(); i++) {
		pool->release_slot(i, true);
	}
	pool->set_listener_position(Vector3(0, 0, 0));

	Ref<SoundEvent> event = make_event(true, false, 343.0f);
	event->set_cooldown_ms(10000.0f); // Long cooldown so re-trigger is rejected.

	Dictionary first = dispatcher->play_event(event, Vector3(686.0f, 0, 0), true);
	int slot = (int)first["slot"];
	REQUIRE(slot >= 0);
	CHECK((float)first["delay_s"] == doctest::Approx(2.0f).epsilon(0.01f));
	float delay_after_first = pool->get_slot_start_delay(slot);

	// Immediate re-trigger — rejected by cooldown, must NOT touch the slot's delay.
	Dictionary second = dispatcher->play_event(event, Vector3(686.0f, 0, 0), true);
	CHECK((int)second["slot"] == -1);
	CHECK((int)second["result"] == (int)SymphonyEventDispatcher::RESULT_REJECTED_COOLDOWN);
	CHECK(pool->get_slot_start_delay(slot) == doctest::Approx(delay_after_first));

	pool->release_slot(slot, true);
	dispatcher->on_voice_stopped(event->get_instance_id());
}

} // namespace TestSymphonyPropagation
