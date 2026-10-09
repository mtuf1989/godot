#pragma once

#include "core/typedefs.h"

// Hybrid sample-rate policy (tech-fix plan P4 / user decision D).
// SFX graphs author at a fixed 48 kHz. Music follows the device only when it
// is 44.1 kHz or 48 kHz; otherwise music runs at 48 kHz and Godot resamples.
// Offline / golden renders always use 48 kHz.
//
// A driver restart that changes the device rate requires stop and reload;
// live rate switching is not handled.

inline constexpr float SYMPHONY_SFX_RATE = 48000.0f;
inline constexpr float SYMPHONY_OFFLINE_RATE = 48000.0f;

[[nodiscard]] inline float symphony_offline_rate() {
	return SYMPHONY_OFFLINE_RATE;
}

[[nodiscard]] inline float symphony_select_music_rate(float p_device_rate) {
	if (p_device_rate == 44100.0f || p_device_rate == 48000.0f) {
		return p_device_rate;
	}
	// Also accept near-exact rates from float noise on some drivers.
	if (p_device_rate > 44050.0f && p_device_rate < 44150.0f) {
		return 44100.0f;
	}
	if (p_device_rate > 47950.0f && p_device_rate < 48050.0f) {
		return 48000.0f;
	}
	return SYMPHONY_SFX_RATE;
}
