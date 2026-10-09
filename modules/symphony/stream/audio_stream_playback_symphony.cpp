#include "audio_stream_playback_symphony.h"
#include "../core/symphony_voice_manager.h"
#include "../core/symphony_graph_package_retirement.h"
#include "../core/symphony_memory_budget.h"
#include "../core/symphony_denormal_guard.h"
#include "../core/symphony_fast_math.h"
#include "../core/symphony_platform_time.h"
#include "../core/symphony_realtime_scope.h"
#include "core/object/class_db.h"
#include "core/os/thread.h"
#include "servers/audio/audio_server.h"

#include <cmath>

namespace {

void _pkg_active(int32_t d) {
	if (SymphonyMemoryBudget *b = SymphonyMemoryBudget::get_singleton()) {
		b->adjust_active_packages(d);
	}
}
void _pkg_pending(int32_t d) {
	if (SymphonyMemoryBudget *b = SymphonyMemoryBudget::get_singleton()) {
		b->adjust_pending_packages(d);
	}
}
void _pkg_outgoing(int32_t d) {
	if (SymphonyMemoryBudget *b = SymphonyMemoryBudget::get_singleton()) {
		b->adjust_outgoing_packages(d);
	}
}

} // namespace

void AudioStreamPlaybackSymphony::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_parameter", "name", "value"), &AudioStreamPlaybackSymphony::set_parameter);
	ClassDB::bind_method(D_METHOD("get_parameter", "name"), &AudioStreamPlaybackSymphony::get_parameter);
	ClassDB::bind_method(D_METHOD("trigger", "name", "value"), &AudioStreamPlaybackSymphony::trigger, DEFVAL(1.0f));
	ClassDB::bind_method(D_METHOD("get_voice_cpu_microseconds"), &AudioStreamPlaybackSymphony::get_voice_cpu_microseconds);
	ClassDB::bind_method(D_METHOD("get_budget_percent"), &AudioStreamPlaybackSymphony::get_budget_percent);
	ClassDB::bind_method(D_METHOD("get_last_rms"), &AudioStreamPlaybackSymphony::get_last_rms);
	ClassDB::bind_method(D_METHOD("has_unsupported_seek"), &AudioStreamPlaybackSymphony::has_unsupported_seek);
	ClassDB::bind_method(D_METHOD("get_budget_lod_floor"), &AudioStreamPlaybackSymphony::get_budget_lod_floor);
	ClassDB::bind_method(D_METHOD("get_current_lod_tier"), &AudioStreamPlaybackSymphony::get_current_lod_tier);
	ClassDB::bind_method(D_METHOD("request_lod_tier", "lod_tier"), &AudioStreamPlaybackSymphony::request_lod_tier);
}

void AudioStreamPlaybackSymphony::_install_package(PreparedGraphPackage *p_package) {
	// Mixing thread only (I1). Publish control_package before the caller retires the old package (I3).
	current_package = p_package;
	current_graph = p_package ? p_package->graph : nullptr;
	graph_output_node = p_package ? p_package->graph_output : nullptr;
	if (p_package) {
		installed_lod_tier.store(p_package->lod_tier, std::memory_order_relaxed);
		stat_cost_units.store(p_package->estimated_cost_units, std::memory_order_relaxed);
		cost_ewma_reseed = true;
	}
	control_package.store(p_package, std::memory_order_release);
}

void AudioStreamPlaybackSymphony::_release_crossfade_token() {
	if (!holds_crossfade_token) {
		return;
	}
	holds_crossfade_token = false;
	SymphonyVoiceManager *mgr = SymphonyVoiceManager::get_singleton();
	if (mgr) {
		mgr->release_crossfade_token();
	}
}

void AudioStreamPlaybackSymphony::_abort_transition_packages() {
	_release_crossfade_token();
	if (outgoing_package) {
		_pkg_outgoing(-1);
		GraphPackageRetirement::retire(outgoing_package);
		outgoing_package = nullptr;
	}
	if (incoming_package) {
		_pkg_pending(-1);
		GraphPackageRetirement::retire(incoming_package);
		incoming_package = nullptr;
	}
	transition_mode = TransitionMode::Idle;
	transition_progress = 1.0f;
	transition_speed = 0.0f;
}

