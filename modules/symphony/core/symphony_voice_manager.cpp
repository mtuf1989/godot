#include "symphony_voice_manager.h"
#include "../stream/audio_stream_playback_symphony.h"
#include "symphony_trigger.h"
#include "core/object/class_db.h"
#include "core/object/object.h"
#include "servers/audio/audio_server.h"

#include <cfloat>
#include <climits>

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
	ClassDB::bind_method(D_METHOD("process_deferred_lod"), &SymphonyVoiceManager::process_deferred_lod);
	ClassDB::bind_method(D_METHOD("get_dropped_trigger_count"), &SymphonyVoiceManager::get_dropped_trigger_count);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_voices"), "set_max_voices", "get_max_voices");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "warning_threshold"), "set_warning_threshold", "get_warning_threshold");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "critical_threshold"), "set_critical_threshold", "get_critical_threshold");
}

SymphonyVoiceManager::SymphonyVoiceManager() {
	singleton = this;
#ifndef TOOLS_ENABLED
	if (AudioServer::get_singleton()) {
		AudioServer::get_singleton()->add_mix_callback(_mix_callback, this);
	}
#endif
	if (AudioServer::get_singleton()) {
		AudioServer::get_singleton()->add_update_callback(_update_callback, this);
		update_callback_registered = true;
	}
}

SymphonyVoiceManager::~SymphonyVoiceManager() {
#ifndef TOOLS_ENABLED
	if (AudioServer::get_singleton()) {
		AudioServer::get_singleton()->remove_mix_callback(_mix_callback, this);
	}
#endif
	if (update_callback_registered && AudioServer::get_singleton()) {
		AudioServer::get_singleton()->remove_update_callback(_update_callback, this);
		update_callback_registered = false;
	}
	singleton = nullptr;
}

void SymphonyVoiceManager::_mix_callback(void *p_userdata) {
	SymphonyVoiceManager *mgr = static_cast<SymphonyVoiceManager *>(p_userdata);
	mgr->enforce_voice_limits();
}

void SymphonyVoiceManager::_update_callback(void *p_userdata) {
	SymphonyVoiceManager *mgr = static_cast<SymphonyVoiceManager *>(p_userdata);
	mgr->process_deferred_lod();
}

void SymphonyVoiceManager::register_voice(AudioStreamPlaybackSymphony *p_voice) {
	active_voices.insert(p_voice);
}

void SymphonyVoiceManager::unregister_voice(AudioStreamPlaybackSymphony *p_voice) {
	active_voices.erase(p_voice);
}

void SymphonyVoiceManager::process_deferred_lod() {
	// Main thread: apply per-voice stop/LOD atomics written by the audio callback.
	// At most one LOD compilation per update to avoid allocation bursts (plan §5).
	bool lod_compiled = false;

	for (auto it = active_voices.begin(); it != active_voices.end(); ++it) {
		AudioStreamPlaybackSymphony *v = *it;
		if (!v) {
			continue;
		}
		if (v->manager_stop_request.load(std::memory_order_relaxed)) {
			v->process_manager_requests();
			continue;
		}
		if (!lod_compiled && v->requested_lod_tier.load(std::memory_order_relaxed) >= 0) {
			v->process_manager_requests();
			lod_compiled = true;
		}
	}

	active_voices.maybe_cleanup();
}

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

