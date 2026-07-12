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
#ifndef TOOLS_ENABLED
	AudioServer::get_singleton()->add_mix_callback(_mix_callback, this);
#endif
}

SymphonyVoiceManager::~SymphonyVoiceManager() {
#ifndef TOOLS_ENABLED
	AudioServer::get_singleton()->remove_mix_callback(_mix_callback, this);
#endif
	singleton = nullptr;
}

void SymphonyVoiceManager::_mix_callback(void *p_userdata) {
	SymphonyVoiceManager *mgr = static_cast<SymphonyVoiceManager *>(p_userdata);
	mgr->enforce_voice_limits();
}

void SymphonyVoiceManager::register_voice(AudioStreamPlaybackSymphony *p_voice) {
	// Lock-free insert into SafeList. Can be called from any thread.
	active_voices.insert(p_voice);
}

void SymphonyVoiceManager::unregister_voice(AudioStreamPlaybackSymphony *p_voice) {
	// Lock-free erase from SafeList. The node is logically removed immediately
	// but memory is reclaimed later via maybe_cleanup().
	active_voices.erase(p_voice);
}

// --- Lock-free getters: read atomic snapshots ---

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
	// --- Phase 1: Iterate SafeList to compute metrics and snapshot voice state ---
	// SafeList iteration is lock-free. The iterator keeps nodes alive during traversal.

	struct VoiceSnapshot {
		AudioStreamPlaybackSymphony *voice = nullptr;
		ObjectID id;
		float budget = 0.0f;
		float rms = 0.0f;
		int priority = 50;
		float microseconds = 0.0f;
	};

	// Stack-local snapshot array (bounded by a reasonable max).
	static constexpr int32_t MAX_SNAPSHOT = 256;
	VoiceSnapshot snapshots[MAX_SNAPSHOT];
	int32_t voice_count = 0;

	float total_budget = 0.0f;
	float peak_budget = 0.0f;
	float total_microseconds = 0.0f;

	for (auto it = active_voices.begin(); it != active_voices.end(); ++it) {
		if (voice_count >= MAX_SNAPSHOT) {
			break; // Safety cap
		}
		AudioStreamPlaybackSymphony *v = *it;
		if (!v) {
			continue;
		}

		VoiceSnapshot &snap = snapshots[voice_count];
		snap.voice = v;
		snap.id = v->get_instance_id();
		snap.budget = v->get_budget_percent();
		snap.rms = v->get_last_rms();
		snap.priority = v->get_effective_priority();
		snap.microseconds = v->get_voice_cpu_microseconds();

		total_budget += snap.budget;
		if (snap.budget > peak_budget) {
			peak_budget = snap.budget;
		}
		total_microseconds += snap.microseconds;

		voice_count++;
	}

	float avg_microseconds = (voice_count > 0) ? (total_microseconds / voice_count) : 0.0f;

	// --- Phase 2: Identify victims for voice stealing ---
	Vector<ObjectID> victim_ids;

	// Voice count limit
	if (max_voices > 0 && voice_count > max_voices) {
		// Mark lowest-priority, quietest voices as victims until we're at the limit.
		// Use a simple repeated scan (voice counts are small, typically < 64).
		while (voice_count - (int32_t)victim_ids.size() > max_voices) {
			int worst_idx = -1;
			int worst_priority = INT32_MAX;
			float worst_rms = FLT_MAX;

			for (int32_t i = 0; i < voice_count; i++) {
				// Skip already-identified victims
				bool is_victim = false;
				for (int vi = 0; vi < victim_ids.size(); vi++) {
					if (snapshots[i].id == victim_ids[vi]) {
						is_victim = true;
						break;
					}
				}
				if (is_victim) {
					continue;
				}

				if (snapshots[i].priority < worst_priority ||
						(snapshots[i].priority == worst_priority && snapshots[i].rms < worst_rms)) {
					worst_priority = snapshots[i].priority;
					worst_rms = snapshots[i].rms;
					worst_idx = i;
				}
			}

			if (worst_idx >= 0) {
				victim_ids.push_back(snapshots[worst_idx].id);
			} else {
				break;
			}
		}
	}

	// Budget critical threshold: steal more voices if over budget.
	float remaining_budget = 0.0f;
	for (int32_t i = 0; i < voice_count; i++) {
		bool is_victim = false;
		for (int vi = 0; vi < victim_ids.size(); vi++) {
			if (snapshots[i].id == victim_ids[vi]) {
				is_victim = true;
				break;
			}
		}
		if (!is_victim) {
			remaining_budget += snapshots[i].budget;
		}
	}

	if (remaining_budget > critical_threshold * 100.0f) {
		while (remaining_budget > warning_threshold * 100.0f) {
			int worst_idx = -1;
			int worst_priority = INT32_MAX;
			float worst_rms = FLT_MAX;

			for (int32_t i = 0; i < voice_count; i++) {
				bool is_victim = false;
				for (int vi = 0; vi < victim_ids.size(); vi++) {
					if (snapshots[i].id == victim_ids[vi]) {
						is_victim = true;
						break;
					}
				}
				if (is_victim) {
					continue;
				}

				if (snapshots[i].priority < worst_priority ||
						(snapshots[i].priority == worst_priority && snapshots[i].rms < worst_rms)) {
					worst_priority = snapshots[i].priority;
					worst_rms = snapshots[i].rms;
					worst_idx = i;
				}
			}

			if (worst_idx >= 0) {
				remaining_budget -= snapshots[worst_idx].budget;
				victim_ids.push_back(snapshots[worst_idx].id);
			} else {
				break;
			}
		}
	} else if (remaining_budget > warning_threshold * 100.0f) {
		WARN_PRINT_ONCE("Symphony: Voice budget exceeds warning threshold.");
	}

	// --- Phase 3: Publish metrics atomically ---
	metric_active_count.store(voice_count, std::memory_order_relaxed);
	metric_stolen_this_frame.store(victim_ids.size(), std::memory_order_relaxed);
	metric_total_budget_millipercent.store(static_cast<int32_t>(total_budget * 1000.0f), std::memory_order_relaxed);
	metric_peak_budget_millipercent.store(static_cast<int32_t>(peak_budget * 1000.0f), std::memory_order_relaxed);
	metric_avg_voice_microseconds_x1000.store(static_cast<int32_t>(avg_microseconds * 1000.0f), std::memory_order_relaxed);

	// --- Phase 4: Stop victims via safe ObjectID lookup ---
	for (int i = 0; i < victim_ids.size(); i++) {
		Object *obj = ObjectDB::get_instance(victim_ids[i]);
		if (obj) {
			AudioStreamPlaybackSymphony *v = Object::cast_to<AudioStreamPlaybackSymphony>(obj);
			if (v) {
				v->stop();
			}
		}
	}

	// --- Phase 5: Deferred cleanup of erased SafeList nodes ---
	// Safe to call here because we are past the iteration (iterator destructed above).
	active_voices.maybe_cleanup();
}
