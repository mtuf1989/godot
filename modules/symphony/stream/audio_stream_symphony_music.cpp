/**************************************************************************/
/*  audio_stream_symphony_music.cpp                                       */
/**************************************************************************/

#include "audio_stream_symphony_music.h"
#include "audio_stream_playback_symphony_music.h"
#include "../core/symphony_rate_policy.h"
#include "../core/symphony_seed.h"
#include "../runtime/symphony_music_stereo_pcm.h"
#include "../runtime/symphony_note_pool.h"
#include "../runtime/sound_event.h"

#include "core/object/class_db.h"
#include "core/os/thread.h"
#include "servers/audio/audio_server.h"

void AudioStreamSymphonyMusic::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_mix_rate", "rate"), &AudioStreamSymphonyMusic::set_mix_rate);
	ClassDB::bind_method(D_METHOD("get_mix_rate"), &AudioStreamSymphonyMusic::get_mix_rate);
	ClassDB::bind_method(D_METHOD("set_initial_bpm", "bpm"), &AudioStreamSymphonyMusic::set_initial_bpm);
	ClassDB::bind_method(D_METHOD("get_initial_bpm"), &AudioStreamSymphonyMusic::get_initial_bpm);
	ClassDB::bind_method(D_METHOD("set_track_instrument", "track_id", "instrument", "max_voices", "protected"), &AudioStreamSymphonyMusic::set_track_instrument, DEFVAL(8), DEFVAL(false));
	ClassDB::bind_method(D_METHOD("set_track_release_seconds", "track_id", "seconds"), &AudioStreamSymphonyMusic::set_track_release_seconds);
	ClassDB::bind_method(D_METHOD("set_track_gain", "track_id", "gain"), &AudioStreamSymphonyMusic::set_track_gain);
	ClassDB::bind_method(D_METHOD("get_track_count"), &AudioStreamSymphonyMusic::get_track_count);
	ClassDB::bind_method(D_METHOD("register_clip_pcm", "key", "interleaved_stereo", "sample_rate", "source_bpm", "body_start", "body_end"), &AudioStreamSymphonyMusic::register_clip_pcm, DEFVAL(120.0f), DEFVAL(0), DEFVAL(0));
	ClassDB::bind_method(D_METHOD("find_clip_index", "key"), &AudioStreamSymphonyMusic::find_clip_index);
	ClassDB::bind_method(D_METHOD("get_clip_count"), &AudioStreamSymphonyMusic::get_clip_count);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mix_rate"), "set_mix_rate", "get_mix_rate");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "initial_bpm"), "set_initial_bpm", "get_initial_bpm");
}

void AudioStreamSymphonyMusic::set_mix_rate(float p_mix_rate) {
	mix_rate = symphony_select_music_rate(p_mix_rate);
}

void AudioStreamSymphonyMusic::set_initial_bpm(float p_bpm) {
	initial_bpm = CLAMP(p_bpm, 1.0f, 999.0f);
}

