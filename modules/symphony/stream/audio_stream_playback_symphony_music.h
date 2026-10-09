/**************************************************************************/
/*  audio_stream_playback_symphony_music.h                                */
/**************************************************************************/
/* Music playback: transport, note pools, stretch clips, VITAL voice.     */
/**************************************************************************/

#pragma once

#include "audio_stream_playback_symphony.h"
#include "audio_stream_symphony_music.h"
#include "../runtime/symphony_music_scheduler.h"
#include "../runtime/symphony_music_stretch.h"
#include "../runtime/symphony_note_pool.h"

#include "core/templates/local_vector.h"
#include "core/templates/vector.h"

#include <atomic>
#include <cstdint>

class AudioStreamPlaybackSymphonyMusic : public AudioStreamPlaybackSymphony {
	GDCLASS(AudioStreamPlaybackSymphonyMusic, AudioStreamPlaybackSymphony)

	friend class AudioStreamSymphonyMusic;

public:
	static constexpr int STATUS_CAP = 64;
	static constexpr int MAX_ACTIVE_CLIPS = 8;

	struct StatusEntry {
		enum class Kind : uint8_t {
			PhraseActivated = 0,
			NoteDropped = 1,
			ClipDropped = 2,
			TempoApplied = 3,
		};
		Kind kind = Kind::PhraseActivated;
		int32_t generation = 0;
		int32_t track_id = 0;
		int32_t note_id = 0;
		float payload = 0.0f;
	};

private:
	Ref<AudioStreamSymphonyMusic> music_stream;

	SymphonyMusicScheduler scheduler;
	SymphonyNotePool pools[AudioStreamSymphonyMusic::MAX_TRACKS];
	int32_t pool_count = 0;
	int32_t track_to_pool[256]; // track_id → pool index; -1 = missing
	float track_gains[AudioStreamSymphonyMusic::MAX_TRACKS];
	float track_release_frames[AudioStreamSymphonyMusic::MAX_TRACKS];

	SymphonyStretchPool stretch_pool;
	struct PreparedClip {
		int64_t tick = 0;
		int32_t note_id = 0;
		SymphonyStretchContext *ctx = nullptr;
		bool claimed = false;
	};
	LocalVector<PreparedClip> prepared_clips;
	SymphonyStretchContext *active_clips[MAX_ACTIVE_CLIPS] = {};
	int32_t active_clip_count = 0;

	// Synth delay to align with stretch common latency.
	Vector<AudioFrame> latency_delay;
	int32_t latency_samples = 0;
	int32_t latency_write = 0;

	StatusEntry status_ring[STATUS_CAP];
	std::atomic<uint32_t> status_w{ 0 };
	std::atomic<uint32_t> status_r{ 0 };

	bool prepared = false;
	uint64_t pool_seed = 0;

	void _push_status(StatusEntry::Kind p_kind, int32_t p_gen, int32_t p_track, int32_t p_note, float p_payload = 0.0f);
	void _bind_music_stream(const Ref<AudioStreamSymphonyMusic> &p_stream, uint64_t p_seed, float p_mix_rate);
	bool _prepare_pools();
	void _clear_runtime();
	void _apply_event(const SymphonyMusicEvent &p_ev, uint64_t p_frame, int32_t p_offset);
	void _render_block(AudioFrame *p_buffer, int p_frames, uint64_t p_block_start);
	static bool _parse_phrase(const Dictionary &p_phrase, SymphonyMusicPhrase &r_out, String &r_error);

protected:
	static void _bind_methods();
	virtual int _mix_internal(AudioFrame *p_buffer, int p_frames) override;
	virtual float get_stream_sampling_rate() override;

public:
	virtual void start(double p_from_pos = 0.0) override;
	virtual void stop() override;
	virtual void seek(double p_time) override;

	// Main-thread control surface (plan §2).
	Error submit_phrase(const Dictionary &p_phrase);
	Error replace_future(int64_t p_from_tick, int32_t p_generation);
	Error schedule_tempo(float p_bpm, int64_t p_start_tick, int32_t p_ramp_ticks = 0);
	Error schedule_track_gain(int32_t p_track_id, float p_gain, int64_t p_start_tick, int32_t p_fade_ticks = 0);
	Dictionary get_transport_snapshot() const;
	Array drain_status();

	~AudioStreamPlaybackSymphonyMusic() override;
};
