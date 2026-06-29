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

int32_t SymphonyVoiceManager::get_active_voice_count() const {
	std::lock_guard<std::mutex> lock(registry_mutex);
	return active_voices.size();
}

float SymphonyVoiceManager::get_total_budget_percent() const {
	std::lock_guard<std::mutex> lock(registry_mutex);
	float total = 0.0f;
	for (const AudioStreamPlaybackSymphony *v : active_voices) {
		total += v->get_budget_percent();
	}
	return total;
}

float SymphonyVoiceManager::get_peak_budget_percent() const {
	std::lock_guard<std::mutex> lock(registry_mutex);
	float peak = 0.0f;
	for (const AudioStreamPlaybackSymphony *v : active_voices) {
		float b = v->get_budget_percent();
		if (b > peak) {
			peak = b;
		}
	}
	return peak;
}

float SymphonyVoiceManager::get_average_voice_microseconds() const {
	std::lock_guard<std::mutex> lock(registry_mutex);
	if (active_voices.is_empty()) {
		return 0.0f;
	}
	float total = 0.0f;
	for (const AudioStreamPlaybackSymphony *v : active_voices) {
		total += v->get_voice_cpu_microseconds();
	}
	return total / active_voices.size();
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
		std::lock_guard<std::mutex> lock(registry_mutex);

		// Voice count limit
		if (max_voices > 0 && active_voices.size() > max_voices) {
			while (active_voices.size() - victim_ids.size() > max_voices) {
				int worst_idx = -1;
				int worst_priority = INT32_MAX;
				float worst_rms = FLT_MAX;

				for (int i = 0; i < active_voices.size(); i++) {
					bool is_victim = false;
					for (int v = 0; v < victim_ids.size(); v++) {
						if (active_voices[i]->get_instance_id() == victim_ids[v]) {
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

		// Budget critical threshold
		float total_budget = 0.0f;
		for (const AudioStreamPlaybackSymphony *v : active_voices) {
			bool is_victim = false;
			for (int vi = 0; vi < victim_ids.size(); vi++) {
				if (v->get_instance_id() == victim_ids[vi]) {
					is_victim = true;
					break;
				}
			}
			if (is_victim) {
				continue;
			}
			total_budget += v->get_budget_percent();
		}

		if (total_budget > critical_threshold * 100.0f) {
			while (total_budget > warning_threshold * 100.0f && !active_voices.is_empty()) {
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
					total_budget -= active_voices[worst_idx]->get_budget_percent();
					victim_ids.push_back(active_voices[worst_idx]->get_instance_id());
				} else {
					break;
				}
			}
		} else if (total_budget > warning_threshold * 100.0f) {
			WARN_PRINT_ONCE("Symphony: Voice budget exceeds warning threshold.");
		}
	} // mutex released here

	// Stop victims outside the lock — use ObjectID for safe lookup
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