Error AudioStreamSymphonyMusic::set_track_instrument(int32_t p_track_id, const Ref<AudioStreamSymphony> &p_instrument, int32_t p_max_voices, bool p_protected) {
	ERR_FAIL_COND_V(p_track_id < 0 || p_track_id > 255, ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V(p_instrument.is_null(), ERR_INVALID_PARAMETER);
	const GraphDescription &desc = p_instrument->get_graph_description();
	ERR_FAIL_COND_V(desc.nodes.is_empty(), ERR_INVALID_PARAMETER);

	int slot = -1;
	for (int i = 0; i < track_count; i++) {
		if (tracks[i].track_id == p_track_id) {
			slot = i;
			break;
		}
	}
	if (slot < 0) {
		ERR_FAIL_COND_V(track_count >= MAX_TRACKS, ERR_OUT_OF_MEMORY);
		slot = track_count++;
	}
	TrackConfig &t = tracks[slot];
	t.track_id = p_track_id;
	t.instrument = desc;
	t.max_voices = CLAMP(p_max_voices, 1, SymphonyNotePool::MAX_VOICES);
	t.protected_track = p_protected;
	t.valid = true;
	return OK;
}

void AudioStreamSymphonyMusic::set_track_release_seconds(int32_t p_track_id, float p_seconds) {
	for (int i = 0; i < track_count; i++) {
		if (tracks[i].track_id == p_track_id) {
			tracks[i].release_seconds = MAX(0.01f, p_seconds);
			return;
		}
	}
}

void AudioStreamSymphonyMusic::set_track_gain(int32_t p_track_id, float p_gain) {
	for (int i = 0; i < track_count; i++) {
		if (tracks[i].track_id == p_track_id) {
			tracks[i].gain = CLAMP(p_gain, 0.0f, 4.0f);
			return;
		}
	}
}

Error AudioStreamSymphonyMusic::register_clip_pcm(const StringName &p_key, const PackedFloat32Array &p_interleaved_stereo, float p_sample_rate, float p_source_bpm, int32_t p_body_start, int32_t p_body_end) {
	ERR_FAIL_COND_V(p_key == StringName(), ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V(p_interleaved_stereo.size() < 2 || (p_interleaved_stereo.size() & 1), ERR_INVALID_PARAMETER);
	SymphonyMusicStereoCache *cache = SymphonyMusicStereoCache::get_singleton();
	ERR_FAIL_COND_V(!cache, ERR_UNAVAILABLE);

	const int32_t frames = p_interleaved_stereo.size() / 2;
	const float rate = symphony_select_music_rate(p_sample_rate);
	const float *pcm = cache->acquire(p_key, p_interleaved_stereo.ptr(), frames, rate);
	ERR_FAIL_COND_V(!pcm, ERR_OUT_OF_MEMORY);

	int slot = find_clip_index(p_key);
	if (slot < 0) {
		ERR_FAIL_COND_V(clip_count >= MAX_CLIPS, ERR_OUT_OF_MEMORY);
		slot = clip_count++;
	} else if (clips[slot].registered) {
		cache->release(clips[slot].key);
	}

	ClipConfig &c = clips[slot];
	c.key = p_key;
	c.desc = {};
	c.desc.pcm_key = p_key;
	c.desc.pcm = pcm;
	c.desc.frames = frames;
	c.desc.sample_rate = rate;
	c.desc.source_bpm = CLAMP(p_source_bpm, 1.0f, 999.0f);
	c.desc.body_start = CLAMP(p_body_start, 0, frames);
	c.desc.body_end = (p_body_end <= 0) ? frames : CLAMP(p_body_end, c.desc.body_start, frames);
	c.registered = true;
	return OK;
}

int32_t AudioStreamSymphonyMusic::find_clip_index(const StringName &p_key) const {
	for (int i = 0; i < clip_count; i++) {
		if (clips[i].key == p_key) {
			return i;
		}
	}
	return -1;
}

Ref<AudioStreamPlayback> AudioStreamSymphonyMusic::instantiate_playback() {
	Ref<AudioStreamPlaybackSymphonyMusic> playback;
	playback.instantiate();

	// Resolve mix rate from device when available (policy D).
	float device = mix_rate;
	if (AudioServer::get_singleton()) {
		device = AudioServer::get_singleton()->get_mix_rate();
	}
	const float music_rate = symphony_select_music_rate(device);
	playback->_bind_music_stream(Ref<AudioStreamSymphonyMusic>(this), symphony_draw_voice_seed(), music_rate);

	if (Thread::is_main_thread()) {
		playback->_prepare_pools();
	}
	return playback;
}

double AudioStreamSymphonyMusic::get_length() const {
	return 0.0; // continuous
}

bool AudioStreamSymphonyMusic::is_monophonic() const {
	return true; // one music transport per stream instance
}

AudioStreamSymphonyMusic::~AudioStreamSymphonyMusic() {
	SymphonyMusicStereoCache *cache = SymphonyMusicStereoCache::get_singleton();
	if (!cache) {
		return;
	}
	for (int i = 0; i < clip_count; i++) {
		if (clips[i].registered) {
			cache->release(clips[i].key);
			clips[i].registered = false;
		}
	}
}
