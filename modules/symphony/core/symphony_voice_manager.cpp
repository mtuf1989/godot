#include "symphony_voice_manager.h"
#include "../stream/audio_stream_playback_symphony.h"
#include "symphony_trigger.h"
#include "symphony_runtime_metrics.h"
#include "symphony_memory_budget.h"
#include "symphony_graph_package_retirement.h"
#include "symphony_realtime_scope.h"
#include "core/config/engine.h"
#include "core/object/class_db.h"
#include "core/object/object.h"
#include "core/os/thread.h"
#include "symphony_platform_time.h"
#include "core/variant/dictionary.h"
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
	ClassDB::bind_method(D_METHOD("get_dropped_grains_count"), &SymphonyVoiceManager::get_dropped_grains_count);
	ClassDB::bind_method(D_METHOD("get_spectral_underflow_count"), &SymphonyVoiceManager::get_spectral_underflow_count);
	ClassDB::bind_method(D_METHOD("get_crossfade_transition_count"), &SymphonyVoiceManager::get_crossfade_transition_count);
	ClassDB::bind_method(D_METHOD("get_fallback_transition_count"), &SymphonyVoiceManager::get_fallback_transition_count);
	ClassDB::bind_method(D_METHOD("get_packages_destroyed_count"), &SymphonyVoiceManager::get_packages_destroyed_count);
	ClassDB::bind_method(D_METHOD("get_retirement_pending_count"), &SymphonyVoiceManager::get_retirement_pending_count);
	ClassDB::bind_method(D_METHOD("get_retirement_peak_pending_count"), &SymphonyVoiceManager::get_retirement_peak_pending_count);
	ClassDB::bind_method(D_METHOD("get_rt_violation_count"), &SymphonyVoiceManager::get_rt_violation_count);
	ClassDB::bind_method(D_METHOD("get_debug_metrics"), &SymphonyVoiceManager::get_debug_metrics);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_voices"), "set_max_voices", "get_max_voices");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "warning_threshold"), "set_warning_threshold", "get_warning_threshold");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "critical_threshold"), "set_critical_threshold", "get_critical_threshold");
}

static bool _symphony_editor_ui_session() {
	Engine *engine = Engine::get_singleton();
	return engine && engine->is_editor_hint();
}

SymphonyVoiceManager::SymphonyVoiceManager() {
	singleton = this;
	if (AudioServer::get_singleton()) {
		AudioServer::get_singleton()->add_update_callback(_update_callback, this);
		update_callback_registered = true;
		// Editor-launched games are still TOOLS_ENABLED binaries, but editor_hint is false.
		// Budget management has to run there as well as in exports. The editor UI itself does not.
		_ensure_mix_callback();
	}
}

void SymphonyVoiceManager::_ensure_mix_callback() {
	if (_symphony_editor_ui_session()) {
		return;
	}
	AudioServer *server = AudioServer::get_singleton();
	if (!server) {
		return;
	}
	if (mix_callback_registered && mix_callback_server == server) {
		return;
	}
	server->add_mix_callback(_mix_callback, this);
	mix_callback_registered = true;
	mix_callback_server = server;
}

SymphonyVoiceManager::~SymphonyVoiceManager() {
	if (mix_callback_registered && mix_callback_server && AudioServer::get_singleton() == mix_callback_server) {
		// Lock the driver so no mix callback is in flight when it is removed.
		mix_callback_server->lock();
		mix_callback_server->remove_mix_callback(_mix_callback, this);
		mix_callback_server->unlock();
		mix_callback_registered = false;
		mix_callback_server = nullptr;
	}
	if (update_callback_registered && AudioServer::get_singleton()) {
		AudioServer::get_singleton()->remove_update_callback(_update_callback, this);
		update_callback_registered = false;
	}
	singleton = nullptr;
	for (Ref<AudioStreamPlaybackSymphony> &voice : retained_voices) {
		if (voice.is_valid()) {
			voice->registered_with_manager = false;
		}
	}
	retained_voices.clear();
}

void SymphonyVoiceManager::_mix_callback(void *p_userdata) {
	SymphonyRealtimeScope rt_scope;
	SymphonyVoiceManager *mgr = static_cast<SymphonyVoiceManager *>(p_userdata);
	mgr->enforce_voice_limits();
}