AudioStreamPlaybackSymphony::AdmitResult AudioStreamPlaybackSymphony::_try_admit_crossfade(const PreparedGraphPackage *p_incoming) {
	// Only admit a dual-graph crossfade from a fully idle transition state.
	if (transition_mode != TransitionMode::Idle) {
		return AdmitResult::Denied;
	}
	SymphonyVoiceManager *mgr = SymphonyVoiceManager::get_singleton();
	if (!mgr) {
		return AdmitResult::AdmittedNoToken;
	}
	// get_total_budget_percent() is 0–100+; thresholds are 0–1 fractions.
	float cpu_fraction = mgr->get_total_budget_percent() / 100.0f;
	if (cpu_fraction >= mgr->get_critical_threshold() || cpu_fraction >= mgr->get_warning_threshold()) {
		return AdmitResult::Denied;
	}
	float incoming_cost = p_incoming ? p_incoming->estimated_cost_units : 0.0f;
	const int32_t last_frames = last_output_frames.load(std::memory_order_relaxed);
	const int frames = last_frames > 0 ? last_frames : 512;
	const float out_rate = output_rate_cached.load(std::memory_order_relaxed);
	const float budget_rate = out_rate > 0.0f ? out_rate : mix_rate_cached.load(std::memory_order_relaxed);
	float estimated_add = mgr->estimate_cpu_fraction_for_cost(incoming_cost, budget_rate, frames);
	if (cpu_fraction + estimated_add >= mgr->get_warning_threshold()) {
		return AdmitResult::Denied;
	}
	if (!mgr->try_acquire_crossfade_token()) {
		return AdmitResult::Denied;
	}
	return AdmitResult::AdmittedWithToken;
}

void AudioStreamPlaybackSymphony::_migrate_into(PreparedGraphPackage *p_destination) {
	// Audio-thread only: current_package is stable for this mix callback.
	PreparedGraphPackage::migrate_compatible_state(current_package, p_destination);
}

void AudioStreamPlaybackSymphony::_begin_equal_power_crossfade(PreparedGraphPackage *p_new_package) {
	// Migrate bounded state into the incoming graph before dual-graph playback.
	_migrate_into(p_new_package);
	// pending → current, current → outgoing
	_pkg_pending(-1);
	if (current_package) {
		_pkg_active(-1);
		_pkg_outgoing(1);
	}
	outgoing_package = current_package;
	_install_package(p_new_package);
	_pkg_active(1);
	transition_mode = TransitionMode::EqualPowerCrossfade;
	transition_progress = 0.0f;
	const float graph_rate = mix_rate_cached.load(std::memory_order_relaxed);
	float samples = graph_rate > 0.0f ? graph_rate * CROSSFADE_SECONDS : 2048.0f;
	transition_speed = 1.0f / samples;
	if (SymphonyVoiceManager *mgr = SymphonyVoiceManager::get_singleton()) {
		mgr->note_crossfade_transition();
	}
}

void AudioStreamPlaybackSymphony::_begin_fallback_transition(PreparedGraphPackage *p_new_package) {
	_release_crossfade_token();
	if (outgoing_package) {
		_pkg_outgoing(-1);
		GraphPackageRetirement::retire(outgoing_package);
		outgoing_package = nullptr;
	}
	if (incoming_package) {
		_pkg_pending(-1);
		GraphPackageRetirement::retire(incoming_package);
	}
	// New package stays counted as pending until installed as current.
	incoming_package = p_new_package;
	if (!current_package) {
		_pkg_pending(-1);
		_install_package(incoming_package);
		_pkg_active(1);
		incoming_package = nullptr;
		transition_mode = TransitionMode::FallbackFadeIn;
	} else {
		transition_mode = TransitionMode::FallbackFadeOut;
	}
	transition_progress = 0.0f;
	transition_speed = 1.0f / (float)FALLBACK_FADE_SAMPLES;
	if (SymphonyVoiceManager *mgr = SymphonyVoiceManager::get_singleton()) {
		mgr->note_fallback_transition();
	}
}

