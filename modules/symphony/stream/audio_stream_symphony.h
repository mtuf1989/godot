#pragma once

#include "servers/audio/audio_stream.h"

class AudioStreamPlaybackSymphony;

class AudioStreamSymphony : public AudioStream {
	GDCLASS(AudioStreamSymphony, AudioStream)

private:
	friend class AudioStreamPlaybackSymphony;
	float mix_rate = 44100.0f;

protected:
	static void _bind_methods();

public:
	void set_mix_rate(float p_mix_rate);
	float get_mix_rate() const;

	virtual Ref<AudioStreamPlayback> instantiate_playback() override;
	virtual String get_stream_name() const override;
	virtual double get_length() const override;
	virtual bool is_monophonic() const override;
};
