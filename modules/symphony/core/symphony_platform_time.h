#pragma once

#include <cstdint>

// High-resolution timer for profiling. Returns elapsed microseconds.
// Used on the audio thread — must be lock-free and fast.

#if defined(__APPLE__)
#include <mach/mach_time.h>

static inline uint64_t symphony_time_usec() {
	static double timebase = 0.0;
	if (timebase == 0.0) {
		mach_timebase_info_data_t info;
		mach_timebase_info(&info);
		timebase = (double)info.numer / (double)info.denom / 1000.0;
	}
	return (uint64_t)(mach_absolute_time() * timebase);
}

#elif defined(_WIN32)
#include <windows.h>

static inline uint64_t symphony_time_usec() {
	static double freq_inv = 0.0;
	if (freq_inv == 0.0) {
		LARGE_INTEGER f;
		QueryPerformanceFrequency(&f);
		freq_inv = 1000000.0 / (double)f.QuadPart;
	}
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	return (uint64_t)(now.QuadPart * freq_inv);
}

#elif defined(__EMSCRIPTEN__)
#include <emscripten.h>

static inline uint64_t symphony_time_usec() {
	return (uint64_t)(emscripten_get_now() * 1000.0);
}

#else // Linux / POSIX
#include <time.h>

static inline uint64_t symphony_time_usec() {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

#endif
