#pragma once

#include "servers/audio/audio_stream.h"
#include "audio_stream_symphony.h"
#include "../core/symphony_pin_types.h"
#include "../nodes/generators/symphony_oscillator.h"
#include "../nodes/envelopes/symphony_gain.h"
#include "../nodes/io/symphony_graph_output.h"

class AudioStreamPlaybackSymphony : public AudioStreamPlayback {
	GDCLASS(AudioStreamPlaybackSymphony, AudioStreamPlayback)
	friend class AudioStreamSymphony;

private:
	Ref<AudioStreamSymphony> stream;
	bool active = false;

	// Pre-allocated mono buffers (one per operator output)
	float osc_buffer[SYMPHONY_MICRO_BLOCK_SIZE] = {};
	float gain_buffer[SYMPHONY_MICRO_BLOCK_SIZE] = {};

	// Hardcoded operators
	SymphonyOscillator oscillator;
	SymphonyGain gain;
	SymphonyGraphOutput graph_output;

protected:
	static void _bind_methods();

public:
	virtual void start(double p_from_pos = 0.0) override;
	virtual void stop() override;
	virtual bool is_playing() const override;
	virtual int get_loop_count() const override;
	virtual double get_playback_position() const override;
	virtual void seek(double p_time) override;
	virtual int mix(AudioFrame *p_buffer, float p_rate_scale, int p_frames) override;
};
