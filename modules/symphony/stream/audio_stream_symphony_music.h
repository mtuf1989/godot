/**************************************************************************/
/*  audio_stream_symphony_music.h                                         */
/**************************************************************************/
/* Procedural music stream: track instruments, clip registry, BPM.        */
/**************************************************************************/

#pragma once

#include "scene/resources/audio/audio_stream.h"
#include "../core/symphony_graph_description.h"
#include "../runtime/symphony_music_stretch.h"
#include "audio_stream_symphony.h"

#include "core/templates/hash_map.h"
#include "core/templates/vector.h"

class AudioStreamPlaybackSymphonyMusic;

class AudioStreamSymphonyMusic : public AudioStream {
	GDCLASS(AudioStreamSymphonyMusic, AudioStream)

	friend class AudioStreamPlaybackSymphonyMusic;

public:
	static constexpr int MAX_TRACKS = 16;
	static constexpr int MAX_CLIPS = 32;

	struct TrackConfig {
		int32_t track_id = 0;
		GraphDescription instrument;
		int32_t max_voices = 8;
		bool protected_track = false;
		float release_seconds = 0.2f;
		float gain = 1.0f;
		bool valid = false;
	};

	struct ClipConfig {
		StringName key;
		SymphonyMusicClipDesc desc; // pcm filled after register into stereo cache
		bool registered = false;
	};

private:
	float mix_rate = 48000.0f;
	float initial_bpm = 120.0f;
	TrackConfig tracks[MAX_TRACKS];
	int32_t track_count = 0;
	ClipConfig clips[MAX_CLIPS];
	int32_t clip_count = 0;

protected:
	static void _bind_methods();

public:
	void set_mix_rate(float p_mix_rate);
	[[nodiscard]] float get_mix_rate() const { return mix_rate; }

	void set_initial_bpm(float p_bpm);
	[[nodiscard]] float get_initial_bpm() const { return initial_bpm; }

	// Copy instrument graph from an AudioStreamSymphony resource.
	Error set_track_instrument(int32_t p_track_id, const Ref<AudioStreamSymphony> &p_instrument, int32_t p_max_voices = 8, bool p_protected = false);
	void set_track_release_seconds(int32_t p_track_id, float p_seconds);
	void set_track_gain(int32_t p_track_id, float p_gain);
	[[nodiscard]] int32_t get_track_count() const { return track_count; }

	// Register interleaved stereo float PCM into the music stereo cache.
	Error register_clip_pcm(const StringName &p_key, const PackedFloat32Array &p_interleaved_stereo, float p_sample_rate, float p_source_bpm = 120.0f, int32_t p_body_start = 0, int32_t p_body_end = 0);
	[[nodiscard]] int32_t find_clip_index(const StringName &p_key) const;
	[[nodiscard]] int32_t get_clip_count() const { return clip_count; }

	virtual Ref<AudioStreamPlayback> instantiate_playback() override;
	virtual double get_length() const override;
	virtual bool is_monophonic() const override;

	~AudioStreamSymphonyMusic() override;
};
