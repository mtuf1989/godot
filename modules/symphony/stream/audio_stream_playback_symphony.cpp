#include "audio_stream_playback_symphony.h"
#include "../core/symphony_voice_manager.h"
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

void AudioStreamPlaybackSymphony::start(double p_from_pos) {
	active.store(true, std::memory_order_release);
	if (stream.is_valid()) {
		mix_rate_cached = stream->get_mix_rate();
	}
	CompiledGraph *pending = pending_graph.exchange(nullptr, std::memory_order_acquire);
	if (pending) {
		if (current_graph) {
			cleanup_graveyard();
			graveyard = current_graph;
		}
		current_graph = pending;
		find_graph_output();
		rebuild_routing_tables();
	}

	// Auto-fire triggers that have auto_trigger_on_play enabled.
	for (const KeyValue<StringName, SymphonyTriggerInput *> &E : trigger_map) {
		if (E.value->get_auto_trigger_on_play()) {
			E.value->fire(1.0f);
		}
	}

	// Only register with voice manager during game runtime, not in the editor.
	// In the editor, voice limiting is unnecessary and the mix callback's
	// enforce_voice_limits() can call stop() on playbacks mid-frame, causing
	// graph destruction while AudioServer still expects the playback to be alive.
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

	// Defer graph deletion to the next mix() call on the audio thread.
	// This ensures no one is mid-execution on the graph when we delete it.
	stop_pending = true;
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
		// Finalize deferred stop: delete graph safely now that we know
		// no one is mid-execution (we're the only reader of current_graph).
		if (stop_pending) {
			_finalize_stop();
		}
		return 0;
	}

	uint64_t t_start = symphony_time_usec();

	// Hot-swap check: pick up new graph if available.
	CompiledGraph *pending = pending_graph.exchange(nullptr, std::memory_order_acquire);
	if (pending) {
		bool is_lod = pending_is_lod.exchange(false, std::memory_order_acquire);
		if (is_lod && current_graph) {
			// LOD transition: set up parallel crossfade from current → new.
			// If already in a crossfade, discard the old outgoing graph.
			if (lod_outgoing_graph) {
				memdelete(lod_outgoing_graph);
			}
			lod_outgoing_graph = current_graph;
			lod_outgoing_output = graph_output_node;
			lod_crossfade_progress = 0.0f;
			lod_crossfade_speed = 1.0f / (float)LOD_CROSSFADE_SAMPLES;
		} else {
			// Regular hot-swap: graveyard the old graph.
			cleanup_graveyard();
			graveyard = current_graph;
		}
		current_graph = pending;
		find_graph_output();
		rebuild_routing_tables();
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

		// LOD crossfade: if outgoing graph exists, execute it too and blend
		if (lod_outgoing_graph && lod_outgoing_output && lod_crossfade_progress < 1.0f) {
			// Temporary buffer for outgoing graph output
			AudioFrame outgoing_buf[SYMPHONY_MICRO_BLOCK_SIZE];
			lod_outgoing_output->set_output(outgoing_buf, 0);
			lod_outgoing_graph->execute(chunk);

			// Blend: crossfade from outgoing to current
			for (int s = 0; s < chunk; s++) {
				float mix_new = lod_crossfade_progress;
				float mix_old = 1.0f - mix_new;
				int buf_idx = frames_processed + s;
				p_buffer[buf_idx].left = p_buffer[buf_idx].left * mix_new + outgoing_buf[s].left * mix_old;
				p_buffer[buf_idx].right = p_buffer[buf_idx].right * mix_new + outgoing_buf[s].right * mix_old;

				lod_crossfade_progress += lod_crossfade_speed;
				if (lod_crossfade_progress >= 1.0f) {
					lod_crossfade_progress = 1.0f;
					lod_crossfade_speed = 0.0f;
					break; // Crossfade complete mid-chunk
				}
			}

			// If crossfade complete, destroy outgoing graph
			if (lod_crossfade_progress >= 1.0f) {
				memdelete(lod_outgoing_graph);
				lod_outgoing_graph = nullptr;
				lod_outgoing_output = nullptr;
			}
		}

		frames_processed += chunk;
	}

	// Timing
	uint64_t t_end = symphony_time_usec();
	last_mix_time_us = (float)(t_end - t_start);
	last_frame_count = p_frames;

	// RMS computation (both channels)
	float sum_sq = 0.0f;
	for (int i = 0; i < p_frames; i++) {
		sum_sq += p_buffer[i].left * p_buffer[i].left + p_buffer[i].right * p_buffer[i].right;
	}
	float rms_candidate = sqrtf(sum_sq / (2.0f * (float)p_frames));
	// Guard against NaN/Inf from broken upstream operators propagating into
	// voice manager stealing decisions (Bug 2 pattern: chained math domain errors).
	if (unlikely(std::isnan(rms_candidate) || std::isinf(rms_candidate))) {
		rms_candidate = 0.0f;
	}
	last_rms = rms_candidate;

	return p_frames;
}

