#include "shared_pcm_cache.h"
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
	for (const KeyValue<StringName, Entry> &E : cache) {
		if (E.value.data) {
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
	MutexLock lock(mutex);

	if (cache.has(p_key)) {
		Entry &entry = cache[p_key];
		entry.ref_count++;
		return entry.data;
	}

	// New entry: allocate and copy.
	ERR_FAIL_COND_V(!p_source_data, nullptr);
	ERR_FAIL_COND_V(p_length <= 0, nullptr);

	Entry entry;
	entry.length = p_length;
	entry.ref_count = 1;
	entry.data = (float *)memalloc(sizeof(float) * p_length);
	ERR_FAIL_COND_V(!entry.data, nullptr);
	memcpy(entry.data, p_source_data, sizeof(float) * p_length);

	cache.insert(p_key, entry);
	return entry.data;
}

void SharedPCMCache::release(const StringName &p_key) {
	MutexLock lock(mutex);

	if (!cache.has(p_key)) {
		return;
	}

	Entry &entry = cache[p_key];
	entry.ref_count--;

	if (entry.ref_count <= 0) {
		if (entry.data) {
			memfree(entry.data);
			entry.data = nullptr;
		}
		cache.erase(p_key);
	}
}

int32_t SharedPCMCache::get_length(const StringName &p_key) const {
	MutexLock lock(mutex);
	if (cache.has(p_key)) {
		return cache[p_key].length;
	}
	return 0;
}

int32_t SharedPCMCache::get_entry_count() const {
	MutexLock lock(mutex);
	return cache.size();
}

size_t SharedPCMCache::get_total_bytes() const {
	MutexLock lock(mutex);
	size_t total = 0;
	for (const KeyValue<StringName, Entry> &E : cache) {
		total += sizeof(float) * E.value.length;
	}
	return total;
}