void AudioStreamPlaybackSymphony::_cache_stream_metadata() {
	if (!stream.is_valid()) {
		mix_rate_cached.store(44100.0f, std::memory_order_relaxed);
		cached_priority.store(50, std::memory_order_relaxed);
		cached_max_lod.store(0, std::memory_order_relaxed);
		return;
	}
	const float graph_rate = stream->get_mix_rate();
	mix_rate_cached.store(graph_rate, std::memory_order_relaxed);
	cached_priority.store(stream->get_voice_priority(), std::memory_order_relaxed);
	cached_max_lod.store(MAX(0, stream->get_lod_count() - 1), std::memory_order_relaxed);
	cached_duration_limit.store(stream->get_duration_limit_seconds(), std::memory_order_relaxed);
	float out_rate = graph_rate;
	if (AudioServer::get_singleton()) {
		const float server_rate = AudioServer::get_singleton()->get_mix_rate();
		if (server_rate > 0.0f) {
			out_rate = server_rate;
		}
	}
	output_rate_cached.store(out_rate, std::memory_order_relaxed);
}

void AudioStreamPlaybackSymphony::request_lod_tier(int32_t p_lod_tier) {
	requested_lod_tier.store(p_lod_tier, std::memory_order_release);
}

void AudioStreamPlaybackSymphony::set_budget_lod_floor(int32_t p_lod_tier) {
	budget_lod_floor.store(CLAMP(p_lod_tier, 0, 8), std::memory_order_release);
}

void AudioStreamPlaybackSymphony::set_budget_lod_floor_at(int32_t p_lod_tier, uint64_t p_now_us) {
	set_budget_lod_floor(p_lod_tier);
	floor_changed_us.store(p_now_us, std::memory_order_relaxed);
}

void AudioStreamPlaybackSymphony::_test_set_cost_ewma(float p_pct) {
	stat_cost_pct_ewma.store(p_pct, std::memory_order_relaxed);
	cost_ewma_reseed = false;
	SymphonyVoiceManager *mgr = SymphonyVoiceManager::get_singleton();
	last_mix_seq.store(mgr ? mgr->get_callback_seq() : 0, std::memory_order_relaxed);
}

void AudioStreamPlaybackSymphony::_sync_voice_lod() {
	// Main thread: turn the manager's budget floor into a concrete LOD publish, once per tier.
	const int32_t floor = budget_lod_floor.load(std::memory_order_acquire);
	if (floor == main_requested_lod) {
		return;
	}
	if (floor == installed_lod_tier.load(std::memory_order_relaxed)) {
		main_requested_lod = floor;
		return;
	}
	transition_to_lod(floor);
}

void AudioStreamPlaybackSymphony::process_manager_requests() {
	// Main thread only: LOD compile may touch ObjectDB and containers.
	symphony_rt_note(SymphonyRTViolation::ObjectDB, "AudioStreamPlaybackSymphony::process_manager_requests");
	int32_t lod = requested_lod_tier.exchange(-1, std::memory_order_acquire);
	if (lod >= 0) {
		transition_to_lod(lod);
		return;
	}
	_sync_voice_lod();
}

void AudioStreamPlaybackSymphony::_register_with_manager() {
	if (registered_with_manager || !Thread::is_main_thread()) {
		return;
	}
	SymphonyVoiceManager *mgr = SymphonyVoiceManager::get_singleton();
	if (mgr) {
		mgr->register_voice(this);
	}
}

void AudioStreamPlaybackSymphony::_report_unsupported_seek() {
	unsupported_seek.store(true, std::memory_order_release);
	ERR_PRINT("Symphony procedural playback does not support seeking. Start the voice at the selected beat or bar instead.");
}

bool AudioStreamPlaybackSymphony::_source_or_graph_finished() const {
	if (!current_package) {
		return false;
	}
	if (stream.is_valid() && stream->get_stop_on_source_finished()) {
		for (int t = 0; t < current_package->source_finished_triggers.size(); t++) {
			const TriggerBuffer *buf = current_package->source_finished_triggers[t];
			if (buf != nullptr && buf->count > 0) {
				return true;
			}
		}
	}
	for (int t = 0; t < current_package->finish_triggers.size(); t++) {
		const TriggerBuffer *buf = current_package->finish_triggers[t];
		if (buf != nullptr && buf->count > 0) {
			return true;
		}
	}
	return false;
}