void SymphonyVoiceManager::_update_callback(void *p_userdata) {
	SymphonyVoiceManager *mgr = static_cast<SymphonyVoiceManager *>(p_userdata);
	mgr->process_deferred_lod();
}

bool SymphonyVoiceManager::register_voice(AudioStreamPlaybackSymphony *p_voice) {
	if (!p_voice || _symphony_editor_ui_session()) {
		return false;
	}
	if (p_voice->registered_with_manager) {
		return true;
	}
	if (!Thread::is_main_thread()) {
		return false;
	}
	_ensure_mix_callback();
	symphony_rt_note(SymphonyRTViolation::ContainerMutation, "SymphonyVoiceManager::register_voice");
	// Lock-free for the audio thread: SafeList insert only; retained_voices is main-owned.
	retained_voices.push_back(Ref<AudioStreamPlaybackSymphony>(p_voice));
	active_voices.insert(p_voice);
	p_voice->registered_with_manager = true;
	return true;
}

int32_t SymphonyVoiceManager::get_parked_package_count() const {
	int32_t parked = 0;
	for (const Ref<AudioStreamPlaybackSymphony> &voice : retained_voices) {
		if (voice.is_valid() && !voice->is_playing() && voice->control_package.load(std::memory_order_acquire) != nullptr) {
			parked++;
		}
	}
	return parked;
}

