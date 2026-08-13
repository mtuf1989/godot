/**************************************************************************/
/*  symphony_realtime_scope.h                                             */
/**************************************************************************/

#pragma once

#include "core/error/error_macros.h"
#include "core/typedefs.h"

#include <cstdint>

// Thread-local real-time scope for Symphony audio callbacks (plan §6).
// Entered around mix / graph execute / mix callbacks. Instrumented Symphony
// alloc, free, mutex, ObjectDB, compile, and dynamic-container sites call
// symphony_rt_note(); a hit increments process-wide counters and DEV_ASSERTs
// unless tests suppress the trap.

enum class SymphonyRTViolation : uint8_t {
	Alloc = 0,
	Free,
	Mutex,
	ObjectDB,
	Compile,
	ContainerMutation,
	COUNT
};

class SymphonyRealtimeScope {
	static inline thread_local int32_t depth = 0;
	static inline thread_local bool assert_enabled = true;

	static void _record(SymphonyRTViolation p_kind, const char *p_where);

public:
	_FORCE_INLINE_ SymphonyRealtimeScope() { depth++; }
	_FORCE_INLINE_ ~SymphonyRealtimeScope() {
		if (depth > 0) {
			depth--;
		}
	}

	SymphonyRealtimeScope(const SymphonyRealtimeScope &) = delete;
	SymphonyRealtimeScope &operator=(const SymphonyRealtimeScope &) = delete;

	_FORCE_INLINE_ static bool in_scope() { return depth > 0; }
	_FORCE_INLINE_ static int32_t get_depth() { return depth; }

	static uint32_t violation_count();
	static uint32_t violation_count(SymphonyRTViolation p_kind);
	static const char *last_where();
	static void reset_violations();

	_FORCE_INLINE_ static bool is_assert_enabled() { return assert_enabled; }
	_FORCE_INLINE_ static void set_assert_enabled(bool p_enabled) { assert_enabled = p_enabled; }

	static void record(SymphonyRTViolation p_kind, const char *p_where) { _record(p_kind, p_where); }
};

// RAII: disable DEV_ASSERT traps so tests can observe violation counters.
class SymphonyRealtimeAssertSuppressor {
	bool previous;

public:
	SymphonyRealtimeAssertSuppressor() :
			previous(SymphonyRealtimeScope::is_assert_enabled()) {
		SymphonyRealtimeScope::set_assert_enabled(false);
	}
	~SymphonyRealtimeAssertSuppressor() {
		SymphonyRealtimeScope::set_assert_enabled(previous);
	}

	SymphonyRealtimeAssertSuppressor(const SymphonyRealtimeAssertSuppressor &) = delete;
	SymphonyRealtimeAssertSuppressor &operator=(const SymphonyRealtimeAssertSuppressor &) = delete;
};

_FORCE_INLINE_ void symphony_rt_note(SymphonyRTViolation p_kind, const char *p_where = nullptr) {
	if (!SymphonyRealtimeScope::in_scope()) {
		return;
	}
	SymphonyRealtimeScope::record(p_kind, p_where);
}