void AudioStreamPlaybackSymphony::_publish_restart_package() {
	// Main thread only. A voice that has installed a package carries mixed state; a restart needs a
	// fresh graph. The audio thread adopts it (adopt_mode == Restart) before the next block.
	if (!stream.is_valid() || !Thread::is_main_thread() || control_package.load(std::memory_order_acquire) == nullptr) {
		return;
	}
	CompiledGraph *graph = stream->compile_lod_graph(main_requested_lod);
	if (!graph) {
		return;
	}
	PreparedGraphPackage *pkg = PreparedGraphPackage::create_from_graph(graph, 0, 0, main_requested_lod);
	if (!pkg) {
		memdelete(graph);
		return;
	}
	pkg->adopt_mode = PreparedGraphPackage::AdoptMode::Restart;
	_pkg_pending(1);
	PreparedGraphPackage *old_pending = pending_package.exchange(pkg, std::memory_order_acq_rel);
	if (old_pending) {
		_pkg_pending(-1);
		PreparedGraphPackage::destroy(old_pending);
	}
}

void AudioStreamPlaybackSymphony::start(double p_from_pos) {
	if (p_from_pos != 0.0) {
		_report_unsupported_seek();
	}
	const bool on_main = Thread::is_main_thread();
	if (on_main) {
		_cache_stream_metadata();
		if (control_package.load(std::memory_order_acquire) == nullptr && !active.load(std::memory_order_acquire)) {
			// Never mixed and not active: no mixer touches this voice, so main installs the first
			// package directly and controls (set_parameter/trigger) work right after start().
			if (PreparedGraphPackage *first = pending_package.exchange(nullptr, std::memory_order_acquire)) {
				_adopt_pending(first);
			}
		} else {
			_publish_restart_package();
		}
		_register_with_manager();
	}
	// Any thread: requests consumed by the mixing thread. active is stored last.
	position_us.store(0, std::memory_order_release);
	resample_reset_pending.store(true, std::memory_order_release);
	auto_fire_pending.store(true, std::memory_order_release);
	active.store(true, std::memory_order_release);
}

void AudioStreamPlaybackSymphony::stop() {
	// Pure request (I2): the mixing thread notices and goes silent; packages stay parked.
	active.store(false, std::memory_order_release);
}

bool AudioStreamPlaybackSymphony::is_playing() const {
	return active.load(std::memory_order_acquire);
}

int AudioStreamPlaybackSymphony::get_loop_count() const {
	return 0;
}

double AudioStreamPlaybackSymphony::get_playback_position() const {
	return (double)position_us.load(std::memory_order_acquire) / 1000000.0;
}

void AudioStreamPlaybackSymphony::seek(double p_time) {
	if (p_time != 0.0) {
		_report_unsupported_seek();
	}
}

float AudioStreamPlaybackSymphony::get_stream_sampling_rate() {
	const float rate = mix_rate_cached.load(std::memory_order_relaxed);
	return rate > 0.0f ? rate : 44100.0f;
}

void AudioStreamPlaybackSymphony::_adopt_pending(PreparedGraphPackage *p_pending) {
	if (p_pending->adopt_mode == PreparedGraphPackage::AdoptMode::Restart) {
		// Fresh state: drop any in-flight transition, install, then retire the old package (I3).
		_abort_transition_packages();
		PreparedGraphPackage *old_current = current_package;
		_pkg_pending(-1);
		_install_package(p_pending);
		_pkg_active(1);
		if (old_current) {
			_pkg_active(-1);
			GraphPackageRetirement::retire(old_current);
		}
		return;
	}
	AdmitResult admit = current_package ? _try_admit_crossfade(p_pending) : AdmitResult::Denied;
	if (admit != AdmitResult::Denied) {
		holds_crossfade_token = (admit == AdmitResult::AdmittedWithToken);
		_begin_equal_power_crossfade(p_pending);
	} else if (current_package || incoming_package || outgoing_package || transition_mode != TransitionMode::Idle) {
		_begin_fallback_transition(p_pending);
	} else {
		_pkg_pending(-1);
		_install_package(p_pending);
		_pkg_active(1);
	}
}

