/**************************************************************************/
/*  symphony_music_stereo_pcm.cpp                                         */
/**************************************************************************/

#include "symphony_music_stereo_pcm.h"

#include "core/math/math_funcs.h"
#include "core/os/memory.h"
#include "core/string/print_string.h"

#include <cstring>

SymphonyMusicStereoCache *SymphonyMusicStereoCache::singleton = nullptr;

SymphonyMusicStereoCache::SymphonyMusicStereoCache() {
	singleton = this;
}

SymphonyMusicStereoCache::~SymphonyMusicStereoCache() {
	for (const KeyValue<StringName, Entry> &E : cache) {
		if (E.value.ref_count > 0) {
			WARN_PRINT(vformat("SymphonyMusicStereoCache: Entry '%s' still has %d references at shutdown.", String(E.key), E.value.ref_count));
		}
		if (E.value.interleaved) {
			if (SymphonyMemoryBudget *budget = SymphonyMemoryBudget::get_singleton()) {
				budget->release_shared((size_t)E.value.frames * 2 * sizeof(float));
			}
			memfree(E.value.interleaved);
		}
	}
	cache.clear();
	singleton = nullptr;
}

const float *SymphonyMusicStereoCache::acquire(const StringName &p_key, const float *p_interleaved, int32_t p_frames, float p_sample_rate) {
	symphony_rt_note(SymphonyRTViolation::Mutex, "SymphonyMusicStereoCache::acquire");
	MutexLock lock(mutex);
	if (cache.has(p_key)) {
		Entry &e = cache[p_key];
		e.ref_count++;
		return e.interleaved;
	}
	if (!p_interleaved || p_frames <= 0) {
		return nullptr;
	}
	const size_t bytes = (size_t)p_frames * 2 * sizeof(float);
	SymphonyMemoryBudget *budget = SymphonyMemoryBudget::get_singleton();
	String budget_error;
	if (budget && !budget->try_reserve_shared(bytes, &budget_error)) {
		ERR_PRINT(budget_error.is_empty() ? vformat("SymphonyMusicStereoCache: budget reject for '%s'.", String(p_key)) : budget_error);
		return nullptr;
	}
	float *buf = (float *)memalloc(bytes);
	if (!buf) {
		if (budget) {
			budget->release_shared(bytes);
		}
		return nullptr;
	}
	symphony_rt_note(SymphonyRTViolation::Alloc, "SymphonyMusicStereoCache::acquire");
	memcpy(buf, p_interleaved, bytes);
	Entry e;
	e.interleaved = buf;
	e.frames = p_frames;
	e.sample_rate = p_sample_rate > 0.0f ? p_sample_rate : 48000.0f;
	e.ref_count = 1;
	cache[p_key] = e;
	symphony_rt_note(SymphonyRTViolation::ContainerMutation, "SymphonyMusicStereoCache::acquire");
	return buf;
}

void SymphonyMusicStereoCache::release(const StringName &p_key) {
	symphony_rt_note(SymphonyRTViolation::Mutex, "SymphonyMusicStereoCache::release");
	MutexLock lock(mutex);
	if (!cache.has(p_key)) {
		return;
	}
	Entry &e = cache[p_key];
	e.ref_count--;
	if (e.ref_count > 0) {
		return;
	}
	const size_t bytes = (size_t)e.frames * 2 * sizeof(float);
	if (SymphonyMemoryBudget *budget = SymphonyMemoryBudget::get_singleton()) {
		budget->release_shared(bytes);
	}
	symphony_rt_note(SymphonyRTViolation::Free, "SymphonyMusicStereoCache::release");
	memfree(e.interleaved);
	cache.erase(p_key);
	symphony_rt_note(SymphonyRTViolation::ContainerMutation, "SymphonyMusicStereoCache::release");
}

int32_t SymphonyMusicStereoCache::get_frames(const StringName &p_key) const {
	MutexLock lock(mutex);
	const Entry *e = cache.getptr(p_key);
	return e ? e->frames : 0;
}

float SymphonyMusicStereoCache::get_sample_rate(const StringName &p_key) const {
	MutexLock lock(mutex);
	const Entry *e = cache.getptr(p_key);
	return e ? e->sample_rate : 0.0f;
}

int32_t SymphonyMusicStereoCache::get_entry_count() const {
	MutexLock lock(mutex);
	return cache.size();
}

uint64_t SymphonyMusicStereoCache::hash_pcm(const float *p_interleaved, int32_t p_frames) {
	uint64_t h = 14695981039346656037ull;
	if (!p_interleaved || p_frames <= 0) {
		return h;
	}
	const int n = p_frames * 2;
	for (int i = 0; i < n; i++) {
		uint32_t bits = 0;
		memcpy(&bits, p_interleaved + i, sizeof(float));
		h ^= (uint64_t)bits;
		h *= 1099511628211ull;
	}
	return h;
}

StringName SymphonyMusicStereoCache::make_cache_key(const StringName &p_user_key, float p_rate, int32_t p_frames, uint64_t p_hash) {
	return StringName(vformat("%s|%d|%d|%08x%08x", String(p_user_key), (int)Math::round(p_rate), p_frames, (uint32_t)(p_hash >> 32), (uint32_t)p_hash));
}

size_t SymphonyMusicStereoCache::get_total_bytes() const {
	MutexLock lock(mutex);
	size_t total = 0;
	for (const KeyValue<StringName, Entry> &E : cache) {
		total += (size_t)E.value.frames * 2 * sizeof(float);
	}
	return total;
}
