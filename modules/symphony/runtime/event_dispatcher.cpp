#include "event_dispatcher.h"
#include "voice_manager.h"
#include "core/object/class_db.h"
#include "core/os/os.h"

SymphonyEventDispatcher *SymphonyEventDispatcher::singleton = nullptr;

SymphonyEventDispatcher::SymphonyEventDispatcher() {
	singleton = this;
	rng.seed(OS::get_singleton()->get_ticks_usec());
}

SymphonyEventDispatcher::~SymphonyEventDispatcher() {
	singleton = nullptr;
}

void SymphonyEventDispatcher::_bind_methods() {
	ClassDB::bind_method(D_METHOD("play_event", "event"), &SymphonyEventDispatcher::play_event);
	ClassDB::bind_method(D_METHOD("on_voice_started", "event_id"), &SymphonyEventDispatcher::on_voice_started);
	ClassDB::bind_method(D_METHOD("on_voice_stopped", "event_id"), &SymphonyEventDispatcher::on_voice_stopped);

	BIND_ENUM_CONSTANT(RESULT_PLAYED);
	BIND_ENUM_CONSTANT(RESULT_STOLEN);
	BIND_ENUM_CONSTANT(RESULT_REJECTED_COOLDOWN);
	BIND_ENUM_CONSTANT(RESULT_REJECTED_VOICE_LIMIT);
	BIND_ENUM_CONSTANT(RESULT_REJECTED_NO_STREAMS);
}

int SymphonyEventDispatcher::dispatch(const Ref<SoundEvent> &p_event, PlayResult &r_result) {
	ERR_FAIL_COND_V(p_event.is_null(), -1);

	// --- instance_id Sharing Semantics ---
	// Cooldown, voice limiting, and variation state are keyed by the SoundEvent's
	// Object instance_id. This means:
	//
	// • All scripts referencing the same loaded .tres resource SHARE cooldown and
	//   voice limits (Godot's resource cache returns one instance per path).
	//   This is the standard middleware behavior (Wwise/FMOD global event limiting).
	//
	// • Calling event.duplicate() creates a NEW instance_id → independent cooldown
	//   and voice limits. Use this when per-emitter limiting is needed.
	//
	// • Dynamically created SoundEvent.new() instances each get unique instance_ids
	//   even if configured identically — they won't share cooldown.
	//
	// • Variation sequence/shuffle state is also per-instance_id, so all callers
	//   sharing a .tres advance the same sequence counter.
	uint64_t event_id = p_event->get_instance_id();
	uint64_t now = OS::get_singleton()->get_ticks_usec();

	// Check: no streams
	if (p_event->get_streams().size() == 0) {
		r_result = RESULT_REJECTED_NO_STREAMS;
		return -1;
	}

	// Check: cooldown
	float cooldown_ms = p_event->get_cooldown_ms();
	if (cooldown_ms > 0.0f) {
		if (cooldown_map.has(event_id)) {
			uint64_t elapsed_us = now - cooldown_map[event_id];
			if (elapsed_us < (uint64_t)(cooldown_ms * 1000.0f)) {
				r_result = RESULT_REJECTED_COOLDOWN;
				return -1;
			}
		}
	}

	// Check: per-event voice limit
	int max_voices = p_event->get_max_voices();
	if (max_voices > 0) {
		int current = event_voice_counts.has(event_id) ? event_voice_counts[event_id] : 0;
		if (current >= max_voices) {
			r_result = RESULT_REJECTED_VOICE_LIMIT;
			return -1;
		}
	}

	// Acquire voice slot
	SymphonyVoicePool *pool = SymphonyVoicePool::get_singleton();
	ERR_FAIL_NULL_V(pool, -1);

	int slot = pool->acquire_slot(p_event->get_priority());
	if (slot < 0) {
		r_result = RESULT_REJECTED_VOICE_LIMIT;
		return -1;
	}

	// Success — update tracking
	cooldown_map[event_id] = now;
	on_voice_started(event_id);

	// Set slot metadata
	SymphonyVoicePool::VoiceSlot *vs = pool->get_slot(slot);
	if (vs) {
		vs->event_id = event_id;
		vs->priority = p_event->get_priority();
		vs->importance = (float)p_event->get_priority();
	}

	r_result = RESULT_PLAYED;
	return slot;
}

int SymphonyEventDispatcher::resolve_variation(const Ref<SoundEvent> &p_event) {
	ERR_FAIL_COND_V(p_event.is_null(), 0);

	int count = p_event->get_streams().size();
	if (count <= 1) {
		return 0;
	}

	uint64_t event_id = p_event->get_instance_id();

	switch (p_event->get_variation_mode()) {
		case SoundEvent::VARIATION_RANDOM:
			return rng.rand() % count;

		case SoundEvent::VARIATION_SEQUENCE: {
			int idx = sequence_indices.has(event_id) ? sequence_indices[event_id] : 0;
			sequence_indices[event_id] = (idx + 1) % count;
			return idx;
		}

		case SoundEvent::VARIATION_SHUFFLE: {
			if (!shuffle_orders.has(event_id) || shuffle_orders[event_id].is_empty()) {
				Vector<int> order;
				order.resize(count);
				for (int i = 0; i < count; i++) {
					order.write[i] = i;
				}
				// Fisher-Yates shuffle
				for (int i = count - 1; i > 0; i--) {
					int j = rng.rand() % (i + 1);
					SWAP(order.write[i], order.write[j]);
				}
				shuffle_orders[event_id] = order;
			}
			Vector<int> &order = shuffle_orders[event_id];
			int idx = order[order.size() - 1];
			order.resize(order.size() - 1);
			return idx;
		}
	}
	return 0;
}

