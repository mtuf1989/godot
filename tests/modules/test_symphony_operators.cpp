/**************************************************************************/
/*  test_symphony_operators.cpp                                           */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_operators)

#include "modules/symphony/core/symphony_arena_allocator.h"
#include "modules/symphony/core/symphony_operator_registry.h"
#include "modules/symphony/core/symphony_pin_types.h"
#include "modules/symphony/core/symphony_trigger.h"
#include "modules/symphony/nodes/delay/symphony_delay_line.h"
#include "modules/symphony/nodes/delay/symphony_feedback_path.h"
#include "modules/symphony/nodes/filters/symphony_sv_filter.h"
#include "modules/symphony/nodes/generators/symphony_oscillator.h"
#include "modules/symphony/nodes/timing/symphony_stochastic_trigger.h"
#include "modules/symphony/nodes/utility/symphony_parameter_smoother.h"

#include <cmath>
#include <cstring>

// Helper: create an operator from the arena with given params.
#define ARENA_SIZE 65536
#define MIX_RATE 48000.0f

namespace TestSymphonyOperators {

// --- S1.3: PolyBLEP Oscillator Tests ---

TEST_CASE("[Symphony][Operators][Oscillator] Sine produces valid output") {
	ArenaAllocator arena;
	arena.init(ARENA_SIZE);

	HashMap<StringName, Variant> params;
	params.insert("frequency", 440.0f);
	params.insert("waveform", 0.0f); // Sine

	SymphonyOscillator *osc = (SymphonyOscillator *)SymphonyOscillator::create(arena, params, MIX_RATE);

	float out_buf[SYMPHONY_MICRO_BLOCK_SIZE] = {};
	// Descriptor pins: frequency (AUDIO), pulse_width (FLOAT) — both optional.
	void *inputs[] = { nullptr, nullptr };
	void *outputs[] = { out_buf };
	osc->bind_pins(inputs, outputs);
	osc->execute(SYMPHONY_MICRO_BLOCK_SIZE);

	// Sine output should be in [-1, 1]
	for (int i = 0; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
		CHECK(out_buf[i] >= -1.0f);
		CHECK(out_buf[i] <= 1.0f);
	}
	// First sample of sine at phase=0 should be 0
	CHECK(out_buf[0] == doctest::Approx(0.0f).epsilon(0.01f));

	arena.free();
}

TEST_CASE("[Symphony][Operators][Oscillator] Saw PolyBLEP output bounded") {
	ArenaAllocator arena;
	arena.init(ARENA_SIZE);

	HashMap<StringName, Variant> params;
	params.insert("frequency", 5000.0f); // High freq to stress anti-aliasing
	params.insert("waveform", 1.0f); // Saw

	SymphonyOscillator *osc = (SymphonyOscillator *)SymphonyOscillator::create(arena, params, MIX_RATE);

	float out_buf[SYMPHONY_MICRO_BLOCK_SIZE] = {};
	void *inputs[] = { nullptr, nullptr };
	void *outputs[] = { out_buf };
	osc->bind_pins(inputs, outputs);

	// Run several blocks
	for (int b = 0; b < 100; b++) {
		osc->execute(SYMPHONY_MICRO_BLOCK_SIZE);
	}

	// Output should stay bounded (no NaN, no explosion)
	for (int i = 0; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
		CHECK(!std::isnan(out_buf[i]));
		CHECK(!std::isinf(out_buf[i]));
		CHECK(out_buf[i] >= -1.5f); // PolyBLEP can slightly overshoot
		CHECK(out_buf[i] <= 1.5f);
	}

	arena.free();
}

TEST_CASE("[Symphony][Operators][Oscillator] Square PolyBLEP output bounded") {
	ArenaAllocator arena;
	arena.init(ARENA_SIZE);

	HashMap<StringName, Variant> params;
	params.insert("frequency", 10000.0f);
	params.insert("waveform", 2.0f); // Square

	SymphonyOscillator *osc = (SymphonyOscillator *)SymphonyOscillator::create(arena, params, MIX_RATE);

	float out_buf[SYMPHONY_MICRO_BLOCK_SIZE] = {};
	void *inputs[] = { nullptr, nullptr };
	void *outputs[] = { out_buf };
	osc->bind_pins(inputs, outputs);

	for (int b = 0; b < 100; b++) {
		osc->execute(SYMPHONY_MICRO_BLOCK_SIZE);
	}

	for (int i = 0; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
		CHECK(!std::isnan(out_buf[i]));
		CHECK(!std::isinf(out_buf[i]));
		CHECK(out_buf[i] >= -1.5f);
		CHECK(out_buf[i] <= 1.5f);
	}

	arena.free();
}

// --- S1.4: DelayLine Tests ---

TEST_CASE("[Symphony][Operators][DelayLine] Impulse delayed by correct sample count") {
	ArenaAllocator arena;
	arena.init(ARENA_SIZE);

	HashMap<StringName, Variant> params;
	params.insert("max_delay_ms", 100.0f);
	params.insert("delay_ms", 10.0f); // 10ms = 480 samples at 48kHz

	SymphonyDelayLine *dl = (SymphonyDelayLine *)SymphonyDelayLine::create(arena, params, MIX_RATE);

	float in_buf[SYMPHONY_MICRO_BLOCK_SIZE] = {};
	float out_buf[SYMPHONY_MICRO_BLOCK_SIZE] = {};
	void *inputs[] = { in_buf, nullptr };
	void *outputs[] = { out_buf };
	dl->bind_pins(inputs, outputs);

	// Send an impulse at sample 0 of the first block
	in_buf[0] = 1.0f;
	dl->execute(SYMPHONY_MICRO_BLOCK_SIZE);
	in_buf[0] = 0.0f;

	// The impulse should appear after 480 samples (10ms at 48kHz)
	// That's 480 / SYMPHONY_MICRO_BLOCK_SIZE blocks later
	int delay_samples = (int)(10.0f * MIX_RATE / 1000.0f);
	int blocks_needed = delay_samples / SYMPHONY_MICRO_BLOCK_SIZE;
	int sample_in_block = delay_samples % SYMPHONY_MICRO_BLOCK_SIZE;

	for (int b = 1; b < blocks_needed; b++) {
		dl->execute(SYMPHONY_MICRO_BLOCK_SIZE);
		// Should be silence
		for (int i = 0; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
			CHECK(out_buf[i] == doctest::Approx(0.0f).epsilon(0.001f));
		}
	}

	// Block where the impulse emerges
	dl->execute(SYMPHONY_MICRO_BLOCK_SIZE);
	CHECK(out_buf[sample_in_block] == doctest::Approx(1.0f).epsilon(0.05f));

	arena.free();
}

TEST_CASE("[Symphony][Operators][DelayLine] No NaN or Inf under modulation") {
	ArenaAllocator arena;
	arena.init(ARENA_SIZE);

	HashMap<StringName, Variant> params;
	params.insert("max_delay_ms", 50.0f);
	params.insert("delay_ms", 25.0f);

	SymphonyDelayLine *dl = (SymphonyDelayLine *)SymphonyDelayLine::create(arena, params, MIX_RATE);

	float in_buf[SYMPHONY_MICRO_BLOCK_SIZE];
	float out_buf[SYMPHONY_MICRO_BLOCK_SIZE];
	float delay_val = 5.0f;
	void *inputs[] = { in_buf, &delay_val };
	void *outputs[] = { out_buf };
	dl->bind_pins(inputs, outputs);

	// Feed noise and sweep delay
	for (int b = 0; b < 200; b++) {
		delay_val = 1.0f + 48.0f * ((float)b / 200.0f);
		for (int i = 0; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
			in_buf[i] = (float)(b * SYMPHONY_MICRO_BLOCK_SIZE + i) * 0.0001f;
			in_buf[i] = sinf(in_buf[i]); // Bounded input
		}
		dl->execute(SYMPHONY_MICRO_BLOCK_SIZE);
		for (int i = 0; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
			CHECK(!std::isnan(out_buf[i]));
			CHECK(!std::isinf(out_buf[i]));
		}
	}

	arena.free();
}

// --- S1.5: FeedbackPath Tests ---

TEST_CASE("[Symphony][Operators][FeedbackPath] Outputs zeros on first block") {
	ArenaAllocator arena;
	arena.init(ARENA_SIZE);

	HashMap<StringName, Variant> params;
	SymphonyFeedbackPath *fb = (SymphonyFeedbackPath *)SymphonyFeedbackPath::create(arena, params, MIX_RATE);

	float in_buf[SYMPHONY_MICRO_BLOCK_SIZE];
	float out_buf[SYMPHONY_MICRO_BLOCK_SIZE];
	for (int i = 0; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
		in_buf[i] = 1.0f;
	}
	void *inputs[] = { in_buf };
	void *outputs[] = { out_buf };
	fb->bind_pins(inputs, outputs);

	// First execute: output should be zeros (prev_block initialized to 0)
	fb->execute(SYMPHONY_MICRO_BLOCK_SIZE);
	for (int i = 0; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
		CHECK(out_buf[i] == 0.0f);
	}

	// Second execute: output should be the previous input (1.0)
	fb->execute(SYMPHONY_MICRO_BLOCK_SIZE);
	for (int i = 0; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
		CHECK(out_buf[i] == 1.0f);
	}

	arena.free();
}

TEST_CASE("[Symphony][Operators][FeedbackPath] Energy decays with gain < 1") {
	ArenaAllocator arena;
	arena.init(ARENA_SIZE);

	HashMap<StringName, Variant> params;
	SymphonyFeedbackPath *fb = (SymphonyFeedbackPath *)SymphonyFeedbackPath::create(arena, params, MIX_RATE);

	float buf[SYMPHONY_MICRO_BLOCK_SIZE];
	float out_buf[SYMPHONY_MICRO_BLOCK_SIZE];
	// Initial impulse
	for (int i = 0; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
		buf[i] = 1.0f;
	}
	void *inputs[] = { buf };
	void *outputs[] = { out_buf };
	fb->bind_pins(inputs, outputs);

	fb->execute(SYMPHONY_MICRO_BLOCK_SIZE); // First block: output 0, stores 1.0

	// Now simulate feedback with gain 0.5
	for (int b = 0; b < 20; b++) {
		// Use output as next input (scaled by 0.5)
		for (int i = 0; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
			buf[i] = out_buf[i] * 0.5f;
		}
		fb->execute(SYMPHONY_MICRO_BLOCK_SIZE);
	}

	// After 20 iterations with gain 0.5, energy should be near zero
	float energy = 0.0f;
	for (int i = 0; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
		energy += out_buf[i] * out_buf[i];
	}
	CHECK(energy < 0.0001f);

	arena.free();
}

// --- S1.6: ParameterSmoother Tests ---

TEST_CASE("[Symphony][Operators][ParameterSmoother] Snaps on first execution") {
	ArenaAllocator arena;
	arena.init(ARENA_SIZE);

	HashMap<StringName, Variant> params;
	params.insert("smooth_time_ms", 5.0f);

	SymphonyParameterSmoother *ps = (SymphonyParameterSmoother *)SymphonyParameterSmoother::create(arena, params, MIX_RATE);

	float input_val = 0.75f;
	float output_val = 0.0f;
	void *inputs[] = { &input_val, nullptr };
	void *outputs[] = { &output_val };
	ps->bind_pins(inputs, outputs);

	ps->execute(SYMPHONY_MICRO_BLOCK_SIZE);
	// Should snap to target immediately on first call
	CHECK(output_val == doctest::Approx(0.75f).epsilon(0.001f));

	arena.free();
}

TEST_CASE("[Symphony][Operators][ParameterSmoother] Smoothly approaches step change") {
	ArenaAllocator arena;
	arena.init(ARENA_SIZE);

	HashMap<StringName, Variant> params;
	params.insert("smooth_time_ms", 5.0f);

	SymphonyParameterSmoother *ps = (SymphonyParameterSmoother *)SymphonyParameterSmoother::create(arena, params, MIX_RATE);

	float input_val = 0.0f;
	float output_val = 0.0f;
	void *inputs[] = { &input_val, nullptr };
	void *outputs[] = { &output_val };
	ps->bind_pins(inputs, outputs);

	// Initialize at 0
	ps->execute(SYMPHONY_MICRO_BLOCK_SIZE);
	CHECK(output_val == doctest::Approx(0.0f).epsilon(0.001f));

	// Step to 1.0
	input_val = 1.0f;

	// After 1 block (~1.3ms at 64 samples/48kHz), should be partially there
	ps->execute(SYMPHONY_MICRO_BLOCK_SIZE);
	CHECK(output_val > 0.0f);
	CHECK(output_val < 1.0f);

	// After many blocks (~50ms), should be very close to 1.0
	for (int i = 0; i < 100; i++) {
		ps->execute(SYMPHONY_MICRO_BLOCK_SIZE);
	}
	CHECK(output_val == doctest::Approx(1.0f).epsilon(0.001f));

	arena.free();
}

// --- S1.7: StochasticTrigger Tests ---

TEST_CASE("[Symphony][Operators][StochasticTrigger] Density=0 produces no triggers") {
	ArenaAllocator arena;
	arena.init(ARENA_SIZE);

	HashMap<StringName, Variant> params;
	params.insert("density", 0.0f);
	params.insert("seed", 42.0f);

	SymphonyStochasticTrigger *st = (SymphonyStochasticTrigger *)SymphonyStochasticTrigger::create(arena, params, MIX_RATE);

	TriggerBuffer trig_buf;
	float density_val = 0.0f;
	void *inputs[] = { &density_val };
	void *outputs[] = { &trig_buf };
	st->bind_pins(inputs, outputs);

	int total_triggers = 0;
	for (int b = 0; b < 1000; b++) {
		st->execute(SYMPHONY_MICRO_BLOCK_SIZE);
		total_triggers += trig_buf.count;
	}
	CHECK(total_triggers == 0);

	arena.free();
}

TEST_CASE("[Symphony][Operators][StochasticTrigger] Density=100 produces ~100 triggers/sec") {
	ArenaAllocator arena;
	arena.init(ARENA_SIZE);

	HashMap<StringName, Variant> params;
	params.insert("density", 100.0f);
	params.insert("seed", 12345.0f);

	SymphonyStochasticTrigger *st = (SymphonyStochasticTrigger *)SymphonyStochasticTrigger::create(arena, params, MIX_RATE);

	TriggerBuffer trig_buf;
	float density_val = 100.0f;
	void *inputs[] = { &density_val };
	void *outputs[] = { &trig_buf };
	st->bind_pins(inputs, outputs);

	// Run for 1 second worth of samples
	int blocks_per_sec = (int)(MIX_RATE / SYMPHONY_MICRO_BLOCK_SIZE);
	int total_triggers = 0;
	for (int b = 0; b < blocks_per_sec; b++) {
		st->execute(SYMPHONY_MICRO_BLOCK_SIZE);
		total_triggers += trig_buf.count;
	}

	// Should be approximately 100 ±30%
	CHECK(total_triggers > 70);
	CHECK(total_triggers < 130);

	arena.free();
}

// --- S1.8: SVFilter Tests ---

TEST_CASE("[Symphony][Operators][SVFilter] LP reduces high-frequency energy") {
	ArenaAllocator arena;
	arena.init(ARENA_SIZE);

	HashMap<StringName, Variant> params;
	params.insert("cutoff", 500.0f); // Low cutoff
	params.insert("resonance", 0.0f);

	SymphonySVFilter *svf = (SymphonySVFilter *)SymphonySVFilter::create(arena, params, MIX_RATE);

	float in_buf[SYMPHONY_MICRO_BLOCK_SIZE];
	float lp_buf[SYMPHONY_MICRO_BLOCK_SIZE];
	float hp_buf[SYMPHONY_MICRO_BLOCK_SIZE];
	float bp_buf[SYMPHONY_MICRO_BLOCK_SIZE];
	void *inputs[] = { in_buf, nullptr, nullptr };
	void *outputs[] = { lp_buf, hp_buf, bp_buf };
	svf->bind_pins(inputs, outputs);

	// Generate high-frequency signal (10kHz sine)
	float energy_in = 0.0f;
	float energy_lp = 0.0f;

	for (int b = 0; b < 100; b++) {
		for (int i = 0; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
			float t = (float)(b * SYMPHONY_MICRO_BLOCK_SIZE + i) / MIX_RATE;
			in_buf[i] = sinf(2.0f * (float)Math::PI * 10000.0f * t);
		}
		svf->execute(SYMPHONY_MICRO_BLOCK_SIZE);

		for (int i = 0; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
			energy_in += in_buf[i] * in_buf[i];
			energy_lp += lp_buf[i] * lp_buf[i];
		}
	}

	// LP output energy should be much less than input for 10kHz through 500Hz filter
	CHECK(energy_lp < energy_in * 0.01f);

	arena.free();
}

TEST_CASE("[Symphony][Operators][SVFilter] No instability at extreme parameters") {
	ArenaAllocator arena;
	arena.init(ARENA_SIZE);

	HashMap<StringName, Variant> params;
	params.insert("cutoff", 20000.0f); // Max cutoff
	params.insert("resonance", 0.99f); // Near self-oscillation

	SymphonySVFilter *svf = (SymphonySVFilter *)SymphonySVFilter::create(arena, params, MIX_RATE);

	float in_buf[SYMPHONY_MICRO_BLOCK_SIZE];
	float lp_buf[SYMPHONY_MICRO_BLOCK_SIZE];
	float hp_buf[SYMPHONY_MICRO_BLOCK_SIZE];
	float bp_buf[SYMPHONY_MICRO_BLOCK_SIZE];
	void *inputs[] = { in_buf, nullptr, nullptr };
	void *outputs[] = { lp_buf, hp_buf, bp_buf };
	svf->bind_pins(inputs, outputs);

	// Feed impulse then silence
	memset(in_buf, 0, sizeof(in_buf));
	in_buf[0] = 1.0f;
	svf->execute(SYMPHONY_MICRO_BLOCK_SIZE);
	memset(in_buf, 0, sizeof(in_buf));

	// Run many blocks — should never produce NaN/Inf
	for (int b = 0; b < 500; b++) {
		svf->execute(SYMPHONY_MICRO_BLOCK_SIZE);
		for (int i = 0; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
			CHECK(!std::isnan(lp_buf[i]));
			CHECK(!std::isinf(lp_buf[i]));
			CHECK(!std::isnan(hp_buf[i]));
			CHECK(!std::isnan(bp_buf[i]));
		}
	}

	arena.free();
}

} // namespace TestSymphonyOperators
