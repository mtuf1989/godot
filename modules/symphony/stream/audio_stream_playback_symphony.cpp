#include "audio_stream_playback_symphony.h"
#include "core/object/class_db.h"

void AudioStreamPlaybackSymphony::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_parameter", "name", "value"), &AudioStreamPlaybackSymphony::set_parameter);
	ClassDB::bind_method(D_METHOD("trigger", "name", "value"), &AudioStreamPlaybackSymphony::trigger, DEFVAL(1.0f));
}

void AudioStreamPlaybackSymphony::start(double p_from_pos) {
	active = true;
	// Check if a graph was pre-loaded via swap_graph before play
	CompiledGraph *pending = pending_graph.exchange(nullptr, std::memory_order_acquire);
	if (pending) {
		if (current_graph) {
			cleanup_graveyard();
			graveyard = current_graph;
		}
		current_graph = pending;
		find_graph_output();
	}
}

void AudioStreamPlaybackSymphony::stop() {
	active = false;
	cleanup_graveyard();
	if (current_graph) {
		memdelete(current_graph);
		current_graph = nullptr;
	}
	graph_output_node = nullptr;
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

	// Hot-swap check: pick up new graph if available.
	CompiledGraph *pending = pending_graph.exchange(nullptr, std::memory_order_acquire);
	if (pending) {
		// Old graph goes to graveyard (will be freed on main thread).
		// NOTE Phase 3 limitation: no state migration. New graph starts from silence.
		// See work_doc/phase4_design_notes.md for future ExportState/ImportState plan.
		cleanup_graveyard();
		graveyard = current_graph;
		current_graph = pending;
		find_graph_output();
	}

	if (!current_graph || !graph_output_node) {
		// No graph loaded — output silence
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

		frames_processed += chunk;
	}

	return p_frames;
}

void AudioStreamPlaybackSymphony::swap_graph(CompiledGraph *p_graph) {
	// Store the new graph for the audio thread to pick up.
	CompiledGraph *old_pending = pending_graph.exchange(p_graph, std::memory_order_release);
	// If there was already a pending graph that was never picked up, free it.
	if (old_pending) {
		memdelete(old_pending);
	}
}

void AudioStreamPlaybackSymphony::set_parameter(const StringName &p_name, float p_value) {
	// Phase 3 stub: parameter routing not yet implemented.
	// Will be wired to SafeNumeric slots feeding GraphInput nodes.
}

void AudioStreamPlaybackSymphony::trigger(const StringName &p_name, float p_value) {
	// Phase 3 stub: trigger routing not yet implemented.
	// Will queue a TriggerEvent to the appropriate trigger buffer.
}

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
	// Find the GraphOutput operator (last in topological order typically, but search to be safe)
	for (int32_t i = 0; i < current_graph->operator_count; i++) {
		SymphonyGraphOutput *out = dynamic_cast<SymphonyGraphOutput *>(current_graph->operators[i]);
		if (out) {
			graph_output_node = out;
			break;
		}
	}
}

AudioStreamPlaybackSymphony::~AudioStreamPlaybackSymphony() {
	cleanup_graveyard();
	if (current_graph) {
		memdelete(current_graph);
		current_graph = nullptr;
	}
}