void AudioStreamPlaybackSymphony::swap_graph(CompiledGraph *p_graph) {
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

	CompiledGraph *old_pending = pending_graph.exchange(p_graph, std::memory_order_release);
	if (old_pending) {
		memdelete(old_pending);
	}
}

void AudioStreamPlaybackSymphony::set_parameter(const StringName &p_name, const Variant &p_value) {
	SymphonyGraphInput **ptr = parameter_map.getptr(p_name);
	if (ptr) {
		(*ptr)->set_value((float)p_value);
	}
}

void AudioStreamPlaybackSymphony::trigger(const StringName &p_name, float p_value) {
	SymphonyTriggerInput **ptr = trigger_map.getptr(p_name);
	if (ptr) {
		(*ptr)->fire(p_value);
	}
}

// --- Profiling API ---

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
	if (stream.is_valid()) {
		return stream->get_voice_priority();
	}
	return 50;
}

// --- Internal ---

void AudioStreamPlaybackSymphony::cleanup_graveyard() {
	if (graveyard) {
		memdelete(graveyard);
		graveyard = nullptr;
	}
}

void AudioStreamPlaybackSymphony::_finalize_stop() {
	// Called from the audio thread (inside mix()) when stop_pending is true.
	// Safe to delete the graph here because mix() has already returned early
	// due to active==false, so no code path is using current_graph.
	stop_pending = false;

	cleanup_graveyard();
	if (current_graph) {
		memdelete(current_graph);
		current_graph = nullptr;
	}
	if (lod_outgoing_graph) {
		memdelete(lod_outgoing_graph);
		lod_outgoing_graph = nullptr;
		lod_outgoing_output = nullptr;
	}
	graph_output_node = nullptr;
	parameter_map.clear();
	trigger_map.clear();

	// Discard any pending graph that was queued for hot-swap.
	CompiledGraph *pending = pending_graph.exchange(nullptr, std::memory_order_acquire);
	if (pending) {
		memdelete(pending);
	}
}

void AudioStreamPlaybackSymphony::find_graph_output() {
	graph_output_node = nullptr;
	if (!current_graph) {
		return;
	}
	for (int32_t i = 0; i < current_graph->operator_count; i++) {
		SymphonyGraphOutput *out = dynamic_cast<SymphonyGraphOutput *>(current_graph->operators[i]);
		if (out) {
			graph_output_node = out;
			break;
		}
	}
}

void AudioStreamPlaybackSymphony::rebuild_routing_tables() {
	parameter_map.clear();
	trigger_map.clear();
	if (!current_graph) {
		return;
	}
	for (int32_t i = 0; i < current_graph->operator_count; i++) {
		SymphonyGraphInput *gi = dynamic_cast<SymphonyGraphInput *>(current_graph->operators[i]);
		if (gi && current_graph->node_names[i] != StringName()) {
			parameter_map[current_graph->node_names[i]] = gi;
			continue;
		}
		SymphonyTriggerInput *ti = dynamic_cast<SymphonyTriggerInput *>(current_graph->operators[i]);
		if (ti && current_graph->node_names[i] != StringName()) {
			trigger_map[current_graph->node_names[i]] = ti;
		}
	}
}

AudioStreamPlaybackSymphony::~AudioStreamPlaybackSymphony() {
	if (active.load(std::memory_order_acquire) && registered_with_manager) {
		SymphonyVoiceManager *mgr = SymphonyVoiceManager::get_singleton();
		if (mgr) {
			mgr->unregister_voice(this);
		}
	}
	// If stop was deferred but mix() was never called again (e.g., scene exit),
	// finalize here. This runs on the main thread but is safe because the
	// AudioServer has already removed this playback from its processing list.
	cleanup_graveyard();
	if (current_graph) {
		memdelete(current_graph);
		current_graph = nullptr;
	}
	if (lod_outgoing_graph) {
		memdelete(lod_outgoing_graph);
		lod_outgoing_graph = nullptr;
	}
	CompiledGraph *pending = pending_graph.exchange(nullptr, std::memory_order_acquire);
	if (pending) {
		memdelete(pending);
	}
}

void AudioStreamPlaybackSymphony::transition_to_lod(int p_lod_tier) {
	if (!stream.is_valid()) {
		return;
	}
	if (p_lod_tier == current_lod_tier) {
		return; // Already at requested LOD
	}
	if (p_lod_tier < 0 || p_lod_tier >= stream->get_lod_count()) {
		return; // Invalid tier
	}

	// Compile the new LOD graph on the main thread.
	CompiledGraph *new_graph = stream->compile_lod_graph(p_lod_tier);
	if (!new_graph) {
		return; // Compilation failed
	}

	// Publish via the atomic pending_graph slot.
	// The audio thread will pick this up at the next mix() block boundary
	// and initiate the crossfade there (thread-safe).
	pending_is_lod.store(true, std::memory_order_release);
	CompiledGraph *old_pending = pending_graph.exchange(new_graph, std::memory_order_acq_rel);
	if (old_pending) {
		memdelete(old_pending);
	}

	current_lod_tier = p_lod_tier;
}