int AudioStreamPlaybackSymphony::_mix_internal(AudioFrame *p_buffer, int p_frames) {
	if (!active.load(std::memory_order_acquire) || p_frames <= 0) {
		return 0;
	}

	PreparedGraphPackage *pending = pending_package.exchange(nullptr, std::memory_order_acquire);
	if (pending) {
		_adopt_pending(pending);
	}

	if (auto_fire_pending.load(std::memory_order_acquire) && current_package) {
		auto_fire_pending.store(false, std::memory_order_relaxed);
		for (int i = 0; i < current_package->trigger_routes.size(); i++) {
			SymphonyTriggerInput *tin = current_package->trigger_routes[i].input;
			if (tin && tin->get_auto_trigger_on_play()) {
				tin->fire_local(1.0f);
			}
		}
	}

	if (!current_graph || !graph_output_node) {
		for (int i = 0; i < p_frames; i++) {
			p_buffer[i] = AudioFrame(0, 0);
		}
		return p_frames;
	}

	int frames_processed = 0;
	while (frames_processed < p_frames) {
		int chunk = MIN(SYMPHONY_MICRO_BLOCK_SIZE, p_frames - frames_processed);
		graph_output_node->set_output(p_buffer, frames_processed);
		current_graph->execute(chunk);

		if (transition_mode == TransitionMode::EqualPowerCrossfade && outgoing_package &&
				outgoing_package->graph && outgoing_package->graph_output) {
			AudioFrame outgoing_buf[SYMPHONY_MICRO_BLOCK_SIZE];
			outgoing_package->graph_output->set_output(outgoing_buf, 0);
			outgoing_package->graph->execute(chunk);

			for (int s = 0; s < chunk; s++) {
				float gain_old = 1.0f;
				float gain_new = 0.0f;
				SymphonyFastMath::equal_power_gains(transition_progress, gain_old, gain_new);
				int buf_idx = frames_processed + s;
				p_buffer[buf_idx].left = p_buffer[buf_idx].left * gain_new + outgoing_buf[s].left * gain_old;
				p_buffer[buf_idx].right = p_buffer[buf_idx].right * gain_new + outgoing_buf[s].right * gain_old;

				transition_progress += transition_speed;
				if (transition_progress >= 1.0f) {
					transition_progress = 1.0f;
					break;
				}
			}

			if (transition_progress >= 1.0f) {
				_pkg_outgoing(-1);
				GraphPackageRetirement::retire(outgoing_package);
				outgoing_package = nullptr;
				_release_crossfade_token();
				transition_mode = TransitionMode::Idle;
				transition_speed = 0.0f;
			}
		} else if (transition_mode == TransitionMode::FallbackFadeOut) {
			for (int s = 0; s < chunk; s++) {
				float gain = 1.0f - transition_progress;
				int buf_idx = frames_processed + s;
				p_buffer[buf_idx].left *= gain;
				p_buffer[buf_idx].right *= gain;
				transition_progress += transition_speed;
				if (transition_progress >= 1.0f) {
					transition_progress = 1.0f;
					break;
				}
			}
			if (transition_progress >= 1.0f) {
				// Block boundary: migrate then swap; never run two graphs in one sample.
				_migrate_into(incoming_package);
				PreparedGraphPackage *outgoing_current = current_package;
				_pkg_pending(-1);
				_install_package(incoming_package);
				_pkg_active(1);
				incoming_package = nullptr;
				// I3: install the new package before retiring the old one.
				if (outgoing_current) {
					_pkg_active(-1);
					GraphPackageRetirement::retire(outgoing_current);
				}
				transition_mode = TransitionMode::FallbackFadeIn;
				transition_progress = 0.0f;
				transition_speed = 1.0f / (float)FALLBACK_FADE_SAMPLES;
			}
		} else if (transition_mode == TransitionMode::FallbackFadeIn) {
			for (int s = 0; s < chunk; s++) {
				float gain = transition_progress;
				int buf_idx = frames_processed + s;
				p_buffer[buf_idx].left *= gain;
				p_buffer[buf_idx].right *= gain;
				transition_progress += transition_speed;
				if (transition_progress >= 1.0f) {
					transition_progress = 1.0f;
					break;
				}
			}
			if (transition_progress >= 1.0f) {
				transition_mode = TransitionMode::Idle;
				transition_speed = 0.0f;
			}
		}

		frames_processed += chunk;

		if (_source_or_graph_finished()) {
			for (int i = frames_processed; i < p_frames; i++) {
				p_buffer[i] = AudioFrame(0, 0);
			}
			active.store(false, std::memory_order_release);
			break;
		}
	}

	return frames_processed;
}

