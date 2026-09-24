#include "audio_stream_playback_symphony.h"
#include "../core/symphony_voice_manager.h"
#include "../core/symphony_graph_package_retirement.h"
#include "../core/symphony_memory_budget.h"
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
}

void AudioStreamPlaybackSymphony::_install_package(PreparedGraphPackage *p_package) {
	current_package = p_package;
	current_graph = p_package ? p_package->graph : nullptr;
	graph_output_node = p_package ? p_package->graph_output : nullptr;
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
	const int frames = last_output_frames > 0 ? last_output_frames : 512;
	const float budget_rate = output_rate_cached > 0.0f ? output_rate_cached : mix_rate_cached;
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
	float samples = mix_rate_cached > 0.0f ? mix_rate_cached * CROSSFADE_SECONDS : 2048.0f;
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
		mix_rate_cached = 44100.0f;
		cached_priority = 50;
		cached_max_lod = 0;
		return;
	}
	mix_rate_cached = stream->get_mix_rate();
	cached_priority = stream->get_voice_priority();
	cached_max_lod = MAX(0, stream->get_lod_count() - 1);
	cached_duration_limit = stream->get_duration_limit_seconds();
	output_rate_cached = mix_rate_cached;
	if (AudioServer::get_singleton()) {
		const float server_rate = AudioServer::get_singleton()->get_mix_rate();
		if (server_rate > 0.0f) {
			output_rate_cached = server_rate;
		}
	}
}

void AudioStreamPlaybackSymphony::request_lod_tier(int32_t p_lod_tier) {
	requested_lod_tier.store(p_lod_tier, std::memory_order_release);
}

void AudioStreamPlaybackSymphony::request_manager_stop() {
	manager_stop_request.store(true, std::memory_order_release);
}

void AudioStreamPlaybackSymphony::process_manager_requests() {
	// Main thread only: stop() / LOD compile may touch ObjectDB and containers.
	symphony_rt_note(SymphonyRTViolation::ObjectDB, "AudioStreamPlaybackSymphony::process_manager_requests");
	if (manager_stop_request.exchange(false, std::memory_order_acquire)) {
		stop();
		requested_lod_tier.store(-1, std::memory_order_relaxed);
		return;
	}
	int32_t lod = requested_lod_tier.exchange(-1, std::memory_order_acquire);
	if (lod >= 0) {
		transition_to_lod(lod);
	}
}

void AudioStreamPlaybackSymphony::_register_with_manager() {
	if (registered_with_manager) {
		return;
	}
	SymphonyVoiceManager *mgr = SymphonyVoiceManager::get_singleton();
	if (mgr && mgr->register_voice(this)) {
		registered_with_manager = true;
	}
}

