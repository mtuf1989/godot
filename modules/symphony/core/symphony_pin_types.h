#pragma once

#include <cmath>
#include <cstdint>

// === Cross-platform compiler intrinsics ===

// Restrict qualifier: tells the compiler pointers don't alias.
// MSVC uses __restrict, GCC/Clang use __restrict__.
#if defined(_MSC_VER)
#define SYMPHONY_RESTRICT __restrict
#else
#define SYMPHONY_RESTRICT __restrict__
#endif

// Count trailing zeros (used in Voss-McCartney pink noise).
// MSVC doesn't have __builtin_ctz; use _BitScanForward instead.
#if defined(_MSC_VER)
#include <intrin.h>
static inline int32_t symphony_ctz(uint32_t value) {
	unsigned long index;
	if (_BitScanForward(&index, value)) {
		return (int32_t)index;
	}
	return 32; // undefined for 0 in __builtin_ctz too, but safe fallback
}
#else
static inline int32_t symphony_ctz(uint32_t value) {
	return __builtin_ctz(value);
}
#endif

// Internal micro-block size: configurable per platform.
// Web (128-sample AudioWorklet buffer): 32 samples = 4 iterations for finer trigger resolution.
// Native (512-sample driver buffer): 64 samples = 8 iterations, better throughput.
#ifndef SYMPHONY_MICRO_BLOCK_SIZE
#ifdef __EMSCRIPTEN__
static constexpr int32_t SYMPHONY_MICRO_BLOCK_SIZE = 32;
#else
static constexpr int32_t SYMPHONY_MICRO_BLOCK_SIZE = 64;
#endif
#endif

// Compiler hint: p_num_frames is always <= SYMPHONY_MICRO_BLOCK_SIZE.
// Helps auto-vectorization by communicating a fixed upper bound.
#if defined(__clang__) || defined(__GNUC__)
#define SYMPHONY_ASSUME_FRAMES(n) __builtin_assume((n) > 0 && (n) <= SYMPHONY_MICRO_BLOCK_SIZE)
#else
#define SYMPHONY_ASSUME_FRAMES(n) ((void)0)
#endif

// Loop unrolling hint for hot inner loops.
#if defined(__clang__)
#define SYMPHONY_UNROLL _Pragma("clang loop unroll_count(8)")
#elif defined(__GNUC__)
#define SYMPHONY_UNROLL _Pragma("GCC unroll 8")
#else
#define SYMPHONY_UNROLL
#endif

// Pin data types for operator connections
enum class SymphonyPinType {
	AUDIO,   // float* buffer of SYMPHONY_MICRO_BLOCK_SIZE samples (mono)
	FLOAT,   // single float value (control-rate)
	INT,     // single int32_t value
	BOOL,    // single bool value
	TRIGGER, // TriggerBuffer*
};

// ============================================================================
// SmoothedFloat — One-pole parameter smoother (12 bytes)
// ============================================================================
// Eliminates clicks/zipper noise when control-rate parameters change.
// Runs at control rate (once per micro-block), converging toward target.
// Based on Brickworks' built-in parameter smoothing pattern:
//   smoothed_value += coeff * (target - smoothed_value)
//
// Usage:
//   SmoothedFloat gain_smooth;
//   gain_smooth.set_time(5.0f, 44100.0f);  // 5ms smoothing at 44.1kHz
//   gain_smooth.reset(1.0f);                // Initialize without smoothing
//   // Each micro-block:
//   gain_smooth.set_target(new_gain);
//   float g = gain_smooth.next();           // Smoothed value for this block
struct SmoothedFloat {
	float current = 0.0f;
	float target = 0.0f;
	float coeff = 1.0f; // 1.0 = instant (no smoothing)

	// Configure smoothing time constant.
	// p_time_ms: time to reach ~63% of target (one time constant).
	// p_sample_rate: audio sample rate (used to derive per-block coefficient).
	// The coefficient is pre-calculated for SYMPHONY_MICRO_BLOCK_SIZE-sample blocks.
	void set_time(float p_time_ms, float p_sample_rate) {
		if (p_time_ms < 0.01f) {
			coeff = 1.0f; // Instant
		} else {
			float block_duration = (float)SYMPHONY_MICRO_BLOCK_SIZE / p_sample_rate;
			float tau = p_time_ms * 0.001f;
			coeff = 1.0f - expf(-block_duration / tau);
		}
	}

	// Set the target value (called when parameter changes).
	void set_target(float p_value) {
		target = p_value;
	}

	// Bypass smoothing — snap current to value immediately.
	// Use for initialization or discontinuous resets.
	void reset(float p_value) {
		current = p_value;
		target = p_value;
	}

	// Advance one step and return the smoothed value.
	// Call once per micro-block (control rate).
	float next() {
		current += coeff * (target - current);
		return current;
	}

	// Check if smoothing has converged (within epsilon of target).
	bool is_settled(float p_epsilon = 1e-6f) const {
		float diff = target - current;
		return (diff > -p_epsilon) && (diff < p_epsilon);
	}
};

// ============================================================================
// Debug Assertions — Zero cost in release builds
// ============================================================================
// Catches NaN/Infinity propagation, null buffer access, and float domain errors
// on the audio thread. Inspired by Brickworks' Design-by-Contract pattern.
//
// SYMPHONY_ASSERT_FINITE(buf, count):
//   Asserts every sample in an audio buffer is a finite float (not NaN, not Inf).
//   Catches silent float corruption before it cascades through the graph.
//
// SYMPHONY_ASSERT_FINITE_SCALAR(value):
//   Asserts a single float value is finite. For control-rate parameters.

#ifdef DEV_ENABLED
#include "core/error/error_macros.h"

#define SYMPHONY_ASSERT_FINITE(buf, count)                                                   \
	do {                                                                                     \
		for (int32_t _sf_i = 0; _sf_i < (count); _sf_i++) {                                 \
			DEV_ASSERT(std::isfinite((buf)[_sf_i]) && "Symphony: NaN/Inf detected in audio buffer"); \
		}                                                                                    \
	} while (0)

#define SYMPHONY_ASSERT_FINITE_SCALAR(value)                                           \
	DEV_ASSERT(std::isfinite(value) && "Symphony: NaN/Inf detected in float parameter")

// Assert a pointer is non-null (catch unbound pins).
#define SYMPHONY_ASSERT_NOT_NULL(ptr) \
	DEV_ASSERT((ptr) != nullptr && "Symphony: null pointer in audio path")

#else
#define SYMPHONY_ASSERT_FINITE(buf, count) ((void)0)
#define SYMPHONY_ASSERT_FINITE_SCALAR(value) ((void)0)
#define SYMPHONY_ASSERT_NOT_NULL(ptr) ((void)0)
#endif
