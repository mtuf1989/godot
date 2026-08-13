/**************************************************************************/
/*  symphony_runtime_metrics.h                                            */
/**************************************************************************/

#pragma once

#include <atomic>
#include <cstdint>

// Process-wide read-only counters for Symphony diagnostics (plan § API metrics).
// Writers: audio or main. Readers: main-thread getters / get_debug_metrics().

inline std::atomic<uint64_t> &symphony_spectral_underflow_count() {
	static std::atomic<uint64_t> counter{ 0 };
	return counter;
}

inline void symphony_note_spectral_underflow() {
	symphony_spectral_underflow_count().fetch_add(1, std::memory_order_relaxed);
}

inline std::atomic<uint64_t> &symphony_packages_destroyed_count() {
	static std::atomic<uint64_t> counter{ 0 };
	return counter;
}

inline std::atomic<uint32_t> &symphony_retirement_peak_pending() {
	static std::atomic<uint32_t> peak{ 0 };
	return peak;
}

inline void symphony_note_packages_destroyed(uint32_t p_count) {
	if (p_count == 0) {
		return;
	}
	symphony_packages_destroyed_count().fetch_add(p_count, std::memory_order_relaxed);
}

inline void symphony_note_retirement_pending(uint32_t p_pending) {
	uint32_t prev = symphony_retirement_peak_pending().load(std::memory_order_relaxed);
	while (p_pending > prev) {
		if (symphony_retirement_peak_pending().compare_exchange_weak(prev, p_pending, std::memory_order_relaxed)) {
			break;
		}
	}
}
