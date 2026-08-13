/**************************************************************************/
/*  symphony_memory_budget.cpp                                            */
/**************************************************************************/

#include "symphony_memory_budget.h"
#include "symphony_graph_package_retirement.h"

#include "core/string/ustring.h"
#include "core/error/error_macros.h"

SymphonyMemoryBudget *SymphonyMemoryBudget::singleton = nullptr;

SymphonyMemoryBudget *SymphonyMemoryBudget::get_singleton() {
	return singleton;
}

void SymphonyMemoryBudget::create_singleton() {
	ERR_FAIL_COND(singleton != nullptr);
	singleton = memnew(SymphonyMemoryBudget);
}

void SymphonyMemoryBudget::destroy_singleton() {
	ERR_FAIL_NULL(singleton);
	memdelete(singleton);
	singleton = nullptr;
}

void SymphonyMemoryBudget::set_per_graph_limit_bytes(size_t p_bytes) {
	per_graph_limit_bytes = p_bytes;
}

void SymphonyMemoryBudget::set_global_limit_bytes(size_t p_bytes) {
	global_limit_bytes = p_bytes;
}

bool SymphonyMemoryBudget::try_reserve(size_t p_bytes, String *r_error) {
	if (p_bytes > per_graph_limit_bytes) {
		if (r_error) {
			*r_error = vformat("Per-graph memory budget exceeded (%d bytes > %d limit).", (int64_t)p_bytes, (int64_t)per_graph_limit_bytes);
		}
		return false;
	}
	const size_t used = reserved_bytes + shared_pcm_bytes;
	if (p_bytes > global_limit_bytes || used > global_limit_bytes - p_bytes) {
		if (r_error) {
			*r_error = vformat("Global Symphony memory budget exceeded (need %d more bytes, %d/%d used including SharedPCM).", (int64_t)p_bytes, (int64_t)used, (int64_t)global_limit_bytes);
		}
		return false;
	}
	reserved_bytes += p_bytes;
	if (reserved_bytes > peak_reserved_bytes) {
		peak_reserved_bytes = reserved_bytes;
	}
	return true;
}

void SymphonyMemoryBudget::release(size_t p_bytes) {
	ERR_FAIL_COND(p_bytes > reserved_bytes);
	reserved_bytes -= p_bytes;
}

bool SymphonyMemoryBudget::try_reserve_shared(size_t p_bytes, String *r_error) {
	const size_t used = reserved_bytes + shared_pcm_bytes;
	if (p_bytes > global_limit_bytes || used > global_limit_bytes - p_bytes) {
		if (r_error) {
			*r_error = vformat("Global Symphony memory budget exceeded for SharedPCM (need %d more bytes, %d/%d used).", (int64_t)p_bytes, (int64_t)used, (int64_t)global_limit_bytes);
		}
		return false;
	}
	shared_pcm_bytes += p_bytes;
	return true;
}

void SymphonyMemoryBudget::release_shared(size_t p_bytes) {
	ERR_FAIL_COND(p_bytes > shared_pcm_bytes);
	shared_pcm_bytes -= p_bytes;
}

void SymphonyMemoryBudget::set_shared_pcm_bytes(size_t p_bytes) {
	shared_pcm_bytes = p_bytes;
}

void SymphonyMemoryBudget::set_package_counts(uint32_t p_active, uint32_t p_pending, uint32_t p_outgoing, uint32_t p_retired) {
	active_packages.store(p_active, std::memory_order_relaxed);
	pending_packages.store(p_pending, std::memory_order_relaxed);
	outgoing_packages.store(p_outgoing, std::memory_order_relaxed);
	retired_packages = p_retired;
}

static void _adjust_u32(std::atomic<uint32_t> &p_counter, int32_t p_delta) {
	if (p_delta == 0) {
		return;
	}
	if (p_delta > 0) {
		p_counter.fetch_add((uint32_t)p_delta, std::memory_order_relaxed);
		return;
	}
	uint32_t cur = p_counter.load(std::memory_order_relaxed);
	uint32_t sub = (uint32_t)(-p_delta);
	while (true) {
		uint32_t next = (cur > sub) ? (cur - sub) : 0;
		if (p_counter.compare_exchange_weak(cur, next, std::memory_order_relaxed)) {
			break;
		}
	}
}

void SymphonyMemoryBudget::adjust_active_packages(int32_t p_delta) {
	_adjust_u32(active_packages, p_delta);
}

void SymphonyMemoryBudget::adjust_pending_packages(int32_t p_delta) {
	_adjust_u32(pending_packages, p_delta);
}

void SymphonyMemoryBudget::adjust_outgoing_packages(int32_t p_delta) {
	_adjust_u32(outgoing_packages, p_delta);
}

SymphonyMemoryBudget::Snapshot SymphonyMemoryBudget::get_snapshot() const {
	Snapshot snap;
	snap.per_graph_limit_bytes = per_graph_limit_bytes;
	snap.global_limit_bytes = global_limit_bytes;
	snap.reserved_bytes = reserved_bytes;
	snap.peak_reserved_bytes = peak_reserved_bytes;
	snap.shared_pcm_bytes = shared_pcm_bytes;
	snap.active_packages = active_packages.load(std::memory_order_relaxed);
	snap.pending_packages = pending_packages.load(std::memory_order_relaxed);
	snap.outgoing_packages = outgoing_packages.load(std::memory_order_relaxed);
	snap.retired_packages = GraphPackageRetirement::get_pending_count();
	return snap;
}
