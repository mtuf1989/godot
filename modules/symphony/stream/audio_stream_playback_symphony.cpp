#include "audio_stream_playback_symphony.h"
#include "../core/symphony_voice_manager.h"
#include "../core/symphony_platform_time.h"
#include "core/object/class_db.h"

void AudioStreamPlaybackSymphony::_bind_methods() {
	ClassDB::bind_method(D_METHOD("trigger", "name", "value"), &AudioStreamPlaybackSymphony::trigger, DEFVAL(1.0f));
	ClassDB::bind_method(D_METHOD("get_voice_cpu_microseconds"), &AudioStreamPlaybackSymphony::get_voice_cpu_microseconds);
	ClassDB::bind_method(D_METHOD("get_budget_percent"), &AudioStreamPlaybackSymphony::get_budget_percent);
	ClassDB::bind_method(D_METHOD("get_last_rms"), &AudioStreamPlaybackSymphony::get_last_rms);
}

void AudioStreamPlaybackSymphony::start(double p_from_pos) {
	active = true;
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

	SymphonyVoiceManager *mgr = SymphonyVoiceManager::get_singleton();
	if (mgr) {
		mgr->register_voice(this);
	}
}

void AudioStreamPlaybackSymphony::stop() {
	active = false;

	SymphonyVoiceManager *mgr = SymphonyVoiceManager::get_singleton();
	if (mgr) {
		mgr->unregister_voice(this);
	}

	cleanup_graveyard();
	if (current_graph) {
		memdelete(current_graph);
		current_graph = nullptr;
	}
	graph_output_node = nullptr;
	parameter_map.clear();
	trigger_map.clear();
}

bool AudioStreamPlaybackSymphony::is_playing() const {
	return active;
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
	if (!active) {
		return 0;
	}

	uint64_t t_start = symphony_time_usec();

	// Hot-swap check: pick up new graph if available.
	CompiledGraph *pending = pending_graph.exchange(nullptr, std::memory_order_acquire);
	if (pending) {
		cleanup_graveyard();
		graveyard = current_graph;
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
		frames_processed += chunk;
	}

	// Timing
	uint64_t t_end = symphony_time_usec();
	last_mix_time_us = (float)(t_end - t_start);
	last_frame_count = p_frames;

	// RMS computation (cheap: sum of squares from output buffer)
	float sum_sq = 0.0f;
	for (int i = 0; i < p_frames; i++) {
		float s = p_buffer[i].left;
		sum_sq += s * s;
	}
	last_rms = sqrtf(sum_sq / (float)p_frames);

	// Enforce voice limits (stealing) after metrics are updated.
	SymphonyVoiceManager *mgr = SymphonyVoiceManager::get_singleton();
	if (mgr) {
		mgr->enforce_voice_limits();
	}

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
	if (active) {
		SymphonyVoiceManager *mgr = SymphonyVoiceManager::get_singleton();
		if (mgr) {
			mgr->unregister_voice(this);
		}
	}
	cleanup_graveyard();
	if (current_graph) {
		memdelete(current_graph);
		current_graph = nullptr;
	}
}
