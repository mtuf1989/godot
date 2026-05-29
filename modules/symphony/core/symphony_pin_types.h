#pragma once

#include <cstdint>

// Internal micro-block size: 512-sample driver buffer / 8 iterations
static constexpr int32_t SYMPHONY_MICRO_BLOCK_SIZE = 64;

// Pin data types for operator connections
enum class SymphonyPinType {
	AUDIO, // float* buffer of SYMPHONY_MICRO_BLOCK_SIZE samples (mono)
	FLOAT, // single float value (control-rate)
	INT,   // single int32_t value
	BOOL,  // single bool value
};
