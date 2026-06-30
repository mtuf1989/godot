#include "symphony_voice_manager.h"
#include "../stream/audio_stream_playback_symphony.h"
#include "core/object/class_db.h"
#include "core/object/object.h"
#include "servers/audio/audio_server.h"

#include <cfloat>

SymphonyVoiceManager *SymphonyVoiceManager::singleton = nullptr;

void SymphonyVoiceManager::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_active_voice_count"), &SymphonyVoiceManager::get_active_voice_count);
	ClassDB::bind_method(D_METHOD("get_total_budget_percent"), &SymphonyVoiceManager::get_total_budget_percent);
	ClassDB::bind_method(D_METHOD("get_peak_budget_percent"), &SymphonyVoiceManager::get_peak_budget_percent);
	ClassDB::bind_method(D_METHOD("get_average_voice_microseconds"), &SymphonyVoiceManager::get_average_voice_microseconds);
	ClassDB::bind_method(D_METHOD("get_stolen_this_frame"), &SymphonyVoiceManager::get_stolen_this_frame);
	ClassDB::bind_method(D_METHOD("set_max_voices", "max"), &SymphonyVoiceManager::set_max_voices);
	ClassDB::bind_method(D_METHOD("get_max_voices"), &SymphonyVoiceManager::get_max_voices);
	ClassDB::bind_method(D_METHOD("set_warning_threshold", "threshold"), &SymphonyVoiceManager::set_warning_threshold);
	ClassDB::bind_method(D_METHOD("get_warning_threshold"), &SymphonyVoiceManager::get_warning_threshold);
	ClassDB::bind_method(D_METHOD("set_critical_threshold", "threshold"), &SymphonyVoiceManager::set_critical_threshold);
	ClassDB::bind_method(D_METHOD("get_critical_threshold"), &SymphonyVoiceManager::get_critical_threshold);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_voices"), "set_max_voices", "get_max_voices");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "warning_threshold"), "set_warning_threshold", "get_warning_threshold");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "critical_threshold"), "set_critical_threshold", "get_critical_threshold");
}

SymphonyVoiceManager::SymphonyVoiceManager() {
	singleton = this;
	AudioServer::get_singleton()->add_mix_callback(_mix_callback, this);
}

SymphonyVoiceManager::~SymphonyVoiceManager() {
	AudioServer::get_singleton()->remove_mix_callback(_mix_callback, this);
	singleton = nullptr;
}

void SymphonyVoiceManager::_mix_callback(void *p_userdata) {
	SymphonyVoiceManager *mgr = static_cast<SymphonyVoiceManager *>(p_userdata);
	mgr->enforce_voice_limits();
}

void SymphonyVoiceManager::register_voice(AudioStreamPlaybackSymphony *p_voice) {
	std::lock_guard<std::mutex> lock(registry_mutex);
	active_voices.push_back(p_voice);
}

void SymphonyVoiceManager::unregister_voice(AudioStreamPlaybackSymphony *p_voice) {
	std::lock_guard<std::mutex> lock(registry_mutex);
	int idx = active_voices.find(p_voice);
	if (idx >= 0) {
		active_voices.remove_at(idx);
	}
}

// --- Lock-free getters: read atomic snapshots, NO mutex ---

int32_t SymphonyVoiceManager::get_active_voice_count() const {
	return metric_active_count.load(std::memory_order_relaxed);
}

float SymphonyVoiceManager::get_total_budget_percent() const {
	return static_cast<float>(metric_total_budget_millipercent.load(std::memory_order_relaxed)) / 1000.0f;
}

float SymphonyVoiceManager::get_peak_budget_percent() const {
	return static_cast<float>(metric_peak_budget_millipercent.load(std::memory_order_relaxed)) / 1000.0f;
}

float SymphonyVoiceManager::get_average_voice_microseconds() const {
	return static_cast<float>(metric_avg_voice_microseconds_x1000.load(std::memory_order_relaxed)) / 1000.0f;
}

int32_t SymphonyVoiceManager::get_stolen_this_frame() const {
	return metric_stolen_this_frame.load(std::memory_order_relaxed);
}

void SymphonyVoiceManager::set_max_voices(int32_t p_max) {
	max_voices = p_max;
}

int32_t SymphonyVoiceManager::get_max_voices() const {
	return max_voices;
}

void SymphonyVoiceManager::set_warning_threshold(float p_threshold) {
	warning_threshold = p_threshold;
}

float SymphonyVoiceManager::get_warning_threshold() const {
	return warning_threshold;
}

void SymphonyVoiceManager::set_critical_threshold(float p_threshold) {
	critical_threshold = p_threshold;
}

float SymphonyVoiceManager::get_critical_threshold() const {
	return critical_threshold;
}

