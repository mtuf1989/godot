#pragma once

#include "core/os/mutex.h"
#include "core/templates/hash_map.h"
#include "core/string/string_name.h"

// SharedPCMCache: Singleton that caches decoded PCM data for GrainCloud source_pcm mode.
//
// Problem: When multiple voices use the same AudioStreamSymphony resource (which has a
// GrainCloud node with source_pcm data), each voice's arena gets its own copy of the
// same PCM data. For 8 voices × 768KB = 6MB of duplicate data.
//
// Solution: Store one shared copy per unique source. GrainCloud reads from the shared
// buffer instead of copying into its arena. Ref-counted per-entry so memory is released
// when no voices reference it.
//
// Thread safety: All access is from the main thread (graph compilation and destruction).
// The audio thread only reads through the pointer (which remains stable for the entry's
// lifetime). No lock needed for audio-thread reads.
class SharedPCMCache {
public:
	struct Entry {
		float *data = nullptr;     // Owned PCM buffer (heap-allocated)
		int32_t length = 0;        // Number of float samples
		int32_t ref_count = 0;     // Number of active GrainCloud instances referencing this
	};

	static SharedPCMCache *get_singleton() { return singleton; }

	// Acquire a reference to cached PCM data.
	// If the key doesn't exist, copies p_source_data into a new heap buffer.
	// If it already exists, increments ref_count and returns the existing buffer.
	// Returns pointer to the shared float buffer (valid until release is called).
	const float *acquire(const StringName &p_key, const float *p_source_data, int32_t p_length);

	// Release a reference. When ref_count reaches 0, the buffer is freed.
	void release(const StringName &p_key);

	// Get the length of a cached entry (0 if not found).
	int32_t get_length(const StringName &p_key) const;

	// Debug: number of active cache entries.
	int32_t get_entry_count() const;

	// Debug: total bytes used by all cached entries.
	size_t get_total_bytes() const;

	SharedPCMCache();
	~SharedPCMCache();

private:
	static SharedPCMCache *singleton;
	mutable Mutex mutex; // Protects cache map (main thread only, but defensive)
	HashMap<StringName, Entry> cache;
};