void AudioStreamPlaybackSymphony::_arm_release_grace() {
	bool expected = false;
	if (!release_pending.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
		return;
	}
	release_grace.store(RELEASE_GRACE_CALLBACKS, std::memory_order_release);
	if (!registered_with_manager && Thread::is_main_thread()) {
		_finalize_stop();
		release_pending.store(false, std::memory_order_release);
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

bool AudioStreamPlaybackSymphony::tick_release_grace() {
	if (!release_pending.load(std::memory_order_acquire)) {
		return false;
	}
	int32_t left = release_grace.load(std::memory_order_relaxed);
	if (left > 0) {
		left = release_grace.fetch_sub(1, std::memory_order_acq_rel) - 1;
	}
	if (left <= 0) {
		release_ready.store(true, std::memory_order_release);
		return true;
	}
	return false;
}

void AudioStreamPlaybackSymphony::start(double p_from_pos) {
	if (p_from_pos != 0.0) {
		_report_unsupported_seek();
	}
	active.store(true, std::memory_order_release);
	release_pending.store(false, std::memory_order_release);
	release_ready.store(false, std::memory_order_release);
	release_grace.store(0, std::memory_order_release);
	position_us.store(0, std::memory_order_release);
	stop_pending.store(false, std::memory_order_release);
	_cache_stream_metadata();
	PreparedGraphPackage *pending = pending_package.exchange(nullptr, std::memory_order_acquire);
	if (pending) {
		if (current_package) {
			_pkg_active(-1);
			GraphPackageRetirement::retire(current_package);
		}
		_install_package(pending);
		_pkg_pending(-1);
		_pkg_active(1);
	}

	if (current_package) {
		for (int i = 0; i < current_package->trigger_routes.size(); i++) {
			SymphonyTriggerInput *tin = current_package->trigger_routes[i].input;
			if (tin && tin->get_auto_trigger_on_play()) {
				tin->fire(1.0f);
			}
		}
	}

	begin_resample();
	if (active.load(std::memory_order_acquire)) {
		_register_with_manager();
	} else if (Thread::is_main_thread()) {
		_finalize_stop();
	}
}

void AudioStreamPlaybackSymphony::stop() {
	active.store(false, std::memory_order_release);
	release_pending.store(false, std::memory_order_release);
	release_ready.store(false, std::memory_order_release);
	release_grace.store(0, std::memory_order_release);
	stop_pending.store(true, std::memory_order_relaxed);

	if (!Thread::is_main_thread()) {
		_arm_release_grace();
		return;
	}

	// Keep this object alive across unregister. Dropping the manager's ref can
	// be the last reference when a player has already released the playback.
	Ref<AudioStreamPlaybackSymphony> self_hold(this);
	if (registered_with_manager) {
		SymphonyVoiceManager *mgr = SymphonyVoiceManager::get_singleton();
		if (mgr) {
			mgr->unregister_voice(this);
		}
		registered_with_manager = false;
	}
	_finalize_stop();
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
	return mix_rate_cached > 0.0f ? mix_rate_cached : 44100.0f;
}

int AudioStreamPlaybackSymphony::_mix_internal(AudioFrame *p_buffer, int p_frames) {
	if (!active.load(std::memory_order_acquire) || p_frames <= 0) {
		return 0;
	}

	PreparedGraphPackage *pending = pending_package.exchange(nullptr, std::memory_order_acquire);
	if (pending) {
		pending_is_lod.exchange(false, std::memory_order_relaxed);
		AdmitResult admit = current_package ? _try_admit_crossfade(pending) : AdmitResult::Denied;
		if (admit != AdmitResult::Denied) {
			holds_crossfade_token = (admit == AdmitResult::AdmittedWithToken);
			_begin_equal_power_crossfade(pending);
		} else if (current_package || incoming_package || outgoing_package || transition_mode != TransitionMode::Idle) {
			_begin_fallback_transition(pending);
		} else {
			_pkg_pending(-1);
			_install_package(pending);
			_pkg_active(1);
		}
	}

	if (!current_graph || !graph_output_node) {
		for (int i = 0; i < p_frames; i++) {
			p_buffer[i] = AudioFrame(0, 0);
		}
		last_mix_time_us = 0.0f;
		last_rms = 0.0f;
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
				if (current_package) {
					_pkg_active(-1);
					GraphPackageRetirement::retire(current_package);
				}
				_pkg_pending(-1);
				_install_package(incoming_package);
				_pkg_active(1);
				incoming_package = nullptr;
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
			stop_pending.store(true, std::memory_order_relaxed);
			break;
		}
	}

	return frames_processed;
}

int AudioStreamPlaybackSymphony::mix(AudioFrame *p_buffer, float p_rate_scale, int p_frames) {
	SymphonyRealtimeScope rt_scope;
	if (!active.load(std::memory_order_acquire) || p_frames <= 0) {
		return 0;
	}

	const uint64_t t_start = symphony_time_usec();
	int mixed = 0;
	float out_rate = output_rate_cached > 0.0f ? output_rate_cached : mix_rate_cached;
	float speed = p_rate_scale;
	if (AudioServer::get_singleton()) {
		mixed = AudioStreamPlaybackResampled::mix(p_buffer, p_rate_scale, p_frames);
		const float server_rate = AudioServer::get_singleton()->get_mix_rate();
		if (server_rate > 0.0f) {
			out_rate = server_rate;
			output_rate_cached = server_rate;
		}
		speed *= AudioServer::get_singleton()->get_playback_speed_scale();
	} else {
		// Native unit tests mix without an audio device. Generate at the graph rate;
		// device-rate resampling runs when AudioServer is alive.
		out_rate = mix_rate_cached > 0.0f ? mix_rate_cached : 44100.0f;
		output_rate_cached = out_rate;
		mixed = _mix_internal(p_buffer, p_frames);
	}
	last_mix_time_us = (float)(symphony_time_usec() - t_start);
	last_output_frames = p_frames;
	last_frame_count = p_frames;

	if (out_rate <= 0.0f) {
		out_rate = 44100.0f;
	}
	if (mixed > 0 && speed > 0.0f) {
		const double add_us = ((double)mixed / (double)out_rate) * (double)speed * 1000000.0;
		if (add_us > 0.0) {
			position_us.fetch_add((uint64_t)add_us, std::memory_order_acq_rel);
		}
	}
	if (cached_duration_limit > 0.0 && (double)position_us.load(std::memory_order_acquire) / 1000000.0 >= cached_duration_limit) {
		active.store(false, std::memory_order_release);
		stop_pending.store(true, std::memory_order_relaxed);
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
	last_rms = rms_candidate;

	if (!active.load(std::memory_order_acquire)) {
		_arm_release_grace();
	}
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

	_pkg_pending(1);
	PreparedGraphPackage *old_pending = pending_package.exchange(pkg, std::memory_order_release);
	if (old_pending) {
		_pkg_pending(-1);
		PreparedGraphPackage::destroy(old_pending);
	}
}

void AudioStreamPlaybackSymphony::set_parameter(const StringName &p_name, const Variant &p_value) {
	// Main thread: never touch current_package directly (audio may swap it).
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
	PreparedGraphPackage *pkg = control_package.load(std::memory_order_acquire);
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
	return last_mix_time_us;
}

float AudioStreamPlaybackSymphony::get_budget_percent() const {
	const float rate = output_rate_cached > 0.0f ? output_rate_cached : mix_rate_cached;
	if (last_output_frames <= 0 || rate <= 0.0f) {
		return 0.0f;
	}
	const float deadline_us = (float)last_output_frames / rate * 1e6f;
	if (deadline_us <= 0.0f) {
		return 0.0f;
	}
	return (last_mix_time_us / deadline_us) * 100.0f;
}

float AudioStreamPlaybackSymphony::get_last_rms() const {
	return last_rms;
}

int AudioStreamPlaybackSymphony::get_effective_priority() const {
	return cached_priority;
}

float AudioStreamPlaybackSymphony::get_estimated_cost_units() const {
	PreparedGraphPackage *pkg = control_package.load(std::memory_order_acquire);
	return pkg ? pkg->estimated_cost_units : 0.0f;
}

bool AudioStreamPlaybackSymphony::fire_source_finished_and_mix(AudioFrame *p_buffer, int p_frames) {
	// execute() clears trigger buffers at the start of each micro-block, so a
	// pre-pushed finished event would be wiped before the stop check. Mirror the
	// mix() finished branch directly after verifying the package is wired.
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
	stop_pending.store(true, std::memory_order_relaxed);
	_arm_release_grace();
	return true;
}

void AudioStreamPlaybackSymphony::_finalize_stop() {
	stop_pending.store(false, std::memory_order_relaxed);

	_abort_transition_packages();

	if (current_package) {
		_pkg_active(-1);
		GraphPackageRetirement::retire(current_package);
		_install_package(nullptr);
	}

	PreparedGraphPackage *pending = pending_package.exchange(nullptr, std::memory_order_acquire);
	if (pending) {
		_pkg_pending(-1);
		GraphPackageRetirement::retire(pending);
	}
}

AudioStreamPlaybackSymphony::~AudioStreamPlaybackSymphony() {
	if (registered_with_manager) {
		SymphonyVoiceManager *mgr = SymphonyVoiceManager::get_singleton();
		if (mgr) {
			mgr->abandon_voice_for_destructor(this);
		}
		registered_with_manager = false;
	}
	if (!Thread::is_main_thread()) {
		_finalize_stop();
		_release_crossfade_token();
		return;
	}
	_release_crossfade_token();
	if (outgoing_package) {
		_pkg_outgoing(-1);
		PreparedGraphPackage::destroy(outgoing_package);
		outgoing_package = nullptr;
	}
	if (incoming_package) {
		_pkg_pending(-1);
		PreparedGraphPackage::destroy(incoming_package);
		incoming_package = nullptr;
	}
	if (current_package) {
		_pkg_active(-1);
		PreparedGraphPackage::destroy(current_package);
		_install_package(nullptr);
	}
	PreparedGraphPackage *pending = pending_package.exchange(nullptr, std::memory_order_acquire);
	if (pending) {
		_pkg_pending(-1);
		PreparedGraphPackage::destroy(pending);
	}
	GraphPackageRetirement::drain();
}

void AudioStreamPlaybackSymphony::transition_to_lod(int p_lod_tier) {
	if (!stream.is_valid()) {
		return;
	}
	if (p_lod_tier == current_lod_tier) {
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

	pending_is_lod.store(true, std::memory_order_release);
	_pkg_pending(1);
	PreparedGraphPackage *old_pending = pending_package.exchange(pkg, std::memory_order_acq_rel);
	if (old_pending) {
		_pkg_pending(-1);
		PreparedGraphPackage::destroy(old_pending);
	}

	current_lod_tier = p_lod_tier;
}
