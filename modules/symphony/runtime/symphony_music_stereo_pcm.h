/**************************************************************************/
/*  symphony_music_stereo_pcm.h                                           */
/**************************************************************************/
/* Stereo float PCM cache for music recorded phrases (N3).                */
/* Separate from mono SharedPCMCache used by GrainCloud.                  */
/**************************************************************************/

#pragma once

#include "../core/symphony_memory_budget.h"
#include "../core/symphony_realtime_scope.h"

#include "core/os/mutex.h"
#include "core/string/string_name.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"

#include <cstdint>

// Interleaved stereo float PCM at a fixed music rate (policy D).
struct SymphonyMusicStereoPCM {
	float *interleaved = nullptr; // LRLR…, length = frames * 2
	int32_t frames = 0;
	float sample_rate = 48000.0f;
	StringName key;

	[[nodiscard]] int32_t sample_count() const { return frames * 2; }
};

// Ref-counted stereo PCM store. Main-thread acquire/release; audio only reads.
class SymphonyMusicStereoCache {
public:
	struct Entry {
		float *interleaved = nullptr;
		int32_t frames = 0;
		float sample_rate = 48000.0f;
		int32_t ref_count = 0;
	};

	static SymphonyMusicStereoCache *get_singleton() { return singleton; }

	// Copy p_interleaved (frames*2 floats) into the cache under p_key.
	const float *acquire(const StringName &p_key, const float *p_interleaved, int32_t p_frames, float p_sample_rate);
	void release(const StringName &p_key);
	static uint64_t hash_pcm(const float *p_interleaved, int32_t p_frames);
	static StringName make_cache_key(const StringName &p_user_key, float p_rate, int32_t p_frames, uint64_t p_hash);
	[[nodiscard]] int32_t get_frames(const StringName &p_key) const;
	[[nodiscard]] float get_sample_rate(const StringName &p_key) const;
	[[nodiscard]] int32_t get_entry_count() const;
	[[nodiscard]] size_t get_total_bytes() const;

	SymphonyMusicStereoCache();
	~SymphonyMusicStereoCache();

private:
	static SymphonyMusicStereoCache *singleton;
	mutable Mutex mutex;
	HashMap<StringName, Entry> cache;
};
