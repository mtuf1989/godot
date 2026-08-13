#include "audio_stream_playback_symphony.h"
#include "../core/symphony_voice_manager.h"
#include "../core/symphony_graph_package_retirement.h"
#include "../core/symphony_fast_math.h"
#include "../core/symphony_platform_time.h"
#include "core/object/class_db.h"
#include "core/os/thread.h"

#include <cmath>

void AudioStreamPlaybackSymphony::_bind_methods() {
	ClassDB::bind_method(D_METHOD("trigger", "name", "value"), &AudioStreamPlaybackSymphony::trigger, DEFVAL(1.0f));
	ClassDB::bind_method(D_METHOD("get_voice_cpu_microseconds"), &AudioStreamPlaybackSymphony::get_voice_cpu_microseconds);
	ClassDB::bind_method(D_METHOD("get_budget_percent"), &AudioStreamPlaybackSymphony::get_budget_percent);
	ClassDB::bind_method(D_METHOD("get_last_rms"), &AudioStreamPlaybackSymphony::get_last_rms);
}

void AudioStreamPlaybackSymphony::_install_package(PreparedGraphPackage *p_package) {
	current_package = p_package;
	current_graph = p_package ? p_package->graph : nullptr;
	graph_output_node = p_package ? p_package->graph_output : nullptr;
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
		GraphPackageRetirement::retire(outgoing_package);
		outgoing_package = nullptr;
	}
	if (incoming_package) {
		GraphPackageRetirement::retire(incoming_package);
		incoming_package = nullptr;
	}
	transition_mode = TransitionMode::Idle;
	transition_progress = 1.0f;
	transition_speed = 0.0f;
}

AudioStreamPlaybackSymphony::AdmitResult AudioStreamPlaybackSymphony::_try_admit_crossfade() {
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
	if (!mgr->try_acquire_crossfade_token()) {
		return AdmitResult::Denied;
	}
	return AdmitResult::AdmittedWithToken;
}

void AudioStreamPlaybackSymphony::_begin_equal_power_crossfade(PreparedGraphPackage *p_new_package) {
	outgoing_package = current_package;
	_install_package(p_new_package);
	transition_mode = TransitionMode::EqualPowerCrossfade;
	transition_progress = 0.0f;
	float samples = mix_rate_cached > 0.0f ? mix_rate_cached * CROSSFADE_SECONDS : 2048.0f;
	transition_speed = 1.0f / samples;
}

void AudioStreamPlaybackSymphony::_begin_fallback_transition(PreparedGraphPackage *p_new_package) {
	_release_crossfade_token();
	if (outgoing_package) {
		GraphPackageRetirement::retire(outgoing_package);
		outgoing_package = nullptr;
	}
	if (incoming_package) {
		GraphPackageRetirement::retire(incoming_package);
	}
	incoming_package = p_new_package;
	if (!current_package) {
		// Nothing to fade out — install and fade in.
		_install_package(incoming_package);
		incoming_package = nullptr;
		transition_mode = TransitionMode::FallbackFadeIn;
	} else {
		transition_mode = TransitionMode::FallbackFadeOut;
	}
	transition_progress = 0.0f;
	transition_speed = 1.0f / (float)FALLBACK_FADE_SAMPLES;
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
}

void AudioStreamPlaybackSymphony::request_lod_tier(int32_t p_lod_tier) {
	requested_lod_tier.store(p_lod_tier, std::memory_order_release);
}

void AudioStreamPlaybackSymphony::request_manager_stop() {
	manager_stop_request.store(true, std::memory_order_release);
}

