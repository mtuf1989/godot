#pragma once

#include "core/object/object.h"
#include "core/object/class_db.h"
#include "sound_event.h"

class SymphonyVoicePool;

// Processes play requests: cooldown, voice limiting, variation selection.
// Returns a voice slot index or -1 if rejected.
class SymphonyEventDispatcher : public Object {
	GDCLASS(SymphonyEventDispatcher, Object);

public:
	enum PlayResult {
		RESULT_PLAYED = 0,
		RESULT_STOLEN,
		RESULT_REJECTED_COOLDOWN,
		RESULT_REJECTED_VOICE_LIMIT,
		RESULT_REJECTED_NO_STREAMS,
	};

private:
	static SymphonyEventDispatcher *singleton;

	// Cooldown tracking: event resource ID → last play time (usec)
	HashMap<uint64_t, uint64_t> cooldown_map;

	// Per-event voice count: event resource ID → active count
	HashMap<uint64_t, int> event_voice_counts;

	// Variation state: event resource ID → next sequence index
	HashMap<uint64_t, int> sequence_indices;

	// Shuffle state: event resource ID → shuffled index order
	HashMap<uint64_t, Vector<int>> shuffle_orders;

protected:
	static void _bind_methods();

public:
	static SymphonyEventDispatcher *get_singleton() { return singleton; }

	// Main entry point. Returns voice slot index or -1.
	// p_result is set to the reason for success/failure.
	int dispatch(const Ref<SoundEvent> &p_event, PlayResult &r_result);

	// Resolve which stream index to play (variation logic)
	int resolve_variation(const Ref<SoundEvent> &p_event);

	// Track voice start/stop for per-event limiting
	void on_voice_started(uint64_t p_event_id);
	void on_voice_stopped(uint64_t p_event_id);

	// GDScript-friendly dispatch that returns Dictionary {slot, result, stream_index}
	Dictionary play_event(const Ref<SoundEvent> &p_event);

	SymphonyEventDispatcher();
	~SymphonyEventDispatcher();
};

VARIANT_ENUM_CAST(SymphonyEventDispatcher::PlayResult);
