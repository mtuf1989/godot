#include "audio_stream_playback_symphony.h"

void AudioStreamPlaybackSymphony::_bind_methods() {
}

void AudioStreamPlaybackSymphony::start(double p_from_pos) {
	active = true;

	float mix_rate = stream->get_mix_rate();

	// Wire the hardcoded graph: Oscillator -> Gain -> Output
	oscillator.initialize(osc_buffer, 440.0f, mix_rate);
	gain.initialize(osc_buffer, gain_buffer, 0.5f);
	graph_output.initialize(gain_buffer);
}

void AudioStreamPlaybackSymphony::stop() {
	active = false;
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

	int frames_processed = 0;

	while (frames_processed < p_frames) {
		int chunk = MIN(SYMPHONY_MICRO_BLOCK_SIZE, p_frames - frames_processed);

		// Execute graph in topological order
		oscillator.execute(chunk);
		gain.execute(chunk);

		// Output node writes directly to Godot's AudioFrame buffer
		graph_output.set_output(p_buffer, frames_processed);
		graph_output.execute(chunk);

		frames_processed += chunk;
	}

	return p_frames;
}
