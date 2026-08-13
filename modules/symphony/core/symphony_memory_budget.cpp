/**************************************************************************/
/*  symphony_memory_budget.cpp                                            */
/**************************************************************************/

#include "symphony_memory_budget.h"

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
	if (reserved_bytes > global_limit_bytes - p_bytes) {
		if (r_error) {
			*r_error = vformat("Global Symphony memory budget exceeded (need %d more bytes, %d/%d used).", (int64_t)p_bytes, (int64_t)reserved_bytes, (int64_t)global_limit_bytes);
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

void SymphonyMemoryBudget::set_shared_pcm_bytes(size_t p_bytes) {
	shared_pcm_bytes = p_bytes;
}

void SymphonyMemoryBudget::set_package_counts(uint32_t p_active, uint32_t p_pending, uint32_t p_outgoing, uint32_t p_retired) {
	active_packages = p_active;
	pending_packages = p_pending;
	outgoing_packages = p_outgoing;
	retired_packages = p_retired;
}

SymphonyMemoryBudget::Snapshot SymphonyMemoryBudget::get_snapshot() const {
	Snapshot snap;
	snap.per_graph_limit_bytes = per_graph_limit_bytes;
	snap.global_limit_bytes = global_limit_bytes;
	snap.reserved_bytes = reserved_bytes;
	snap.peak_reserved_bytes = peak_reserved_bytes;
	snap.shared_pcm_bytes = shared_pcm_bytes;
	snap.active_packages = active_packages;
	snap.pending_packages = pending_packages;
	snap.outgoing_packages = outgoing_packages;
	snap.retired_packages = retired_packages;
	return snap;
}
