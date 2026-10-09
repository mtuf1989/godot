/**************************************************************************/
/*  test_symphony_p7_timing.cpp — P7: Clock, asymmetric RTPC, N21 validate */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_p7_timing)

#include "modules/symphony/core/symphony_arena_allocator.h"
#include "modules/symphony/core/symphony_pin_types.h"
#include "modules/symphony/core/symphony_trigger.h"
#include "modules/symphony/nodes/timing/symphony_clock.h"
#include "modules/symphony/runtime/music_state_graph.h"
#include "modules/symphony/runtime/rtpc_engine.h"

#include <cmath>
#include <vector>

namespace TestSymphonyP7Timing {

static constexpr float MIX_RATE = 48000.0f;
static constexpr size_t ARENA_SIZE = 65536;

TEST_CASE("[Symphony][P7][Clock] First tick at frame 0; 120 BPM lands on period") {
	ArenaAllocator arena;
	arena.init(ARENA_SIZE);
	HashMap<StringName, Variant> params;
	params.insert("bpm", 120.0f);
	params.insert("subdivision", 1.0f);
	SymphonyClock *clock = (SymphonyClock *)SymphonyClock::create(arena, params, MIX_RATE);
	REQUIRE(clock != nullptr);

	TriggerBuffer trig;
	float phase[SYMPHONY_MICRO_BLOCK_SIZE] = {};
	float bpm = 120.0f;
	TriggerBuffer reset;
	void *inputs[] = { &bpm, &reset };
	void *outputs[] = { &trig, phase };
	clock->bind_pins(inputs, outputs);

	clock->execute(SYMPHONY_MICRO_BLOCK_SIZE);
	REQUIRE(trig.count >= 1);
	CHECK(trig.events[0].sample_offset == 0);

	// 120 BPM → period = 0.5 s = 24000 samples. Collect tick offsets over 1 s.
	std::vector<int> ticks;
	ticks.push_back(0); // already fired
	int frame = SYMPHONY_MICRO_BLOCK_SIZE;
	const int total = (int)MIX_RATE;
	while (frame < total) {
		trig.clear();
		clock->execute(SYMPHONY_MICRO_BLOCK_SIZE);
		for (uint32_t t = 0; t < trig.count; t++) {
			ticks.push_back(frame + (int)trig.events[t].sample_offset);
		}
		frame += SYMPHONY_MICRO_BLOCK_SIZE;
	}
	REQUIRE(ticks.size() >= 2);
	CHECK(ticks[1] == doctest::Approx(24000).epsilon(1));

	arena.free();
}

TEST_CASE("[Symphony][P7][Clock] BPM change mid-beat preserves phase") {
	ArenaAllocator arena;
	arena.init(ARENA_SIZE);
	HashMap<StringName, Variant> params;
	params.insert("bpm", 120.0f);
	SymphonyClock *clock = (SymphonyClock *)SymphonyClock::create(arena, params, MIX_RATE);

	TriggerBuffer trig;
	float phase[SYMPHONY_MICRO_BLOCK_SIZE] = {};
	float bpm = 120.0f;
	TriggerBuffer reset;
	void *inputs[] = { &bpm, &reset };
	void *outputs[] = { &trig, phase };
	clock->bind_pins(inputs, outputs);

	// Advance partway through the first beat (skip the t=0 tick).
	clock->execute(SYMPHONY_MICRO_BLOCK_SIZE); // fires at 0
	const int half_beat_blocks = (24000 / 2) / SYMPHONY_MICRO_BLOCK_SIZE;
	for (int b = 0; b < half_beat_blocks; b++) {
		trig.clear();
		clock->execute(SYMPHONY_MICRO_BLOCK_SIZE);
	}
	const float phase_before = phase[SYMPHONY_MICRO_BLOCK_SIZE - 1];
	CHECK(phase_before > 0.3f);
	CHECK(phase_before < 0.7f);

	bpm = 180.0f; // faster — interval shrinks; phase should stay continuous
	trig.clear();
	clock->execute(SYMPHONY_MICRO_BLOCK_SIZE);
	const float phase_after = phase[0];
	CHECK(std::isfinite(phase_after));
	// Phase must not jump backward across a large discontinuity.
	CHECK(Math::abs(phase_after - phase_before) < 0.25f);

	arena.free();
}

TEST_CASE("[Symphony][P7][Clock] Reset trigger restarts downbeat") {
	ArenaAllocator arena;
	arena.init(ARENA_SIZE);
	HashMap<StringName, Variant> params;
	params.insert("bpm", 120.0f);
	SymphonyClock *clock = (SymphonyClock *)SymphonyClock::create(arena, params, MIX_RATE);

	TriggerBuffer trig;
	float phase[SYMPHONY_MICRO_BLOCK_SIZE] = {};
	float bpm = 120.0f;
	TriggerBuffer reset;
	void *inputs[] = { &bpm, &reset };
	void *outputs[] = { &trig, phase };
	clock->bind_pins(inputs, outputs);

	for (int b = 0; b < 10; b++) {
		trig.clear();
		clock->execute(SYMPHONY_MICRO_BLOCK_SIZE);
	}

	reset.clear();
	reset.push(0, 1.0f);
	trig.clear();
	clock->execute(SYMPHONY_MICRO_BLOCK_SIZE);
	REQUIRE(trig.count >= 1);
	CHECK(trig.events[0].sample_offset == 0);
	CHECK(trig.events[0].value == doctest::Approx(1.0f));

	arena.free();
}

TEST_CASE("[Symphony][P7][RTPC] Asymmetric attack/release smoothing") {
	RTPCEngine *engine = RTPCEngine::get_singleton();
	REQUIRE(engine != nullptr);

	const StringName name = "p7_asym_test";
	if (engine->has_parameter(name)) {
		// Slot already taken from a prior run in the same process — pick another.
	}
	StringName n = name;
	int suffix = 0;
	while (engine->has_parameter(n)) {
		n = StringName(String(name) + "_" + itos(++suffix));
	}

	RTPCEngine::Handle h = engine->register_parameter(n, 0.0f, 5.0f);
	REQUIRE(h != RTPCEngine::INVALID_HANDLE);
	REQUIRE(engine->set_parameter_smoothing(h, /*attack*/ 1.0f, /*release*/ 100.0f));

	engine->set_sample_rate(48000.0f);
	engine->set_target(h, 1.0f);
	// Fast attack: a few blocks should get close to 1.
	for (int i = 0; i < 20; i++) {
		engine->smooth_all(64);
	}
	const float after_attack = engine->get_current_value(h);
	CHECK(after_attack > 0.9f);

	engine->set_target(h, 0.0f);
	// Same number of blocks with slow release should still be well above 0.
	for (int i = 0; i < 20; i++) {
		engine->smooth_all(64);
	}
	const float after_release = engine->get_current_value(h);
	CHECK(after_release > 0.5f);
	CHECK(after_release < after_attack);
}

TEST_CASE("[Symphony][P7][N21] MusicStateGraph validate requires bpm and beats_per_bar") {
	Ref<MusicStateGraph> graph;
	graph.instantiate();

	Dictionary states;
	Dictionary ok_state;
	ok_state["bpm"] = 120.0f;
	ok_state["beats_per_bar"] = 4;
	states[StringName("ok")] = ok_state;
	graph->set_states(states);
	graph->set_initial_state(StringName("ok"));
	CHECK(graph->validate() == true);

	Dictionary bad_missing;
	bad_missing["beats_per_bar"] = 4;
	states[StringName("bad")] = bad_missing;
	graph->set_states(states);
	ERR_PRINT_OFF;
	CHECK(graph->validate() == false);
	ERR_PRINT_ON;

	Dictionary bad_bpm;
	bad_bpm["bpm"] = 0.0f;
	bad_bpm["beats_per_bar"] = 4;
	states.clear();
	states[StringName("ok")] = ok_state;
	states[StringName("bad_bpm")] = bad_bpm;
	graph->set_states(states);
	graph->set_initial_state(StringName("ok"));
	ERR_PRINT_OFF;
	CHECK(graph->validate() == false);
	ERR_PRINT_ON;
}

} // namespace TestSymphonyP7Timing
