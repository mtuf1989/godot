#pragma once

#include "core/object/object.h"
#include "core/object/class_db.h"
#include "core/math/random_pcg.h"
#include "core/math/vector3.h"
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

	// Thread-safe random number generator (not relying on global rand())
	RandomPCG rng;

protected:
	static void _bind_methods();

public:
	static SymphonyEventDispatcher *get_singleton() { return singleton; }

	// Main entry point. Returns voice slot index or -1.
	// p_result is set to the reason for success/failure.
	int dispatch(const Ref<SoundEvent> &p_event, PlayResult &r_result);
	// Same as dispatch, but also returns the steal reason string for logging.
	int dispatch(const Ref<SoundEvent> &p_event, PlayResult &r_result, StringName &r_steal_reason);

	// Resolve which stream index to play (variation logic)
	int resolve_variation(const Ref<SoundEvent> &p_event);

	// Track voice start/stop for per-event limiting
	void on_voice_started(uint64_t p_event_id);
	void on_voice_stopped(uint64_t p_event_id);

	// GDScript-friendly dispatch that returns Dictionary {slot, result, stream_index, steal_reason, delay_s}
	// When p_has_position is true (3D path), the propagation delay for one-shots is
	// computed from the distance between p_source_position and the VoicePool listener,
	// set on the acquired slot (deferred start), and returned as "delay_s".
	// p_victim_slot >= 0 reclaims that slot instead of acquiring or auto-stealing.
	// Cooldown and voice counts change only after the reclaim succeeds.
	Dictionary play_event(const Ref<SoundEvent> &p_event, const Vector3 &p_source_position = Vector3(), bool p_has_position = false, int p_victim_slot = -1);

	SymphonyEventDispatcher();
	~SymphonyEventDispatcher();

private:
	// Select a steal victim.
	// same_event: only slots for p_event_id.
	// category_filter >= 0: only that SoundEvent::Category.
	// Otherwise only slots with priority <= incoming and tier <= incoming
	// (VITAL is never stolen by a lower tier — P8a).
	int _select_steal_victim(uint64_t p_event_id, int p_incoming_priority, int p_incoming_tier, SoundEvent::StealMode p_mode, bool p_same_event_only, int p_category_filter, StringName &r_reason) const;
	[[nodiscard]] static int _category_cap(int p_category);
	[[nodiscard]] int _count_category_voices(int p_category) const;
	// Shared admission: cooldown, event max_voices, category cap, acquire/steal, or forced victim (N8).
	int _admit(const Ref<SoundEvent> &p_event, int p_forced_victim, PlayResult &r_result, StringName &r_steal_reason);
};

VARIANT_ENUM_CAST(SymphonyEventDispatcher::PlayResult);