void SymphonyVoiceManager::enforce_voice_limits() {
	Vector<ObjectID> victim_ids;

	{
		// Acquire mutex briefly to snapshot the voice list and compute metrics.
		// Main-thread register/unregister is infrequent, so contention is minimal.
		std::lock_guard<std::mutex> lock(registry_mutex);

		const int32_t voice_count = active_voices.size();

		// --- Compute metrics while we hold the lock ---
		float total_budget = 0.0f;
		float peak_budget = 0.0f;
		float total_microseconds = 0.0f;

		for (const AudioStreamPlaybackSymphony *v : active_voices) {
			float b = v->get_budget_percent();
			total_budget += b;
			if (b > peak_budget) {
				peak_budget = b;
			}
			total_microseconds += v->get_voice_cpu_microseconds();
		}

		float avg_microseconds = (voice_count > 0) ? (total_microseconds / voice_count) : 0.0f;

		// --- Voice count limit: identify victims ---
		if (max_voices > 0 && voice_count > max_voices) {
			while (static_cast<int32_t>(active_voices.size()) - victim_ids.size() > max_voices) {
				int worst_idx = -1;
				int worst_priority = INT32_MAX;
				float worst_rms = FLT_MAX;

				for (int i = 0; i < active_voices.size(); i++) {
					bool is_victim = false;
					for (int vi = 0; vi < victim_ids.size(); vi++) {
						if (active_voices[i]->get_instance_id() == victim_ids[vi]) {
							is_victim = true;
							break;
						}
					}
					if (is_victim) {
						continue;
					}
					int pri = active_voices[i]->get_effective_priority();
					float rms = active_voices[i]->get_last_rms();
					if (pri < worst_priority || (pri == worst_priority && rms < worst_rms)) {
						worst_priority = pri;
						worst_rms = rms;
						worst_idx = i;
					}
				}

				if (worst_idx >= 0) {
					victim_ids.push_back(active_voices[worst_idx]->get_instance_id());
				} else {
					break;
				}
			}
		}

		// --- Budget critical threshold: identify more victims ---
		// Recompute budget excluding already-identified victims.
		float remaining_budget = 0.0f;
		for (const AudioStreamPlaybackSymphony *v : active_voices) {
			bool is_victim = false;
			for (int vi = 0; vi < victim_ids.size(); vi++) {
				if (v->get_instance_id() == victim_ids[vi]) {
					is_victim = true;
					break;
				}
			}
			if (!is_victim) {
				remaining_budget += v->get_budget_percent();
			}
		}

		if (remaining_budget > critical_threshold * 100.0f) {
			while (remaining_budget > warning_threshold * 100.0f && !active_voices.is_empty()) {
				int worst_idx = -1;
				int worst_priority = INT32_MAX;
				float worst_rms = FLT_MAX;

				for (int i = 0; i < active_voices.size(); i++) {
					bool is_victim = false;
					for (int vi = 0; vi < victim_ids.size(); vi++) {
						if (active_voices[i]->get_instance_id() == victim_ids[vi]) {
							is_victim = true;
							break;
						}
					}
					if (is_victim) {
						continue;
					}
					int pri = active_voices[i]->get_effective_priority();
					float rms = active_voices[i]->get_last_rms();
					if (pri < worst_priority || (pri == worst_priority && rms < worst_rms)) {
						worst_priority = pri;
						worst_rms = rms;
						worst_idx = i;
					}
				}

				if (worst_idx >= 0) {
					remaining_budget -= active_voices[worst_idx]->get_budget_percent();
					victim_ids.push_back(active_voices[worst_idx]->get_instance_id());
				} else {
					break;
				}
			}
		} else if (remaining_budget > warning_threshold * 100.0f) {
			WARN_PRINT_ONCE("Symphony: Voice budget exceeds warning threshold.");
		}

		// --- Publish metrics atomically (relaxed stores — no ordering needed for diagnostics) ---
		metric_active_count.store(voice_count, std::memory_order_relaxed);
		metric_stolen_this_frame.store(victim_ids.size(), std::memory_order_relaxed);
		metric_total_budget_millipercent.store(static_cast<int32_t>(total_budget * 1000.0f), std::memory_order_relaxed);
		metric_peak_budget_millipercent.store(static_cast<int32_t>(peak_budget * 1000.0f), std::memory_order_relaxed);
		metric_avg_voice_microseconds_x1000.store(static_cast<int32_t>(avg_microseconds * 1000.0f), std::memory_order_relaxed);
	} // mutex released here

	// Stop victims outside the lock — use ObjectID for safe lookup.
	for (int i = 0; i < victim_ids.size(); i++) {
		Object *obj = ObjectDB::get_instance(victim_ids[i]);
		if (obj) {
			AudioStreamPlaybackSymphony *v = Object::cast_to<AudioStreamPlaybackSymphony>(obj);
			if (v) {
				v->stop();
			}
		}
	}
}
