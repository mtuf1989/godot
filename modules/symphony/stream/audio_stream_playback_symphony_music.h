/**************************************************************************/
/*  audio_stream_playback_symphony_music.h                                */
/**************************************************************************/
/* Music playback: transport, note pools, stretch clips, VITAL voice.     */
/**************************************************************************/

#pragma once

#include "audio_stream_playback_symphony.h"
#include "audio_stream_symphony_music.h"
#include "../core/symphony_spsc_ring.h"
#include "../runtime/symphony_music_scheduler.h"
#include "../runtime/symphony_music_stretch.h"
#include "../runtime/symphony_note_pool.h"

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
			PhraseLate = 4,
			NoteForcedOff = 5,
			StatusDropped = 6,
			ClipReturned = 7,
		};
		Kind kind = Kind::PhraseActivated;
		int32_t generation = 0;
		int32_t track_id = 0;
		int32_t note_id = 0;
		float payload = 0.0f;
	};

	struct GainRamp {
		bool armed = false;
		float from = 1.0f;
		float to = 1.0f;
		uint64_t start_frame = 0;
		uint64_t end_frame = 0;
	};

	enum class PhraseSlotState : uint8_t {
		Free = 0,
		Filled = 1,
		Queued = 2,
		Active = 3,
		Retired = 4,
	};

	enum class MusicCmdKind : uint8_t {
		SubmitPhrase = 0,
		ReplaceFuture = 1,
		TempoStep = 2,
		TempoRamp = 3,
		TrackGain = 4,
		ClipPrepared = 5,
		Reset = 6,
	};

	struct MusicCmd {
		MusicCmdKind kind = MusicCmdKind::SubmitPhrase;
		int32_t slot = -1;
		int32_t generation = 0;
		int32_t track_id = 0;
		int32_t note_id = 0;
		int32_t duration_ticks = 0;
		int64_t tick = 0;
		float value = 0.0f;
		SymphonyStretchContext *ctx = nullptr;
	};

	struct PhraseSlot {
		SymphonyMusicPhrase phrase;
		std::atomic<uint8_t> state{ (uint8_t)PhraseSlotState::Free };
	};

	struct PreparedClip {
		int64_t tick = 0;
		int32_t note_id = 0;
		int32_t generation = 0;
		SymphonyStretchContext *ctx = nullptr;
		bool claimed = false;
	};

private:
	static constexpr int PHRASE_SLOTS = 4;
	static constexpr int CMD_CAP = 64;
	static constexpr int MAX_PREPARED_CLIPS = 8;

	Ref<AudioStreamSymphonyMusic> music_stream;

	SymphonyMusicScheduler scheduler;
	SymphonyNotePool pools[AudioStreamSymphonyMusic::MAX_TRACKS];
	int32_t pool_count = 0;
	int32_t track_to_pool[256]; // track_id → pool index; -1 = missing
	float track_gains[AudioStreamSymphonyMusic::MAX_TRACKS];
	float track_release_frames[AudioStreamSymphonyMusic::MAX_TRACKS];
	GainRamp gain_ramps[AudioStreamSymphonyMusic::MAX_TRACKS];
	AudioFrame synth_scratch[64];
	AudioFrame track_scratch[64];

	SymphonyStretchPool stretch_pool;
	PreparedClip prepared_clips[MAX_PREPARED_CLIPS];
	int32_t prepared_clip_count = 0;
	SymphonyStretchContext *active_clips[MAX_ACTIVE_CLIPS] = {};
	int32_t active_clip_count = 0;

	Vector<AudioFrame> latency_delay;
	int32_t latency_samples = 0;
	int32_t latency_write = 0;
	bool latency_active = false;

	PhraseSlot phrase_slots[PHRASE_SLOTS];
	SymphonySpscRing<MusicCmd, CMD_CAP> cmd_ring;
	SymphonySpscRing<StatusEntry, STATUS_CAP> audio_status;
	StatusEntry main_status[STATUS_CAP];
	int main_status_count = 0;
	std::atomic<uint32_t> status_overflow{ 0 };
	std::atomic<bool> restart_pending{ false };

	bool prepared = false;
	uint64_t pool_seed = 0;

	void _push_status_main(StatusEntry::Kind p_kind, int32_t p_gen, int32_t p_track, int32_t p_note, float p_payload = 0.0f);
	void _push_status_audio(StatusEntry::Kind p_kind, int32_t p_gen, int32_t p_track, int32_t p_note, float p_payload = 0.0f);
	void _bind_music_stream(const Ref<AudioStreamSymphonyMusic> &p_stream, uint64_t p_seed, float p_mix_rate);
	bool _prepare_pools();
	void _clear_runtime();
	void _apply_event(const SymphonyMusicEvent &p_ev, uint64_t p_frame, int32_t p_offset);
	void _render_block(AudioFrame *p_buffer, int p_frames, uint64_t p_block_start);
	void _arm_gain(int p_pool, float p_target, uint64_t p_start_frame, uint64_t p_end_frame);
	[[nodiscard]] float _gain_at(int p_pool, uint64_t p_frame) const;
	void _drain_scheduler_notices();
	void _reclaim_phrase_slots();
	int _acquire_phrase_slot();
	[[nodiscard]] bool _try_push_cmd(const MusicCmd &p_cmd);
	void _drain_commands();
	void _audio_restart();
	void _return_stale_clips();
	void _return_clip_slot(int p_index);
	static bool _parse_phrase(const Dictionary &p_phrase, SymphonyMusicPhrase &r_out, String &r_error);

protected:
	static void _bind_methods();
	virtual int _mix_internal(AudioFrame *p_buffer, int p_frames) override;
	virtual float get_stream_sampling_rate() override;

public:
	AudioStreamPlaybackSymphonyMusic();
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
	PackedVector2Array render_offline(int p_frames);

	int _test_active_voice_count() const;
	int _test_transport_segment_count() const;
	int _test_free_stretch_contexts() const;
	int _test_latency_samples() const { return latency_samples; }

	~AudioStreamPlaybackSymphonyMusic() override;
};
