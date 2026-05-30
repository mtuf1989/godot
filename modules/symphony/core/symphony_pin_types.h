#pragma once

#include <cstdint>

// Internal micro-block size: 512-sample driver buffer / 8 iterations
static constexpr int32_t SYMPHONY_MICRO_BLOCK_SIZE = 64;

// Compiler hint: p_num_frames is always <= SYMPHONY_MICRO_BLOCK_SIZE.
// Helps auto-vectorization by communicating a fixed upper bound.
#if defined(__clang__) || defined(__GNUC__)
#define SYMPHONY_ASSUME_FRAMES(n) __builtin_assume((n) > 0 && (n) <= SYMPHONY_MICRO_BLOCK_SIZE)
#else
#define SYMPHONY_ASSUME_FRAMES(n) ((void)0)
#endif

// Pin data types for operator connections
enum class SymphonyPinType {
	AUDIO,   // float* buffer of SYMPHONY_MICRO_BLOCK_SIZE samples (mono)
	FLOAT,   // single float value (control-rate)
	INT,     // single int32_t value
	BOOL,    // single bool value
	TRIGGER, // TriggerBuffer*
};