void SymphonyEventDispatcher::on_voice_started(uint64_t p_event_id) {
	if (event_voice_counts.has(p_event_id)) {
		event_voice_counts[p_event_id]++;
	} else {
		event_voice_counts[p_event_id] = 1;
	}
}

void SymphonyEventDispatcher::on_voice_stopped(uint64_t p_event_id) {
	if (event_voice_counts.has(p_event_id)) {
		event_voice_counts[p_event_id]--;
		if (event_voice_counts[p_event_id] <= 0) {
			event_voice_counts.erase(p_event_id);
		}
	}
}

Dictionary SymphonyEventDispatcher::play_event(const Ref<SoundEvent> &p_event) {
	Dictionary result;
	PlayResult pr;
	int slot = dispatch(p_event, pr);
	int stream_index = -1;
	float volume_offset_db = 0.0f;
	float pitch_scale = 1.0f;

	if (slot >= 0) {
		stream_index = resolve_variation(p_event);

		// Item 27: Validate resolved stream is non-null before returning success.
		// A SoundEvent may have non-empty streams array but contain null entries
		// (e.g., removed AudioStream references in .tres). Reject here rather than
		// letting the game layer crash when assigning null to a player.
		TypedArray<AudioStream> streams = p_event->get_streams();
		if (stream_index < 0 || stream_index >= streams.size() || Variant(streams[stream_index]).get_type() == Variant::NIL) {
			// Release the slot we just acquired — stream is invalid
			SymphonyVoicePool *pool = SymphonyVoicePool::get_singleton();
			if (pool) {
				pool->release_slot(slot, true);
			}
			on_voice_stopped(p_event->get_instance_id());
			pr = RESULT_REJECTED_NO_STREAMS;
			slot = -1;
			stream_index = -1;
		} else {
			// Compute per-instance randomized offsets (Wwise/FMOD additive model):
			// - volume_offset_db: random dB offset from volume_range (additive in dB domain)
			// - pitch_scale: random multiplier from pitch_range
			// The game layer combines these with RTPC-driven offsets and bus volume.
			Vector2 vol_range = p_event->get_volume_range();
			if (vol_range.x != vol_range.y) {
				volume_offset_db = vol_range.x + rng.randf() * (vol_range.y - vol_range.x);
			} else {
				volume_offset_db = vol_range.x;
			}

			Vector2 pit_range = p_event->get_pitch_range();
			if (pit_range.x != pit_range.y) {
				pitch_scale = pit_range.x + rng.randf() * (pit_range.y - pit_range.x);
			} else {
				pitch_scale = pit_range.x;
			}
		}
	}

	result["slot"] = slot;
	result["result"] = (int)pr;
	result["stream_index"] = stream_index;
	result["volume_offset_db"] = volume_offset_db;
	result["pitch_scale"] = pitch_scale;

	// Log the event to the ring buffer
	SymphonyVoicePool *pool = SymphonyVoicePool::get_singleton();
	if (pool) {
		// Item 26: Generate a meaningful fallback name for dynamic Resources.
		// get_path() is empty for Resources created at runtime (SoundEvent.new()).
		// get_name() is also empty unless explicitly set by the user.
		// Fallback: "SoundEvent#<instance_id>" to keep event logs debuggable.
		StringName event_name;
		if (p_event.is_valid()) {
			String path = p_event->get_path().get_file();
			if (!path.is_empty()) {
				event_name = StringName(path);
			} else if (!String(p_event->get_name()).is_empty()) {
				event_name = p_event->get_name();
			} else {
				event_name = StringName("SoundEvent#" + itos(p_event->get_instance_id()));
			}
		}
		float importance = (slot >= 0 && pool->get_slot(slot)) ? pool->get_slot(slot)->importance : 0.0f;

		SymphonyVoicePool::EventResult log_result;
		StringName reason;
		switch (pr) {
			case RESULT_PLAYED:
				log_result = SymphonyVoicePool::EVENT_PLAYED;
				break;
			case RESULT_STOLEN:
				log_result = SymphonyVoicePool::EVENT_STOLEN;
				reason = StringName("lowest_importance");
				break;
			case RESULT_REJECTED_COOLDOWN:
				log_result = SymphonyVoicePool::EVENT_REJECTED_COOLDOWN;
				reason = StringName("cooldown");
				break;
			case RESULT_REJECTED_VOICE_LIMIT:
				log_result = SymphonyVoicePool::EVENT_REJECTED_VOICE_LIMIT;
				reason = StringName("voice_limit");
				break;
			case RESULT_REJECTED_NO_STREAMS:
				log_result = SymphonyVoicePool::EVENT_REJECTED_NO_STREAMS;
				reason = StringName("no_streams");
				break;
			default:
				log_result = SymphonyVoicePool::EVENT_PLAYED;
				break;
		}
		pool->log_event(event_name, log_result, slot, importance, reason);
	}

	return result;
}
