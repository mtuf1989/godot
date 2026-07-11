#pragma once

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