void AudioStreamPlaybackSymphony::_update_cost_stats(uint64_t p_mix_us, int p_frames, float p_out_rate) {
	stat_mix_us_last.store((float)p_mix_us, std::memory_order_relaxed);
	last_output_frames.store(p_frames, std::memory_order_relaxed);
	if (p_out_rate <= 0.0f || p_frames <= 0) {
		return;
	}
	const float deadline_s = (float)p_frames / p_out_rate;
	const float sample = ((float)p_mix_us * 1e-6f / deadline_s) * 100.0f;
	float ewma = stat_cost_pct_ewma.load(std::memory_order_relaxed);
	if (cost_ewma_reseed) {
		cost_ewma_reseed = false;
		ewma = sample;
	} else {
		// Spike clamp, then asymmetric EWMA: fast attack (50 ms), slow release (250 ms).
		const float clamped = MIN(sample, 3.0f * ewma + 0.5f);
		const float tau = clamped > ewma ? 0.050f : 0.250f;
		const float alpha = 1.0f - expf(-deadline_s / tau);
		ewma += alpha * (clamped - ewma);
	}
	stat_cost_pct_ewma.store(ewma, std::memory_order_relaxed);
}

int AudioStreamPlaybackSymphony::mix(AudioFrame *p_buffer, float p_rate_scale, int p_frames) {
	SymphonyDenormalGuard denormal_guard;
	SymphonyRealtimeScope rt_scope;
#ifdef DEV_ENABLED
	struct MixDepthScope {
		std::atomic<int32_t> &depth;
		explicit MixDepthScope(std::atomic<int32_t> &p_depth) :
				depth(p_depth) { depth.fetch_add(1, std::memory_order_acq_rel); }
		~MixDepthScope() { depth.fetch_sub(1, std::memory_order_acq_rel); }
	} mix_depth_scope(mix_depth);
#endif
	if (!active.load(std::memory_order_acquire) || p_frames <= 0) {
		return 0;
	}

	const uint64_t t_start = symphony_time_usec();
	int mixed = 0;
	const float graph_rate = mix_rate_cached.load(std::memory_order_relaxed);
	float out_rate = output_rate_cached.load(std::memory_order_relaxed);
	if (out_rate <= 0.0f) {
		out_rate = graph_rate;
	}
	float speed = p_rate_scale;
	const bool reset_resample = resample_reset_pending.exchange(false, std::memory_order_acq_rel);
	if (AudioServer::get_singleton()) {
		if (reset_resample) {
			begin_resample();
		}
		mixed = AudioStreamPlaybackResampled::mix(p_buffer, p_rate_scale, p_frames);
		const float server_rate = AudioServer::get_singleton()->get_mix_rate();
		if (server_rate > 0.0f) {
			out_rate = server_rate;
			output_rate_cached.store(server_rate, std::memory_order_relaxed);
		}
		speed *= AudioServer::get_singleton()->get_playback_speed_scale();
	} else {
		// Native unit tests mix without an audio device. Generate at the graph rate;
		// device-rate resampling runs when AudioServer is alive.
		out_rate = graph_rate > 0.0f ? graph_rate : 44100.0f;
		output_rate_cached.store(out_rate, std::memory_order_relaxed);
		mixed = _mix_internal(p_buffer, p_frames);
	}
	const uint64_t mix_us = symphony_time_usec() - t_start;

	if (out_rate <= 0.0f) {
		out_rate = 44100.0f;
	}
	if (mixed > 0 && speed > 0.0f) {
		const double add_us = ((double)mixed / (double)out_rate) * (double)speed * 1000000.0;
		if (add_us > 0.0) {
			position_us.fetch_add((uint64_t)add_us, std::memory_order_acq_rel);
		}
	}
	const double duration_limit = cached_duration_limit.load(std::memory_order_relaxed);
	if (duration_limit > 0.0 && (double)position_us.load(std::memory_order_acquire) / 1000000.0 >= duration_limit) {
		active.store(false, std::memory_order_release);
	}

	const int rms_frames = mixed > 0 ? mixed : 0;
	float sum_sq = 0.0f;
	for (int i = 0; i < rms_frames; i++) {
		sum_sq += p_buffer[i].left * p_buffer[i].left + p_buffer[i].right * p_buffer[i].right;
	}
	float rms_candidate = rms_frames > 0 ? sqrtf(sum_sq / (2.0f * (float)rms_frames)) : 0.0f;
	if (unlikely(std::isnan(rms_candidate) || std::isinf(rms_candidate))) {
		rms_candidate = 0.0f;
	}
	stat_rms.store(rms_candidate, std::memory_order_relaxed);
	_update_cost_stats(mix_us, p_frames, out_rate);

	SymphonyVoiceManager *mgr = SymphonyVoiceManager::get_singleton();
	last_mix_seq.store(mgr ? mgr->get_callback_seq() : 0, std::memory_order_relaxed);
	return mixed > 0 ? mixed : 0;
}