uint64_t SymphonyVoiceManager::get_dropped_trigger_count() const {
	return symphony_dropped_trigger_count().load(std::memory_order_relaxed);
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

bool SymphonyVoiceManager::try_acquire_crossfade_token() {
	int32_t available = crossfade_tokens.load(std::memory_order_relaxed);
	do {
		if (available <= 0) {
			return false;
		}
	} while (!crossfade_tokens.compare_exchange_weak(available, available - 1, std::memory_order_acq_rel, std::memory_order_relaxed));
	return true;
}

void SymphonyVoiceManager::release_crossfade_token() {
	crossfade_tokens.fetch_add(1, std::memory_order_relaxed);
}

int32_t SymphonyVoiceManager::get_crossfade_tokens_available() const {
	return crossfade_tokens.load(std::memory_order_relaxed);
}

float SymphonyVoiceManager::estimate_cpu_fraction_for_cost(float p_cost_units, float p_mix_rate, int p_frames) const {
	if (p_cost_units <= 0.0f || p_mix_rate <= 0.0f || p_frames <= 0) {
		return 0.0f;
	}
	const float us_per_unit = (float)metric_us_per_cost_x1000.load(std::memory_order_relaxed) / 1000.0f;
	const float estimated_us = p_cost_units * MAX(us_per_unit, 0.001f);
	const float budget_us = ((float)p_frames / p_mix_rate) * 1.0e6f;
	if (budget_us <= 0.0f) {
		return 0.0f;
	}
	return estimated_us / budget_us;
}

void SymphonyVoiceManager::observe_cost_sample(float p_cost_units, float p_mix_us) {
	if (p_cost_units < 1.0f || p_mix_us <= 0.0f) {
		return;
	}
	const float sample = p_mix_us / p_cost_units;
	const int32_t sample_x1000 = (int32_t)CLAMP(sample * 1000.0f, 1.0f, 1000000.0f);
	int32_t cur = metric_us_per_cost_x1000.load(std::memory_order_relaxed);
	// EWMA α ≈ 0.1
	int32_t next = cur + (sample_x1000 - cur) / 10;
	if (next < 1) {
		next = 1;
	}
	metric_us_per_cost_x1000.store(next, std::memory_order_relaxed);
}

void SymphonyVoiceManager::enforce_voice_limits() {
	// Audio thread only: fixed stack snapshots + per-voice atomics. No ObjectDB,
	// heap containers, Resource access, or graph compilation (plan §6).

	struct VoiceSnapshot {
		AudioStreamPlaybackSymphony *voice = nullptr;
		float budget = 0.0f;
		float rms = 0.0f;
		int priority = 50;
		float microseconds = 0.0f;
		int current_lod = 0;
		int max_lod = 0;
		bool transitioning = false;
	};

	static constexpr int32_t MAX_SNAPSHOT = 256;
	VoiceSnapshot snapshots[MAX_SNAPSHOT];
	int32_t voice_count = 0;

	float total_budget = 0.0f;
	float peak_budget = 0.0f;
	float total_microseconds = 0.0f;

	for (auto it = active_voices.begin(); it != active_voices.end(); ++it) {
		if (voice_count >= MAX_SNAPSHOT) {
			break;
		}
		AudioStreamPlaybackSymphony *v = *it;
		if (!v) {
			continue;
		}

		VoiceSnapshot &snap = snapshots[voice_count];
		snap.voice = v;
		snap.budget = v->get_budget_percent();
		snap.rms = v->get_last_rms();
		snap.priority = v->get_effective_priority();
		snap.microseconds = v->get_voice_cpu_microseconds();
		snap.current_lod = v->get_current_lod_tier();
		snap.max_lod = v->get_cached_max_lod();
		snap.transitioning = v->is_lod_transitioning();

		observe_cost_sample(v->get_estimated_cost_units(), snap.microseconds);

		total_budget += snap.budget;
		if (snap.budget > peak_budget) {
			peak_budget = snap.budget;
		}
		total_microseconds += snap.microseconds;
		voice_count++;
	}

	float avg_microseconds = (voice_count > 0) ? (total_microseconds / voice_count) : 0.0f;

	// Phase 2: queue LOD demotions onto per-voice atomics.
	if (total_budget > warning_threshold * 100.0f && total_budget <= critical_threshold * 100.0f) {
		float remaining_budget_lod = total_budget;
		float target_budget = warning_threshold * 100.0f * 0.85f;
		static constexpr int32_t MAX_LODS_PER_CALLBACK = 16;
		int32_t lod_queued = 0;

		while (remaining_budget_lod > target_budget && lod_queued < MAX_LODS_PER_CALLBACK) {
			int32_t best_idx = -1;
			float best_budget = 0.0f;

			for (int32_t i = 0; i < voice_count; i++) {
				if (snapshots[i].current_lod < snapshots[i].max_lod &&
						!snapshots[i].transitioning &&
						snapshots[i].budget > best_budget) {
					best_budget = snapshots[i].budget;
					best_idx = i;
				}
			}

			if (best_idx < 0) {
				break;
			}

			int32_t new_lod = snapshots[best_idx].current_lod + 1;
			snapshots[best_idx].voice->request_lod_tier(new_lod);
			snapshots[best_idx].current_lod = new_lod;
			snapshots[best_idx].transitioning = true;
			lod_queued++;

			remaining_budget_lod -= snapshots[best_idx].budget * 0.4f;
			snapshots[best_idx].budget *= 0.6f;
		}
	}

	// Phase 3: select victims into a fixed stack index array (no heap).
	int32_t victim_indices[MAX_SNAPSHOT];
	int32_t victim_count = 0;
	bool is_victim_flags[MAX_SNAPSHOT] = {};

	auto mark_victim = [&](int32_t idx) {
		if (idx < 0 || idx >= voice_count || is_victim_flags[idx]) {
			return;
		}
		is_victim_flags[idx] = true;
		victim_indices[victim_count++] = idx;
	};

	if (max_voices > 0 && voice_count > max_voices) {
		while (voice_count - victim_count > max_voices) {
			int worst_idx = -1;
			int worst_priority = INT32_MAX;
			float worst_rms = FLT_MAX;

			for (int32_t i = 0; i < voice_count; i++) {
				if (is_victim_flags[i]) {
					continue;
				}
				if (snapshots[i].priority < worst_priority ||
						(snapshots[i].priority == worst_priority && snapshots[i].rms < worst_rms)) {
					worst_priority = snapshots[i].priority;
					worst_rms = snapshots[i].rms;
					worst_idx = i;
				}
			}
			if (worst_idx < 0) {
				break;
			}
			mark_victim(worst_idx);
		}
	}

	float remaining_budget = 0.0f;
	for (int32_t i = 0; i < voice_count; i++) {
		if (!is_victim_flags[i]) {
			remaining_budget += snapshots[i].budget;
		}
	}

	if (remaining_budget > critical_threshold * 100.0f) {
		while (remaining_budget > warning_threshold * 100.0f) {
			int worst_idx = -1;
			int worst_priority = INT32_MAX;
			float worst_rms = FLT_MAX;

			for (int32_t i = 0; i < voice_count; i++) {
				if (is_victim_flags[i]) {
					continue;
				}
				if (snapshots[i].priority < worst_priority ||
						(snapshots[i].priority == worst_priority && snapshots[i].rms < worst_rms)) {
					worst_priority = snapshots[i].priority;
					worst_rms = snapshots[i].rms;
					worst_idx = i;
				}
			}
			if (worst_idx < 0) {
				break;
			}
			remaining_budget -= snapshots[worst_idx].budget;
			mark_victim(worst_idx);
		}
	}

	// Phase 4: publish metrics.
	metric_active_count.store(voice_count, std::memory_order_relaxed);
	metric_stolen_this_frame.store(victim_count, std::memory_order_relaxed);
	metric_total_budget_millipercent.store(static_cast<int32_t>(total_budget * 1000.0f), std::memory_order_relaxed);
	metric_peak_budget_millipercent.store(static_cast<int32_t>(peak_budget * 1000.0f), std::memory_order_relaxed);
	metric_avg_voice_microseconds_x1000.store(static_cast<int32_t>(avg_microseconds * 1000.0f), std::memory_order_relaxed);

	// Phase 5: request stops via atomics only (main thread performs stop()).
	for (int32_t i = 0; i < victim_count; i++) {
		snapshots[victim_indices[i]].voice->request_manager_stop();
	}
}
