#include "shared_pcm_cache.h"
#include "symphony_memory_budget.h"
#include "symphony_realtime_scope.h"
#include "core/os/memory.h"
#include "core/error/error_macros.h"
#include "core/variant/variant.h"
#include "core/templates/pair.h"

SharedPCMCache *SharedPCMCache::singleton = nullptr;

SharedPCMCache::SharedPCMCache() {
	singleton = this;
}

SharedPCMCache::~SharedPCMCache() {
	// Free any remaining entries (shouldn't happen in normal flow).
	SymphonyMemoryBudget *budget = SymphonyMemoryBudget::get_singleton();
	for (const KeyValue<StringName, Entry> &E : cache) {
		if (E.value.data) {
			if (budget) {
				budget->release_shared(sizeof(float) * (size_t)E.value.length);
			}
			memfree(E.value.data);
		}
		if (E.value.ref_count > 0) {
			WARN_PRINT(vformat("SharedPCMCache: Entry '%s' still has %d references at shutdown.", String(E.key), E.value.ref_count));
		}
	}
	cache.clear();
	singleton = nullptr;
}

const float *SharedPCMCache::acquire(const StringName &p_key, const float *p_source_data, int32_t p_length) {
	symphony_rt_note(SymphonyRTViolation::Mutex, "SharedPCMCache::acquire");
	MutexLock lock(mutex);

	if (cache.has(p_key)) {
		Entry &entry = cache[p_key];
		entry.ref_count++;
		return entry.data;
	}

	// New entry: allocate and copy. Charge unique SharedPCM once against the global budget.
	ERR_FAIL_COND_V(!p_source_data, nullptr);
	ERR_FAIL_COND_V(p_length <= 0, nullptr);

	const size_t bytes = sizeof(float) * (size_t)p_length;
	SymphonyMemoryBudget *budget = SymphonyMemoryBudget::get_singleton();
	String budget_error;
	if (budget && !budget->try_reserve_shared(bytes, &budget_error)) {
		ERR_FAIL_V_MSG(nullptr, budget_error);
	}

	Entry entry;
	entry.length = p_length;
	entry.ref_count = 1;
	entry.data = (float *)memalloc(bytes);
	if (!entry.data) {
		if (budget) {
			budget->release_shared(bytes);
		}
		ERR_FAIL_V(nullptr);
	}
	memcpy(entry.data, p_source_data, bytes);

	cache.insert(p_key, entry);
	symphony_rt_note(SymphonyRTViolation::Alloc, "SharedPCMCache::acquire");
	symphony_rt_note(SymphonyRTViolation::ContainerMutation, "SharedPCMCache::acquire");
	return entry.data;
}

void SharedPCMCache::release(const StringName &p_key) {
	symphony_rt_note(SymphonyRTViolation::Mutex, "SharedPCMCache::release");
	MutexLock lock(mutex);

	if (!cache.has(p_key)) {
		return;
	}

	Entry &entry = cache[p_key];
	entry.ref_count--;

	if (entry.ref_count <= 0) {
		if (entry.data) {
			if (SymphonyMemoryBudget *budget = SymphonyMemoryBudget::get_singleton()) {
				budget->release_shared(sizeof(float) * (size_t)entry.length);
			}
			memfree(entry.data);
			entry.data = nullptr;
		}
		cache.erase(p_key);
		symphony_rt_note(SymphonyRTViolation::Free, "SharedPCMCache::release");
		symphony_rt_note(SymphonyRTViolation::ContainerMutation, "SharedPCMCache::release");
	}
}

int32_t SharedPCMCache::get_length(const StringName &p_key) const {
	symphony_rt_note(SymphonyRTViolation::Mutex, "SharedPCMCache::get_length");
	MutexLock lock(mutex);
	if (cache.has(p_key)) {
		return cache[p_key].length;
	}
	return 0;
}

int32_t SharedPCMCache::get_entry_count() const {
	symphony_rt_note(SymphonyRTViolation::Mutex, "SharedPCMCache::get_entry_count");
	MutexLock lock(mutex);
	return cache.size();
}

size_t SharedPCMCache::get_total_bytes() const {
	symphony_rt_note(SymphonyRTViolation::Mutex, "SharedPCMCache::get_total_bytes");
	MutexLock lock(mutex);
	size_t total = 0;
	for (const KeyValue<StringName, Entry> &E : cache) {
		total += sizeof(float) * E.value.length;
	}
	return total;
}