void AudioStreamPlaybackSymphony::swap_graph(CompiledGraph *p_graph) {
	if (!p_graph) {
		return;
	}

	// Main thread: publish only. Compatible state is migrated on the audio thread
	// at the block boundary when the pending package is adopted (plan §5).
	PreparedGraphPackage *pkg = PreparedGraphPackage::create_from_graph(p_graph);
	if (!pkg) {
		memdelete(p_graph);
		return;
	}

	pkg->adopt_mode = PreparedGraphPackage::AdoptMode::Swap;
	main_requested_lod = 0;
	_pkg_pending(1);
	PreparedGraphPackage *old_pending = pending_package.exchange(pkg, std::memory_order_release);
	if (old_pending) {
		_pkg_pending(-1);
		PreparedGraphPackage::destroy(old_pending);
	}
}

void AudioStreamPlaybackSymphony::set_parameter(const StringName &p_name, const Variant &p_value) {
	// Main thread: never touch current_package directly (audio may swap it).
	// A pending package is still alive when audio adopts it (only main frees), so write it too:
	// the value then survives the adoption (first start, restart, hot-swap).
	if (PreparedGraphPackage *pending = pending_package.load(std::memory_order_acquire)) {
		if (SymphonyGraphInput *pending_input = pending->find_param(p_name)) {
			pending_input->set_value((float)p_value);
		}
	}
	// Load the published control package, resolve the route, re-check, then write.
	for (int attempt = 0; attempt < 2; attempt++) {
		PreparedGraphPackage *pkg = control_package.load(std::memory_order_acquire);
		if (!pkg) {
			return;
		}
		SymphonyGraphInput *input = pkg->find_param(p_name);
		if (!input) {
			return;
		}
		if (control_package.load(std::memory_order_acquire) != pkg) {
			continue;
		}
		input->set_value((float)p_value);
		if (control_package.load(std::memory_order_acquire) == pkg) {
			return;
		}
		// Package swapped after the write; retry so the live graph receives the value.
	}
}

Variant AudioStreamPlaybackSymphony::get_parameter(const StringName &p_name) const {
	PreparedGraphPackage *pkg = pending_package.load(std::memory_order_acquire);
	if (!pkg) {
		pkg = control_package.load(std::memory_order_acquire);
	}
	if (!pkg) {
		return Variant();
	}
	SymphonyGraphInput *input = pkg->find_param(p_name);
	if (!input) {
		return Variant();
	}
	return input->get_value();
}

bool AudioStreamPlaybackSymphony::trigger(const StringName &p_name, float p_value) {
	// A restart/swap package already published will go live at the next block: fire into it so the
	// trigger is not lost on the soon-to-be-retired graph.
	if (control_package.load(std::memory_order_acquire) != nullptr) {
		if (PreparedGraphPackage *pending = pending_package.load(std::memory_order_acquire)) {
			if (SymphonyTriggerInput *pending_tin = pending->find_trigger(p_name)) {
				return pending_tin->fire(p_value);
			}
		}
	}
	for (int attempt = 0; attempt < 2; attempt++) {
		PreparedGraphPackage *pkg = control_package.load(std::memory_order_acquire);
		if (!pkg) {
			return false;
		}
		SymphonyTriggerInput *tin = pkg->find_trigger(p_name);
		if (!tin) {
			return false;
		}
		if (control_package.load(std::memory_order_acquire) != pkg) {
			continue;
		}
		const bool ok = tin->fire(p_value);
		if (control_package.load(std::memory_order_acquire) == pkg) {
			return ok;
		}
		// Swapped after enqueue; retry once against the newly published package.
	}
	return false;
}

float AudioStreamPlaybackSymphony::get_voice_cpu_microseconds() const {
	return stat_mix_us_last.load(std::memory_order_relaxed);
}

float AudioStreamPlaybackSymphony::get_budget_percent() const {
	float rate = output_rate_cached.load(std::memory_order_relaxed);
	if (rate <= 0.0f) {
		rate = mix_rate_cached.load(std::memory_order_relaxed);
	}
	const int32_t frames = last_output_frames.load(std::memory_order_relaxed);
	if (frames <= 0 || rate <= 0.0f) {
		return 0.0f;
	}
	const float deadline_us = (float)frames / rate * 1e6f;
	if (deadline_us <= 0.0f) {
		return 0.0f;
	}
	return (stat_mix_us_last.load(std::memory_order_relaxed) / deadline_us) * 100.0f;
}

