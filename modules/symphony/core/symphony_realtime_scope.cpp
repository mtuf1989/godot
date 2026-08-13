/**************************************************************************/
/*  symphony_realtime_scope.cpp                                           */
/**************************************************************************/

#include "symphony_realtime_scope.h"

#include "core/error/error_macros.h"
#include "core/string/ustring.h"

#include <atomic>

namespace {

constexpr int KIND_COUNT = (int)SymphonyRTViolation::COUNT;

std::atomic<uint32_t> g_kind_counts[KIND_COUNT] = {};
std::atomic<uint32_t> g_total{ 0 };
std::atomic<const char *> g_last_where{ nullptr };

#ifdef DEV_ENABLED
const char *_kind_name(SymphonyRTViolation p_kind) {
	switch (p_kind) {
		case SymphonyRTViolation::Alloc:
			return "alloc";
		case SymphonyRTViolation::Free:
			return "free";
		case SymphonyRTViolation::Mutex:
			return "mutex";
		case SymphonyRTViolation::ObjectDB:
			return "ObjectDB";
		case SymphonyRTViolation::Compile:
			return "compile";
		case SymphonyRTViolation::ContainerMutation:
			return "container";
		default:
			return "unknown";
	}
}
#endif

} // namespace

void SymphonyRealtimeScope::_record(SymphonyRTViolation p_kind, const char *p_where) {
	const int idx = (int)p_kind;
	if (idx < 0 || idx >= KIND_COUNT) {
		return;
	}
	g_kind_counts[idx].fetch_add(1, std::memory_order_relaxed);
	g_total.fetch_add(1, std::memory_order_relaxed);
	g_last_where.store(p_where, std::memory_order_relaxed);

#ifdef DEV_ENABLED
	if (assert_enabled) {
		ERR_PRINT(vformat("Symphony real-time violation: %s at %s", _kind_name(p_kind), p_where ? p_where : "(unknown)"));
		DEV_ASSERT(false);
	}
#else
	(void)p_where;
#endif
}

uint32_t SymphonyRealtimeScope::violation_count() {
	return g_total.load(std::memory_order_relaxed);
}

uint32_t SymphonyRealtimeScope::violation_count(SymphonyRTViolation p_kind) {
	const int idx = (int)p_kind;
	if (idx < 0 || idx >= KIND_COUNT) {
		return 0;
	}
	return g_kind_counts[idx].load(std::memory_order_relaxed);
}

const char *SymphonyRealtimeScope::last_where() {
	return g_last_where.load(std::memory_order_relaxed);
}

void SymphonyRealtimeScope::reset_violations() {
	for (int i = 0; i < KIND_COUNT; i++) {
		g_kind_counts[i].store(0, std::memory_order_relaxed);
	}
	g_total.store(0, std::memory_order_relaxed);
	g_last_where.store(nullptr, std::memory_order_relaxed);
}
