#include "audio_stream_symphony.h"
#include "audio_stream_playback_symphony.h"

#include "core/object/class_db.h"

void AudioStreamSymphony::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_mix_rate", "rate"), &AudioStreamSymphony::set_mix_rate);
	ClassDB::bind_method(D_METHOD("get_mix_rate"), &AudioStreamSymphony::get_mix_rate);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mix_rate", PROPERTY_HINT_RANGE, "22050,96000,1"), "set_mix_rate", "get_mix_rate");
}

void AudioStreamSymphony::set_mix_rate(float p_mix_rate) {
	mix_rate = p_mix_rate;
}

float AudioStreamSymphony::get_mix_rate() const {
	return mix_rate;
}

Ref<AudioStreamPlayback> AudioStreamSymphony::instantiate_playback() {
	Ref<AudioStreamPlaybackSymphony> playback;
	playback.instantiate();
	playback->stream = Ref<AudioStreamSymphony>(this);
	return playback;
}

String AudioStreamSymphony::get_stream_name() const {
	return "Symphony";
}

double AudioStreamSymphony::get_length() const {
	return 0.0; // Infinite/procedural
}

bool AudioStreamSymphony::is_monophonic() const {
	return false;
}