float AudioStreamPlaybackSymphony::get_last_rms() const {
	return stat_rms.load(std::memory_order_relaxed);
}

int AudioStreamPlaybackSymphony::get_effective_priority() const {
	return cached_priority.load(std::memory_order_relaxed);
}

float AudioStreamPlaybackSymphony::get_estimated_cost_units() const {
	return stat_cost_units.load(std::memory_order_relaxed);
}

bool AudioStreamPlaybackSymphony::fire_source_finished_and_mix(AudioFrame *p_buffer, int p_frames) {
	// execute() clears trigger buffers at the start of each micro-block, so a
	// pre-pushed finished event would be wiped before the stop check. Mirror the
	// mix() finished branch directly after verifying the package is wired.
	if (!current_package) {
		// Test helper runs on main before any mix: adopt the first package like the mixer would.
		if (PreparedGraphPackage *pending = pending_package.exchange(nullptr, std::memory_order_acquire)) {
			_adopt_pending(pending);
		}
	}
	if (!current_package || current_package->source_finished_triggers.is_empty()) {
		return false;
	}
	if (!stream.is_valid() || !stream->get_stop_on_source_finished()) {
		return false;
	}
	if (p_buffer != nullptr) {
		for (int i = 0; i < p_frames; i++) {
			p_buffer[i] = AudioFrame(0, 0);
		}
	}
	active.store(false, std::memory_order_release);
	return true;
}

void AudioStreamPlaybackSymphony::_reclaim_unreachable() {
	// Main thread, voice provably unreachable by any mixer (reaper: refcount==1 after AudioServer
	// dropped it; destructor: refcount 0). Off-main callers retire instead (I5).
#ifdef DEV_ENABLED
	DEV_ASSERT(mix_depth.load(std::memory_order_acquire) == 0);
#endif
	const bool on_main = Thread::is_main_thread();
	auto dispose = [on_main](PreparedGraphPackage *p_package) {
		if (on_main) {
			PreparedGraphPackage::destroy(p_package);
		} else {
			GraphPackageRetirement::retire(p_package);
		}
	};
	_release_crossfade_token();
	if (outgoing_package) {
		_pkg_outgoing(-1);
		dispose(outgoing_package);
		outgoing_package = nullptr;
	}
	if (incoming_package) {
		_pkg_pending(-1);
		dispose(incoming_package);
		incoming_package = nullptr;
	}
	transition_mode = TransitionMode::Idle;
	if (current_package) {
		_pkg_active(-1);
		PreparedGraphPackage *dead = current_package;
		_install_package(nullptr);
		dispose(dead);
	}
	PreparedGraphPackage *pending = pending_package.exchange(nullptr, std::memory_order_acquire);
	if (pending) {
		_pkg_pending(-1);
		dispose(pending);
	}
}

AudioStreamPlaybackSymphony::~AudioStreamPlaybackSymphony() {
	// Refcount 0: no mixer can reach this voice any more.
	_reclaim_unreachable();
	GraphPackageRetirement::drain();
}

void AudioStreamPlaybackSymphony::transition_to_lod(int p_lod_tier) {
	if (!stream.is_valid()) {
		return;
	}
	if (p_lod_tier == main_requested_lod) {
		return;
	}
	if (p_lod_tier < 0 || p_lod_tier >= stream->get_lod_count()) {
		return;
	}

	CompiledGraph *new_graph = stream->compile_lod_graph(p_lod_tier);
	if (!new_graph) {
		return;
	}

	PreparedGraphPackage *pkg = PreparedGraphPackage::create_from_graph(new_graph, 0, 0, p_lod_tier);
	if (!pkg) {
		memdelete(new_graph);
		return;
	}

	pkg->adopt_mode = PreparedGraphPackage::AdoptMode::Lod;
	_pkg_pending(1);
	PreparedGraphPackage *old_pending = pending_package.exchange(pkg, std::memory_order_acq_rel);
	if (old_pending) {
		_pkg_pending(-1);
		PreparedGraphPackage::destroy(old_pending);
	}

	main_requested_lod = p_lod_tier;
}
