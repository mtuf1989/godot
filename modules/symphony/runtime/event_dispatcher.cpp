#include "event_dispatcher.h"
#include "voice_manager.h"
#include "core/object/class_db.h"
#include "core/os/os.h"

#include <cfloat>
#include <cstdint>

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
	StringName unused_reason;
	return dispatch(p_event, r_result, unused_reason);
}

int SymphonyEventDispatcher::_select_steal_victim(uint64_t p_event_id, int p_incoming_priority, SoundEvent::StealMode p_mode, bool p_same_event_only, StringName &r_reason) const {
	SymphonyVoicePool *pool = SymphonyVoicePool::get_singleton();
	if (!pool) {
		return -1;
	}

	int best_idx = -1;
	uint64_t best_start = UINT64_MAX;
	float best_rms = FLT_MAX;
	float best_dist = -1.0f;
	float best_importance = FLT_MAX;

	Vector3 listener = pool->get_listener_position();

	for (int i = 0; i < pool->get_pool_size(); i++) {
		const SymphonyVoicePool::VoiceSlot *slot = pool->get_slot(i);
		if (!slot) {
			continue;
		}
		if (slot->state != SymphonyVoicePool::VOICE_PLAYING && slot->state != SymphonyVoicePool::VOICE_TO_PLAY) {
			continue;
		}
		if (p_same_event_only) {
			if (slot->event_id != p_event_id) {
				continue;
			}
		} else if (slot->priority > p_incoming_priority) {
			continue;
		}

		bool better = false;
		float dist = listener.distance_to(slot->position);
		float rms_metric = slot->rms_valid ? slot->rms : slot->importance;

		if (best_idx < 0) {
			better = true;
		} else {
			switch (p_mode) {
				case SoundEvent::STEAL_OLDEST:
					if (slot->start_time < best_start) {
						better = true;
					} else if (slot->start_time == best_start) {
						if (slot->importance < best_importance || (slot->importance == best_importance && i < best_idx)) {
							better = true;
						}
					}
					break;
				case SoundEvent::STEAL_QUIETEST:
					if (rms_metric < best_rms) {
						better = true;
					} else if (rms_metric == best_rms) {
						if (slot->importance < best_importance || (slot->importance == best_importance && i < best_idx)) {
							better = true;
						}
					}
					break;
				case SoundEvent::STEAL_FARTHEST:
					if (dist > best_dist) {
						better = true;
					} else if (dist == best_dist) {
						if (slot->importance < best_importance || (slot->importance == best_importance && i < best_idx)) {
							better = true;
						}
					}
					break;
			}
		}

		if (better) {
			best_idx = i;
			best_start = slot->start_time;
			best_rms = rms_metric;
			best_dist = dist;
			best_importance = slot->importance;
		}
	}

	if (best_idx >= 0) {
		switch (p_mode) {
			case SoundEvent::STEAL_OLDEST:
				r_reason = StringName("oldest");
				break;
			case SoundEvent::STEAL_QUIETEST:
				r_reason = StringName("quietest");
				break;
			case SoundEvent::STEAL_FARTHEST:
				r_reason = StringName("farthest");
				break;
		}
	}
	return best_idx;
}

int SymphonyEventDispatcher::dispatch(const Ref<SoundEvent> &p_event, PlayResult &r_result, StringName &r_steal_reason) {
	ERR_FAIL_COND_V(p_event.is_null(), -1);
	r_steal_reason = StringName();

	uint64_t event_id = p_event->get_instance_id();
	uint64_t now = OS::get_singleton()->get_ticks_usec();

	if (p_event->get_streams().size() == 0) {
		r_result = RESULT_REJECTED_NO_STREAMS;
		return -1;
	}

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

	SymphonyVoicePool *pool = SymphonyVoicePool::get_singleton();
	ERR_FAIL_NULL_V(pool, -1);

	const int incoming_priority = p_event->get_priority();
	const int max_voices = p_event->get_max_voices();
	const int current_event_voices = event_voice_counts.has(event_id) ? event_voice_counts[event_id] : 0;
	const bool at_event_cap = max_voices > 0 && current_event_voices >= max_voices;

	int slot = -1;
	bool stole = false;

	if (at_event_cap) {
		slot = _select_steal_victim(event_id, incoming_priority, p_event->get_steal_mode(), true, r_steal_reason);
		if (slot < 0) {
			r_result = RESULT_REJECTED_VOICE_LIMIT;
			return -1;
		}
		SymphonyVoicePool::VoiceSlot *victim = pool->get_slot(slot);
		uint64_t victim_event = victim ? victim->event_id : 0;
		if (victim_event != 0) {
			on_voice_stopped(victim_event);
		}
		pool->reclaim_slot(slot, incoming_priority, r_steal_reason);
		stole = true;
	} else {
		slot = pool->acquire_slot(incoming_priority);
		if (slot < 0) {
			slot = _select_steal_victim(event_id, incoming_priority, p_event->get_steal_mode(), false, r_steal_reason);
			if (slot < 0) {
				r_result = RESULT_REJECTED_VOICE_LIMIT;
				return -1;
			}
			SymphonyVoicePool::VoiceSlot *victim = pool->get_slot(slot);
			uint64_t victim_event = victim ? victim->event_id : 0;
			if (victim_event != 0) {
				on_voice_stopped(victim_event);
			}
			pool->reclaim_slot(slot, incoming_priority, r_steal_reason);
			stole = true;
		}
	}

	cooldown_map[event_id] = now;
	on_voice_started(event_id);

	SymphonyVoicePool::VoiceSlot *vs = pool->get_slot(slot);
	if (vs) {
		vs->event_id = event_id;
		vs->priority = incoming_priority;
		vs->importance = (float)incoming_priority * p_event->get_importance_weight();
		vs->importance_weight = p_event->get_importance_weight();
		vs->category = (int)p_event->get_category();
	}

	r_result = stole ? RESULT_STOLEN : RESULT_PLAYED;
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
	PlayResult pr = RESULT_REJECTED_NO_STREAMS;
	StringName steal_reason;
	int slot = -1;
	int stream_index = -1;
	float volume_offset_db = 0.0f;
	float pitch_scale = 1.0f;

	// Plan §10: resolve/validate the selected stream before reserving or stealing a slot.
	if (p_event.is_valid() && p_event->get_streams().size() > 0) {
		stream_index = resolve_variation(p_event);
		TypedArray<AudioStream> streams = p_event->get_streams();
		if (stream_index < 0 || stream_index >= streams.size() || Variant(streams[stream_index]).get_type() == Variant::NIL) {
			pr = RESULT_REJECTED_NO_STREAMS;
			stream_index = -1;
		} else {
			slot = dispatch(p_event, pr, steal_reason);
			if (slot >= 0) {
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
			} else {
				stream_index = -1;
			}
		}
	}

	result["slot"] = slot;
	result["result"] = (int)pr;
	result["stream_index"] = stream_index;
	result["volume_offset_db"] = volume_offset_db;
	result["pitch_scale"] = pitch_scale;
	result["steal_reason"] = steal_reason;

	SymphonyVoicePool *pool = SymphonyVoicePool::get_singleton();
	if (pool) {
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
		StringName reason = steal_reason;
		switch (pr) {
			case RESULT_PLAYED:
				log_result = SymphonyVoicePool::EVENT_PLAYED;
				break;
			case RESULT_STOLEN:
				log_result = SymphonyVoicePool::EVENT_STOLEN;
				if (reason == StringName()) {
					reason = StringName("stolen");
				}
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
