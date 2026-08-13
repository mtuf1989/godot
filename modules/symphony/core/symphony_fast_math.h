/**************************************************************************/
/*  symphony_fast_math.h                                                  */
/**************************************************************************/

#pragma once

#include <cmath>

// Shared real-time math helpers for Symphony operators and transitions.
namespace SymphonyFastMath {

// Normalized fifth-order sine with quarter-wave folding.
// phase in [0, 1) maps to one full cycle; output ≈ sin(2π·phase).
// Acceptance: max abs error ≤ 0.005 (improve_plan §8).
[[nodiscard]] inline float fast_sine(float p_phase) {
	float x = p_phase - floorf(p_phase);
	float sign = 1.0f;
	if (x >= 0.5f) {
		x -= 0.5f;
		sign = -1.0f;
	}
	if (x >= 0.25f) {
		x = 0.5f - x;
	}
	// x in [0, 0.25] → u in [0, 1] for sin(π/2 · u)
	float u = x * 4.0f;
	float u2 = u * u;
	float s = u * (1.5707963f + u2 * (-0.6459641f + u2 * 0.0796926f));
	return sign * s;
}

// Equal-power crossfade gains for progress in [0, 1].
inline void equal_power_gains(float p_progress, float &r_gain_a, float &r_gain_b) {
	float t = p_progress < 0.0f ? 0.0f : (p_progress > 1.0f ? 1.0f : p_progress);
	// cos/sin of (t * π/2) via quarter-wave of the shared sine
	r_gain_a = fast_sine(0.25f - t * 0.25f); // cos(π t / 2)
	r_gain_b = fast_sine(t * 0.25f); // sin(π t / 2)
}

} // namespace SymphonyFastMath
