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

	if (slot >= 0) {
		stream_index = resolve_variation(p_event);
	}

	result["slot"] = slot;
	result["result"] = (int)pr;
	result["stream_index"] = stream_index;
	return result;
}