void SymphonyVoiceManager::process_deferred_lod() {
	// Main thread: reap unreachable voices, then apply per-voice LOD requests.
	// At most one LOD compilation per update to avoid allocation bursts (plan §5).
	symphony_rt_note(SymphonyRTViolation::ObjectDB, "SymphonyVoiceManager::process_deferred_lod");
	symphony_rt_note(SymphonyRTViolation::ContainerMutation, "SymphonyVoiceManager::process_deferred_lod");

	// Phase 1 (reap, I4): refcount==1 means only our Ref is left, i.e. AudioServer and scripts let go.
	Vector<int32_t> reap_indices;
	for (int32_t i = 0; i < retained_voices.size(); i++) {
		const Ref<AudioStreamPlaybackSymphony> &voice = retained_voices[i];
		if (voice.is_valid() && voice->get_reference_count() == 1) {
			reap_indices.push_back(i);
		}
	}
	if (!reap_indices.is_empty()) {
		{
			// Erase only marks nodes into the graveyard; no allocation under the lock.
			MutexLock lock(voice_lifetime_mutex);
			for (int32_t idx : reap_indices) {
				active_voices.erase(retained_voices[idx].ptr());
			}
		}
		for (int32_t k = reap_indices.size() - 1; k >= 0; k--) {
			const int32_t idx = reap_indices[k];
			Ref<AudioStreamPlaybackSymphony> dead = retained_voices[idx];
			retained_voices.remove_at(idx);
			dead->registered_with_manager = false;
			dead->_reclaim_unreachable();
			dead.unref();
		}
	}

	bool lod_compiled = false;
	for (const Ref<AudioStreamPlaybackSymphony> &voice : retained_voices) {
		AudioStreamPlaybackSymphony *v = voice.ptr();
		if (!v) {
			continue;
		}
		const bool has_request = v->requested_lod_tier.load(std::memory_order_relaxed) >= 0;
		const bool floor_differs = v->get_budget_lod_floor() != v->get_current_lod_tier();
		if (!lod_compiled && (has_request || floor_differs)) {
			v->process_manager_requests();
			lod_compiled = true;
		}
	}

	active_voices.maybe_cleanup();
	GraphPackageRetirement::drain();
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

uint64_t SymphonyVoiceManager::get_dropped_grains_count() const {
	return symphony_dropped_grains_count().load(std::memory_order_relaxed);
}

uint64_t SymphonyVoiceManager::get_spectral_underflow_count() const {
	return symphony_spectral_underflow_count().load(std::memory_order_relaxed);
}

uint64_t SymphonyVoiceManager::get_crossfade_transition_count() const {
	return metric_crossfade_transitions.load(std::memory_order_relaxed);
}

uint64_t SymphonyVoiceManager::get_fallback_transition_count() const {
	return metric_fallback_transitions.load(std::memory_order_relaxed);
}

uint64_t SymphonyVoiceManager::get_packages_destroyed_count() const {
	return GraphPackageRetirement::get_destroyed_count();
}

uint32_t SymphonyVoiceManager::get_retirement_pending_count() const {
	return GraphPackageRetirement::get_pending_count();
}

uint32_t SymphonyVoiceManager::get_retirement_peak_pending_count() const {
	return GraphPackageRetirement::get_peak_pending_count();
}

uint32_t SymphonyVoiceManager::get_rt_violation_count() const {
	return SymphonyRealtimeScope::violation_count();
}

void SymphonyVoiceManager::note_crossfade_transition() {
	metric_crossfade_transitions.fetch_add(1, std::memory_order_relaxed);
}

void SymphonyVoiceManager::note_fallback_transition() {
	metric_fallback_transitions.fetch_add(1, std::memory_order_relaxed);
}

Dictionary SymphonyVoiceManager::get_debug_metrics() const {
	symphony_rt_note(SymphonyRTViolation::ContainerMutation, "SymphonyVoiceManager::get_debug_metrics");
	Dictionary d;
	d["active_voice_count"] = get_active_voice_count();
	d["total_budget_percent"] = get_total_budget_percent();
	d["peak_budget_percent"] = get_peak_budget_percent();
	d["average_voice_microseconds"] = get_average_voice_microseconds();
	d["stolen_this_frame"] = get_stolen_this_frame();
	d["dropped_trigger_count"] = (int64_t)get_dropped_trigger_count();
	d["dropped_grains_count"] = (int64_t)get_dropped_grains_count();
	d["spectral_underflow_count"] = (int64_t)get_spectral_underflow_count();
	d["crossfade_transition_count"] = (int64_t)get_crossfade_transition_count();
	d["fallback_transition_count"] = (int64_t)get_fallback_transition_count();
	d["crossfade_tokens_available"] = get_crossfade_tokens_available();
	d["retirement_pending"] = (int)get_retirement_pending_count();
	d["retirement_peak_pending"] = (int)get_retirement_peak_pending_count();
	d["packages_destroyed"] = (int64_t)get_packages_destroyed_count();
	d["rt_violations"] = (int)get_rt_violation_count();
	d["enforce_skipped"] = (int64_t)get_enforce_skipped_count();
	d["voices_retained"] = get_retained_voice_count();
	d["packages_parked"] = get_parked_package_count();

	if (SymphonyMemoryBudget *budget = SymphonyMemoryBudget::get_singleton()) {
		const SymphonyMemoryBudget::Snapshot snap = budget->get_snapshot();
		d["memory_per_graph_limit_bytes"] = (int64_t)snap.per_graph_limit_bytes;
		d["memory_global_limit_bytes"] = (int64_t)snap.global_limit_bytes;
		d["memory_reserved_bytes"] = (int64_t)snap.reserved_bytes;
		d["memory_peak_reserved_bytes"] = (int64_t)snap.peak_reserved_bytes;
		d["memory_shared_pcm_bytes"] = (int64_t)snap.shared_pcm_bytes;
		d["memory_global_used_bytes"] = (int64_t)(snap.reserved_bytes + snap.shared_pcm_bytes);
		d["packages_active"] = (int)snap.active_packages;
		d["packages_pending"] = (int)snap.pending_packages;
		d["packages_outgoing"] = (int)snap.outgoing_packages;
		d["packages_retired"] = (int)snap.retired_packages;
	}
	return d;
}

void SymphonyVoiceManager::set_max_voices(int32_t p_max) {
	max_voices.store(p_max, std::memory_order_relaxed);
}

int32_t SymphonyVoiceManager::get_max_voices() const {
	return max_voices.load(std::memory_order_relaxed);
}

void SymphonyVoiceManager::set_warning_threshold(float p_threshold) {
	warning_threshold.store(p_threshold, std::memory_order_relaxed);
}

float SymphonyVoiceManager::get_warning_threshold() const {
	return warning_threshold.load(std::memory_order_relaxed);
}

void SymphonyVoiceManager::set_critical_threshold(float p_threshold) {
	critical_threshold.store(p_threshold, std::memory_order_relaxed);
}

float SymphonyVoiceManager::get_critical_threshold() const {
	return critical_threshold.load(std::memory_order_relaxed);
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
	_enforce_voice_limits_at(symphony_time_usec());
}

void SymphonyVoiceManager::_enforce_voice_limits_at(uint64_t p_now_us) {
	// Audio thread only: fixed stack snapshots + per-voice atomics. No ObjectDB, heap containers,
	// Resource access, package memory or graph compilation (plan §6, I6).
	callback_seq.fetch_add(1, std::memory_order_acq_rel);
	if (!voice_lifetime_mutex.try_lock()) {
		// Main is erasing a reaped voice; decisions resume next callback.
		metric_enforce_skipped.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	struct ListUnlock {
		BinaryMutex &mutex;
		~ListUnlock() { mutex.unlock(); }
	} list_unlock{ voice_lifetime_mutex };
	const uint64_t seq = callback_seq.load(std::memory_order_acquire);

	struct VoiceSnapshot {
		AudioStreamPlaybackSymphony *voice = nullptr;
		float cost = 0.0f; // smoothed % of block deadline
		float rms = 0.0f;
		float importance = 50.0f;
		int tier = 1; // SoundEvent::VoiceTier — VITAL never budget-stolen
		float microseconds = 0.0f;
		int floor = 0;
		int max_lod = 0;
		uint64_t floor_changed_us = UINT64_MAX;
		float instant_budget = 0.0f;
	};

	static constexpr int32_t MAX_SNAPSHOT = 256;
	VoiceSnapshot snapshots[MAX_SNAPSHOT];
	int32_t voice_count = 0;

	float total_cost = 0.0f;
	float peak_budget = 0.0f;
	float total_microseconds = 0.0f;

	for (auto it = active_voices.begin(); it != active_voices.end(); ++it) {
		if (voice_count >= MAX_SNAPSHOT) {
			break;
		}
		AudioStreamPlaybackSymphony *v = *it;
		// Only voices that mixed during the previous callback interval count (paused/stopped drop out).
		if (!v || !v->is_playing() || seq - v->get_last_mix_seq() > 1) {
			continue;
		}
		VoiceSnapshot &snap = snapshots[voice_count];
		snap.voice = v;
		snap.cost = v->get_cost_pct_ewma();
		snap.rms = v->get_last_rms();
		snap.importance = v->get_voice_importance();
		snap.tier = v->get_voice_tier();
		snap.microseconds = v->get_voice_cpu_microseconds();
		snap.floor = v->get_budget_lod_floor();
		snap.max_lod = v->get_cached_max_lod();
		snap.floor_changed_us = v->get_floor_changed_us();
		snap.instant_budget = v->get_budget_percent();

		observe_cost_sample(v->get_estimated_cost_units(), snap.microseconds);

		total_cost += snap.cost;
		peak_budget = MAX(peak_budget, snap.instant_budget);
		total_microseconds += snap.microseconds;
		voice_count++;
	}

	const float avg_microseconds = (voice_count > 0) ? (total_microseconds / voice_count) : 0.0f;

	const float warn_pct = get_warning_threshold() * 100.0f;
	const float crit_pct = get_critical_threshold() * 100.0f;
	const float release_pct = MAX(0.0f, warn_pct - RELEASE_HYSTERESIS * 100.0f);
	const float target_pct = warn_pct - ENGAGE_TARGET_BELOW_WARNING * 100.0f;
	const float release_projection_limit = MAX(0.0f, release_pct - 5.0f);

	// Phase 2: LOD floor hysteresis. Floors are only ever cleared one tier at a time by the
	// release rule, never by a blanket reset (a high load must not undo demotions).
	float level = total_cost;
	if (level > warn_pct) {
		if (!over_warn_active) {
			over_warn_active = true;
			over_warn_since_us = p_now_us;
		}
	} else {
		over_warn_active = false;
	}
	const bool above_critical = level > crit_pct;
	const bool warn_sustained = over_warn_active && (p_now_us - over_warn_since_us) >= WARN_SUSTAIN_US;
	bool engaged = false;
	if (above_critical || warn_sustained) {
		static constexpr int32_t MAX_STEPS_WARN = 4;
		static constexpr int32_t MAX_STEPS_CRITICAL = 16;
		const int32_t max_steps = above_critical ? MAX_STEPS_CRITICAL : MAX_STEPS_WARN;
		for (int32_t step = 0; step < max_steps && level > target_pct; step++) {
			int32_t best_idx = -1;
			float best_cost = 0.0f;
			for (int32_t i = 0; i < voice_count; i++) {
				const VoiceSnapshot &snap = snapshots[i];
				if (snap.floor >= snap.max_lod) {
					continue;
				}
				if (snap.floor_changed_us != UINT64_MAX && (p_now_us - snap.floor_changed_us) < FLOOR_CHANGE_MIN_US) {
					continue;
				}
				if (snap.cost > best_cost) {
					best_cost = snap.cost;
					best_idx = i;
				}
			}
			if (best_idx < 0) {
				break;
			}
			VoiceSnapshot &chosen = snapshots[best_idx];
			chosen.floor += 1;
			chosen.floor_changed_us = p_now_us;
			chosen.voice->set_budget_lod_floor_at(chosen.floor, p_now_us);
			// A tier drop is projected to cost ~60% of the previous tier.
			level -= chosen.cost * 0.4f;
			engaged = true;
		}
	}

	if (engaged || level >= release_pct) {
		below_release_active = false;
	} else if (!below_release_active) {
		below_release_active = true;
		below_release_since_us = p_now_us;
	}
	if (!engaged && below_release_active && (p_now_us - below_release_since_us) >= RELEASE_QUIET_US &&
			(!has_released || (p_now_us - last_release_us) >= RELEASE_SPACING_US)) {
		int32_t best_idx = -1;
		float best_importance = -1.0f;
		for (int32_t i = 0; i < voice_count; i++) {
			const VoiceSnapshot &snap = snapshots[i];
			if (snap.floor <= 0) {
				continue;
			}
			if (snap.floor_changed_us != UINT64_MAX && (p_now_us - snap.floor_changed_us) < RELEASE_DWELL_US) {
				continue;
			}
			// Going up a tier is projected to cost 1/0.6 of the current tier.
			if (level + snap.cost * (1.0f / 0.6f - 1.0f) >= release_projection_limit) {
				continue;
			}
			if (snap.importance > best_importance) {
				best_importance = snap.importance;
				best_idx = i;
			}
		}
		if (best_idx >= 0) {
			snapshots[best_idx].voice->set_budget_lod_floor_at(snapshots[best_idx].floor - 1, p_now_us);
			has_released = true;
			last_release_us = p_now_us;
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
	// P8a N9: rank by pool importance (ties → quieter RMS). VITAL is never budget-stolen.
	static constexpr int TIER_VITAL = 2;
	auto pick_worst = [&]() {
		int worst_idx = -1;
		float worst_importance = FLT_MAX;
		float worst_rms = FLT_MAX;
		for (int32_t i = 0; i < voice_count; i++) {
			if (is_victim_flags[i]) {
				continue;
			}
			if (snapshots[i].tier >= TIER_VITAL) {
				continue;
			}
			if (snapshots[i].importance < worst_importance ||
					(snapshots[i].importance == worst_importance && snapshots[i].rms < worst_rms)) {
				worst_importance = snapshots[i].importance;
				worst_rms = snapshots[i].rms;
				worst_idx = i;
			}
		}
		return worst_idx;
	};

	const int32_t voice_limit = get_max_voices();
	if (voice_limit > 0 && voice_count > voice_limit) {
		while (voice_count - victim_count > voice_limit) {
			const int32_t worst_idx = pick_worst();
			if (worst_idx < 0) {
				break;
			}
			mark_victim(worst_idx);
		}
	}

	// Steal while the smoothed load is above critical, down to warning.
	float remaining_cost = 0.0f;
	for (int32_t i = 0; i < voice_count; i++) {
		if (!is_victim_flags[i]) {
			remaining_cost += snapshots[i].cost;
		}
	}
	if (remaining_cost > crit_pct) {
		while (remaining_cost > warn_pct) {
			const int32_t worst_idx = pick_worst();
			if (worst_idx < 0) {
				break;
			}
			remaining_cost -= snapshots[worst_idx].cost;
			mark_victim(worst_idx);
		}
	}

	// Phase 4: publish metrics.
	metric_active_count.store(voice_count, std::memory_order_relaxed);
	metric_stolen_this_frame.store(victim_count, std::memory_order_relaxed);
	metric_total_budget_millipercent.store(static_cast<int32_t>(total_cost * 1000.0f), std::memory_order_relaxed);
	metric_peak_budget_millipercent.store(static_cast<int32_t>(peak_budget * 1000.0f), std::memory_order_relaxed);
	metric_avg_voice_microseconds_x1000.store(static_cast<int32_t>(avg_microseconds * 1000.0f), std::memory_order_relaxed);

	// Phase 5: steals are a pure atomic request (I2); the voice goes silent on its next mix.
	for (int32_t i = 0; i < victim_count; i++) {
		snapshots[victim_indices[i]].voice->stop();
	}
}