void AudioStreamPlaybackSymphony::process_manager_requests() {
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

void AudioStreamPlaybackSymphony::start(double p_from_pos) {
	active.store(true, std::memory_order_release);
	_cache_stream_metadata();
	PreparedGraphPackage *pending = pending_package.exchange(nullptr, std::memory_order_acquire);
	if (pending) {
		if (current_package) {
			GraphPackageRetirement::retire(current_package);
		}
		_install_package(pending);
	}

	if (current_package) {
		for (int i = 0; i < current_package->trigger_routes.size(); i++) {
			SymphonyTriggerInput *tin = current_package->trigger_routes[i].input;
			if (tin && tin->get_auto_trigger_on_play()) {
				tin->fire(1.0f);
			}
		}
	}

#ifndef TOOLS_ENABLED
	SymphonyVoiceManager *mgr = SymphonyVoiceManager::get_singleton();
	if (mgr) {
		mgr->register_voice(this);
		registered_with_manager = true;
	}
#endif
}

void AudioStreamPlaybackSymphony::stop() {
	active.store(false, std::memory_order_release);

	if (registered_with_manager) {
		SymphonyVoiceManager *mgr = SymphonyVoiceManager::get_singleton();
		if (mgr) {
			mgr->unregister_voice(this);
		}
		registered_with_manager = false;
	}

	stop_pending.store(true, std::memory_order_relaxed);
}

bool AudioStreamPlaybackSymphony::is_playing() const {
	return active.load(std::memory_order_acquire);
}

int AudioStreamPlaybackSymphony::get_loop_count() const {
	return 0;
}

double AudioStreamPlaybackSymphony::get_playback_position() const {
	return 0.0;
}

void AudioStreamPlaybackSymphony::seek(double p_time) {
}

int AudioStreamPlaybackSymphony::mix(AudioFrame *p_buffer, float p_rate_scale, int p_frames) {
	if (!active.load(std::memory_order_acquire)) {
		if (stop_pending.load(std::memory_order_relaxed)) {
			_finalize_stop();
		}
		return 0;
	}

	uint64_t t_start = symphony_time_usec();

	PreparedGraphPackage *pending = pending_package.exchange(nullptr, std::memory_order_acquire);
	if (pending) {
		pending_is_lod.exchange(false, std::memory_order_relaxed);
		AdmitResult admit = current_package ? _try_admit_crossfade() : AdmitResult::Denied;
		if (admit != AdmitResult::Denied) {
			holds_crossfade_token = (admit == AdmitResult::AdmittedWithToken);
			_begin_equal_power_crossfade(pending);
		} else if (current_package || incoming_package || outgoing_package || transition_mode != TransitionMode::Idle) {
			_begin_fallback_transition(pending);
		} else {
			_install_package(pending);
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
				if (current_package) {
					GraphPackageRetirement::retire(current_package);
				}
				_install_package(incoming_package);
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
	}

	uint64_t t_end = symphony_time_usec();
	last_mix_time_us = (float)(t_end - t_start);
	last_frame_count = p_frames;

	float sum_sq = 0.0f;
	for (int i = 0; i < p_frames; i++) {
		sum_sq += p_buffer[i].left * p_buffer[i].left + p_buffer[i].right * p_buffer[i].right;
	}
	float rms_candidate = sqrtf(sum_sq / (2.0f * (float)p_frames));
	if (unlikely(std::isnan(rms_candidate) || std::isinf(rms_candidate))) {
		rms_candidate = 0.0f;
	}
	last_rms = rms_candidate;

	return p_frames;
}

void AudioStreamPlaybackSymphony::swap_graph(CompiledGraph *p_graph) {
	if (!p_graph) {
		return;
	}

	if (current_graph && p_graph) {
		uint8_t state_buf[256];
		for (int32_t old_i = 0; old_i < current_graph->operator_count; old_i++) {
			size_t state_size = current_graph->operators[old_i]->export_state(nullptr, 0);
			if (state_size == 0 || state_size > sizeof(state_buf)) {
				continue;
			}
			current_graph->operators[old_i]->export_state(state_buf, sizeof(state_buf));
			int32_t old_id = current_graph->node_ids[old_i];
			for (int32_t new_i = 0; new_i < p_graph->operator_count; new_i++) {
				if (p_graph->node_ids[new_i] == old_id) {
					p_graph->operators[new_i]->import_state(state_buf, state_size);
					break;
				}
			}
		}
	}

	PreparedGraphPackage *pkg = PreparedGraphPackage::create_from_graph(p_graph);
	if (!pkg) {
		memdelete(p_graph);
		return;
	}

	PreparedGraphPackage *old_pending = pending_package.exchange(pkg, std::memory_order_release);
	if (old_pending) {
		PreparedGraphPackage::destroy(old_pending);
	}
}

void AudioStreamPlaybackSymphony::set_parameter(const StringName &p_name, const Variant &p_value) {
	if (!current_package) {
		return;
	}
	SymphonyGraphInput *input = current_package->find_param(p_name);
	if (input) {
		input->set_value((float)p_value);
	}
}

bool AudioStreamPlaybackSymphony::trigger(const StringName &p_name, float p_value) {
	if (!current_package) {
		return false;
	}
	SymphonyTriggerInput *tin = current_package->find_trigger(p_name);
	if (!tin) {
		return false;
	}
	return tin->fire(p_value);
}

float AudioStreamPlaybackSymphony::get_voice_cpu_microseconds() const {
	return last_mix_time_us;
}

float AudioStreamPlaybackSymphony::get_budget_percent() const {
	if (last_frame_count == 0 || mix_rate_cached == 0.0f) {
		return 0.0f;
	}
	float deadline_us = (float)last_frame_count / mix_rate_cached * 1e6f;
	return (last_mix_time_us / deadline_us) * 100.0f;
}

float AudioStreamPlaybackSymphony::get_last_rms() const {
	return last_rms;
}

int AudioStreamPlaybackSymphony::get_effective_priority() const {
	return cached_priority;
}

void AudioStreamPlaybackSymphony::_finalize_stop() {
	stop_pending.store(false, std::memory_order_relaxed);

	_abort_transition_packages();

	if (current_package) {
		GraphPackageRetirement::retire(current_package);
		_install_package(nullptr);
	}

	PreparedGraphPackage *pending = pending_package.exchange(nullptr, std::memory_order_acquire);
	if (pending) {
		GraphPackageRetirement::retire(pending);
	}
}

AudioStreamPlaybackSymphony::~AudioStreamPlaybackSymphony() {
	if (active.load(std::memory_order_acquire) && registered_with_manager) {
		SymphonyVoiceManager *mgr = SymphonyVoiceManager::get_singleton();
		if (mgr) {
			mgr->unregister_voice(this);
		}
	}
	_release_crossfade_token();
	if (outgoing_package) {
		PreparedGraphPackage::destroy(outgoing_package);
		outgoing_package = nullptr;
	}
	if (incoming_package) {
		PreparedGraphPackage::destroy(incoming_package);
		incoming_package = nullptr;
	}
	if (current_package) {
		PreparedGraphPackage::destroy(current_package);
		_install_package(nullptr);
	}
	PreparedGraphPackage *pending = pending_package.exchange(nullptr, std::memory_order_acquire);
	if (pending) {
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
	PreparedGraphPackage *old_pending = pending_package.exchange(pkg, std::memory_order_acq_rel);
	if (old_pending) {
		PreparedGraphPackage::destroy(old_pending);
	}

	current_lod_tier = p_lod_tier;
}
