/**************************************************************************/
/*  test_symphony_early_reflections.cpp                                   */
/*  Suite: [Symphony][Spatial][EarlyReflections] — shoebox (Task 16).     */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_early_reflections)

#include "modules/symphony/nodes/delay/symphony_early_reflections.h"

namespace TestSymphonyEarlyReflections {

using ER = SymphonyEarlyReflections;

// The image-source geometry is verified through the pure static helper
// compute_shoebox_reflections(); the DSP operator is a multi-tap delay driven by
// exactly these taps.

TEST_CASE("[Symphony][Spatial][EarlyReflections] Closed room generates 6 taps") {
	ER::Tap taps[ER::NUM_WALLS];
	ER::compute_shoebox_reflections(Vector3(10, 4, 8), Vector3(0, 0, 0), Vector3(0, 0, 0), 0.8f, 343.0f, taps);
	int active = 0;
	for (int i = 0; i < ER::NUM_WALLS; i++) {
		if (taps[i].delay_seconds > 0.0f && taps[i].gain > 0.0f) {
			active++;
		}
	}
	CHECK(active == 6); // one image source per wall
}

TEST_CASE("[Symphony][Spatial][EarlyReflections] Tap delay matches distance / speed") {
	// Listener + source at the room centre. The +X wall sits at half=5m, so its
	// image source is 10m away → round-trip reflection distance = 10m.
	ER::Tap taps[ER::NUM_WALLS];
	const float speed = 343.0f;
	ER::compute_shoebox_reflections(Vector3(10, 4, 8), Vector3(0, 0, 0), Vector3(0, 0, 0), 1.0f, speed, taps);
	// Wall 0 is +X at +5: image at x = 2*5 - 0 = 10, distance from origin = 10.
	CHECK(taps[0].delay_seconds == doctest::Approx(10.0f / speed).epsilon(0.001));
	// Wall 2 is +Y at +2: image at y = 4, distance = 4.
	CHECK(taps[2].delay_seconds == doctest::Approx(4.0f / speed).epsilon(0.001));
}

TEST_CASE("[Symphony][Spatial][EarlyReflections] Gain tracks reflection coefficient") {
	ER::Tap hi[ER::NUM_WALLS];
	ER::Tap lo[ER::NUM_WALLS];
	ER::compute_shoebox_reflections(Vector3(10, 4, 8), Vector3(), Vector3(), 0.9f, 343.0f, hi);
	ER::compute_shoebox_reflections(Vector3(10, 4, 8), Vector3(), Vector3(), 0.3f, 343.0f, lo);
	for (int i = 0; i < ER::NUM_WALLS; i++) {
		// Same geometry, higher reflection coefficient → louder tap.
		CHECK(hi[i].gain > lo[i].gain);
	}
	// A fully absorptive room (reflection 0) → silent taps.
	ER::Tap dead[ER::NUM_WALLS];
	ER::compute_shoebox_reflections(Vector3(10, 4, 8), Vector3(), Vector3(), 0.0f, 343.0f, dead);
	for (int i = 0; i < ER::NUM_WALLS; i++) {
		CHECK(dead[i].gain == doctest::Approx(0.0f));
	}
}

TEST_CASE("[Symphony][Spatial][EarlyReflections] Large room → later and quieter than small") {
	ER::Tap small[ER::NUM_WALLS];
	ER::Tap large[ER::NUM_WALLS];
	ER::compute_shoebox_reflections(Vector3(4, 3, 4), Vector3(), Vector3(), 0.8f, 343.0f, small);
	ER::compute_shoebox_reflections(Vector3(40, 10, 40), Vector3(), Vector3(), 0.8f, 343.0f, large);

	// Compare the +X wall tap (index 0): the big room's image is farther away.
	CHECK(large[0].delay_seconds > small[0].delay_seconds); // arrives later
	CHECK(large[0].gain < small[0].gain); // quieter (1/distance falloff)
}

TEST_CASE("[Symphony][Spatial][EarlyReflections] Unauthored dimensions → no reflections") {
	ER::Tap taps[ER::NUM_WALLS];
	// Zero dimension (unauthored → engine should fall back to estimate, but the
	// operator must degrade gracefully rather than emit garbage).
	ER::compute_shoebox_reflections(Vector3(0, 0, 0), Vector3(), Vector3(), 0.8f, 343.0f, taps);
	for (int i = 0; i < ER::NUM_WALLS; i++) {
		CHECK(taps[i].delay_seconds == doctest::Approx(0.0f));
		CHECK(taps[i].gain == doctest::Approx(0.0f));
	}
}

TEST_CASE("[Symphony][Spatial][EarlyReflections] Off-centre source shifts opposite-wall delays") {
	// Move the source toward the +X wall: the +X reflection shortens, the −X
	// reflection lengthens.
	ER::Tap centered[ER::NUM_WALLS];
	ER::Tap shifted[ER::NUM_WALLS];
	ER::compute_shoebox_reflections(Vector3(10, 4, 8), Vector3(), Vector3(), 1.0f, 343.0f, centered);
	ER::compute_shoebox_reflections(Vector3(10, 4, 8), Vector3(), Vector3(3, 0, 0), 1.0f, 343.0f, shifted);

	// Wall 0 = +X: source moved toward it → image closer → shorter delay.
	CHECK(shifted[0].delay_seconds < centered[0].delay_seconds);
	// Wall 1 = −X: source moved away → image farther → longer delay.
	CHECK(shifted[1].delay_seconds > centered[1].delay_seconds);
}

TEST_CASE("[Symphony][Spatial][EarlyReflections] Arena byte estimate is finite and positive") {
	HashMap<StringName, Variant> params;
	params["max_room_dim"] = 40.0f;
	size_t bytes = ER::calculate_arena_bytes(params, 48000.0f);
	CHECK(bytes > 0);
	CHECK(bytes != std::numeric_limits<size_t>::max());
	// Buffer must hold at least the longest tap: 3× max dim / speed × rate.
	size_t min_expected = (size_t)(40.0f * 3.0f / 343.0f * 48000.0f);
	CHECK(bytes >= min_expected * sizeof(float));
}

} // namespace TestSymphonyEarlyReflections
