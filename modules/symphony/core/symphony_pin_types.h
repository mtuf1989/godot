#pragma once

#include <cstdint>

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
