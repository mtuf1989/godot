/**************************************************************************/
/*  audio_stream_playback_symphony_music.cpp                              */
/**************************************************************************/

#include "audio_stream_playback_symphony_music.h"
#include "../core/symphony_rate_policy.h"
#include "../core/symphony_voice_manager.h"
#include "../runtime/sound_event.h"

#include "core/object/class_db.h"
#include "core/os/thread.h"
#include "core/string/ustring.h"

AudioStreamPlaybackSymphonyMusic::AudioStreamPlaybackSymphonyMusic() {
	for (int i = 0; i < 256; i++) {
		track_to_pool[i] = -1;
	}
	for (int i = 0; i < AudioStreamSymphonyMusic::MAX_TRACKS; i++) {
		track_gains[i] = 1.0f;
		track_release_frames[i] = 1.0f;
		gain_ramps[i] = GainRamp{};
	}
}

void AudioStreamPlaybackSymphonyMusic::_bind_methods() {
	ClassDB::bind_method(D_METHOD("submit_phrase", "phrase"), &AudioStreamPlaybackSymphonyMusic::submit_phrase);
	ClassDB::bind_method(D_METHOD("replace_future", "from_tick", "generation"), &AudioStreamPlaybackSymphonyMusic::replace_future);
	ClassDB::bind_method(D_METHOD("schedule_tempo", "bpm", "start_tick", "ramp_ticks"), &AudioStreamPlaybackSymphonyMusic::schedule_tempo, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("schedule_track_gain", "track_id", "gain", "start_tick", "fade_ticks"), &AudioStreamPlaybackSymphonyMusic::schedule_track_gain, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("get_transport_snapshot"), &AudioStreamPlaybackSymphonyMusic::get_transport_snapshot);
	ClassDB::bind_method(D_METHOD("drain_status"), &AudioStreamPlaybackSymphonyMusic::drain_status);
}

void AudioStreamPlaybackSymphonyMusic::_bind_music_stream(const Ref<AudioStreamSymphonyMusic> &p_stream, uint64_t p_seed, float p_mix_rate) {
	music_stream = p_stream;
	pool_seed = p_seed;
	voice_seed = p_seed;
	mix_rate_cached.store(p_mix_rate, std::memory_order_relaxed);
	output_rate_cached.store(p_mix_rate, std::memory_order_relaxed);
}

void AudioStreamPlaybackSymphonyMusic::_push_status_main(StatusEntry::Kind p_kind, int32_t p_gen, int32_t p_track, int32_t p_note, float p_payload) {
	if (main_status_count >= STATUS_CAP) {
		status_overflow.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	StatusEntry &e = main_status[main_status_count++];
	e.kind = p_kind;
	e.generation = p_gen;
	e.track_id = p_track;
	e.note_id = p_note;
	e.payload = p_payload;
}

void AudioStreamPlaybackSymphonyMusic::_push_status_audio(StatusEntry::Kind p_kind, int32_t p_gen, int32_t p_track, int32_t p_note, float p_payload) {
	StatusEntry e;
	e.kind = p_kind;
	e.generation = p_gen;
	e.track_id = p_track;
	e.note_id = p_note;
	e.payload = p_payload;
	if (!audio_status.try_push(e)) {
		status_overflow.fetch_add(1, std::memory_order_relaxed);
	}
}

void AudioStreamPlaybackSymphonyMusic::_clear_runtime() {
	for (int i = 0; i < pool_count; i++) {
		pools[i].clear();
	}
	pool_count = 0;
	for (int i = 0; i < 256; i++) {
		track_to_pool[i] = -1;
	}
	prepared_clip_count = 0;
	for (int i = 0; i < MAX_PREPARED_CLIPS; i++) {
		prepared_clips[i] = PreparedClip{};
	}
	active_clip_count = 0;
	for (int i = 0; i < MAX_ACTIVE_CLIPS; i++) {
		active_clips[i] = nullptr;
	}
	stretch_pool.reclaim_returned();
	for (int i = 0; i < SymphonyStretchPool::MAX_CONTEXTS; i++) {
		if (stretch_pool.contexts[i].get_ownership() != SymphonyStretchOwnership::Free) {
			stretch_pool.contexts[i].set_ownership(SymphonyStretchOwnership::Returning);
		}
	}
	stretch_pool.reclaim_returned();
	_reclaim_phrase_slots();
	for (int i = 0; i < PHRASE_SLOTS; i++) {
		phrase_slots[i].phrase.clear();
		phrase_slots[i].state.store((uint8_t)PhraseSlotState::Free, std::memory_order_release);
	}
	cmd_ring.clear();
	audio_status.clear();
	main_status_count = 0;
	latency_delay.clear();
	latency_samples = 0;
	latency_write = 0;
	latency_active = false;
	prepared = false;
}

bool AudioStreamPlaybackSymphonyMusic::_prepare_pools() {
	_clear_runtime();
	if (music_stream.is_null()) {
		return false;
	}
	const float rate = mix_rate_cached.load(std::memory_order_relaxed);
	const float bpm = music_stream->get_initial_bpm();
	scheduler.reset(rate > 0.0f ? rate : symphony_offline_rate(), bpm);

	pool_count = 0;
	for (int i = 0; i < music_stream->track_count; i++) {
		const AudioStreamSymphonyMusic::TrackConfig &tc = music_stream->tracks[i];
		if (!tc.valid) {
			continue;
		}
		ERR_FAIL_COND_V(pool_count >= AudioStreamSymphonyMusic::MAX_TRACKS, false);
		SymphonyNotePool &pool = pools[pool_count];
		pool.track_id = tc.track_id;
		pool.protected_track = tc.protected_track;
		if (!pool.prepare(tc.instrument, rate, tc.max_voices, pool_seed + (uint64_t)pool_count * 0x9E3779B97F4A7C15ull)) {
			_push_status_main(StatusEntry::Kind::NoteDropped, 0, tc.track_id, -1, 0.0f);
			return false;
		}
		if (tc.track_id >= 0 && tc.track_id < 256) {
			track_to_pool[tc.track_id] = pool_count;
		}
		track_gains[pool_count] = tc.gain;
		track_release_frames[pool_count] = MAX(1.0f, tc.release_seconds * rate);
		gain_ramps[pool_count] = GainRamp{};
		pool_count++;
	}

	{
		signalsmith::stretch::SignalsmithStretch<float> probe;
		probe.presetDefault(2, rate > 0.0f ? rate : symphony_offline_rate(), true);
		// Test-driven: outputLatency aligns a click clip with a click note within 48 samples.
		latency_samples = probe.outputLatency();
		latency_delay.resize(MAX(latency_samples, 0) + 64 * 2);
		for (int i = 0; i < latency_delay.size(); i++) {
			latency_delay.write[i] = AudioFrame(0, 0);
		}
		latency_write = 0;
		latency_active = false;
	}

	prepared = true;
	return true;
}

bool AudioStreamPlaybackSymphonyMusic::_parse_phrase(const Dictionary &p_phrase, SymphonyMusicPhrase &r_out, String &r_error) {
	r_out.clear();
	r_out.start_tick = (int64_t)p_phrase.get("start_tick", 0);
	r_out.generation = (int32_t)p_phrase.get("generation", 0);
	const Variant ev_var = p_phrase.get("events", Array());
	if (ev_var.get_type() != Variant::ARRAY) {
		r_error = "phrase.events must be an Array";
		return false;
	}
	const Array events = ev_var;
	for (int i = 0; i < events.size(); i++) {
		if (events[i].get_type() != Variant::DICTIONARY) {
			r_error = "phrase event must be a Dictionary";
			return false;
		}
		const Dictionary d = events[i];
		SymphonyMusicEvent ev;
		ev.tick = (int64_t)d.get("tick", 0);
		ev.track_id = (int32_t)d.get("track", d.get("track_id", 0));
		ev.note_id = (int32_t)d.get("note_id", i + 1);
		ev.midi = (int32_t)d.get("midi", 60);
		ev.velocity = (float)d.get("velocity", 1.0);
		ev.duration_ticks = (int32_t)d.get("duration_ticks", 0);
		ev.payload = (float)d.get("payload", 0.0);

		const String kind = String(d.get("kind", "note_on")).to_lower();
		if (kind == "note_on" || kind == "noteon") {
			ev.kind = SymphonyMusicEventKind::NoteOn;
			if (!r_out.push(ev)) {
				r_error = "phrase event capacity exceeded";
				return false;
			}
			if (ev.duration_ticks > 0) {
				SymphonyMusicEvent off = ev;
				off.kind = SymphonyMusicEventKind::NoteOff;
				off.tick = ev.tick + ev.duration_ticks;
				off.duration_ticks = 0;
				if (!r_out.push(off)) {
					r_error = "phrase event capacity exceeded";
					return false;
				}
			}
		} else if (kind == "note_off" || kind == "noteoff") {
			ev.kind = SymphonyMusicEventKind::NoteOff;
			if (!r_out.push(ev)) {
				r_error = "phrase event capacity exceeded";
				return false;
			}
		} else if (kind == "tempo" || kind == "tempo_step") {
			ev.kind = SymphonyMusicEventKind::TempoStep;
			ev.payload = (float)d.get("bpm", d.get("payload", 120.0));
			ev.duration_ticks = (int32_t)d.get("ramp_ticks", 0);
			if (!r_out.push(ev)) {
				r_error = "phrase event capacity exceeded";
				return false;
			}
		} else if (kind == "track_gain" || kind == "gain") {
			ev.kind = SymphonyMusicEventKind::TrackGain;
			ev.payload = (float)d.get("gain", d.get("payload", 1.0));
			ev.duration_ticks = (int32_t)d.get("fade_ticks", 0);
			if (!r_out.push(ev)) {
				r_error = "phrase event capacity exceeded";
				return false;
			}
		} else if (kind == "clip_on" || kind == "clip") {
			ev.kind = SymphonyMusicEventKind::ClipOn;
			ev.payload = (float)d.get("clip_index", d.get("payload", -1.0));
			ev.midi = (int32_t)d.get("transpose", d.get("midi", 0));
			ev.velocity = (float)d.get("tempo_ratio", d.get("velocity", 1.0));
			if (!r_out.push(ev)) {
				r_error = "phrase event capacity exceeded";
				return false;
			}
		} else {
			r_error = vformat("unknown event kind '%s'", kind);
			return false;
		}
	}
	return true;
}

Error AudioStreamPlaybackSymphonyMusic::submit_phrase(const Dictionary &p_phrase) {
	ERR_FAIL_COND_V(!Thread::is_main_thread(), ERR_UNAVAILABLE);
	stretch_pool.reclaim_returned();
	_reclaim_phrase_slots();

	SymphonyMusicPhrase phrase;
	String err;
	if (!_parse_phrase(p_phrase, phrase, err)) {
		ERR_PRINT(vformat("AudioStreamPlaybackSymphonyMusic::submit_phrase: %s", err));
		return ERR_INVALID_PARAMETER;
	}

	const Array events = p_phrase.get("events", Array());
	SymphonyStretchContext *clip_ctxs[MAX_PREPARED_CLIPS];
	int64_t clip_ticks[MAX_PREPARED_CLIPS];
	int32_t clip_notes[MAX_PREPARED_CLIPS];
	int clip_n = 0;
	int clip_event_i = 0;
	for (int i = 0; i < phrase.event_count; i++) {
		if (phrase.events[i].kind != SymphonyMusicEventKind::ClipOn) {
			continue;
		}
		Dictionary d;
		int seen = 0;
		for (int e = 0; e < events.size(); e++) {
			const Dictionary ed = events[e];
			const String kind = String(ed.get("kind", "")).to_lower();
			if (kind != "clip_on" && kind != "clip") {
				continue;
			}
			if (seen == clip_event_i) {
				d = ed;
				break;
			}
			seen++;
		}
		clip_event_i++;

		int32_t clip_index = (int32_t)phrase.events[i].payload;
		if (d.has("clip_key") && music_stream.is_valid()) {
			clip_index = music_stream->find_clip_index(d.get("clip_key", StringName()));
		}
		if (clip_index < 0 || music_stream.is_null() || clip_index >= music_stream->clip_count || !music_stream->clips[clip_index].registered || !music_stream->clips[clip_index].usable) {
			_push_status_main(StatusEntry::Kind::ClipDropped, phrase.generation, phrase.events[i].track_id, phrase.events[i].note_id, 0.0f);
			continue;
		}
		phrase.events[i].payload = (float)clip_index;
		const AudioStreamSymphonyMusic::ClipConfig &cc = music_stream->clips[clip_index];
		const float tempo_ratio = phrase.events[i].velocity > 0.0f ? phrase.events[i].velocity : 1.0f;
		const float transpose = (float)phrase.events[i].midi;
		SymphonyStretchContext *ctx = stretch_pool.acquire_prepared(cc.desc, tempo_ratio, transpose);
		if (!ctx) {
			_push_status_main(StatusEntry::Kind::ClipDropped, phrase.generation, phrase.events[i].track_id, phrase.events[i].note_id, 0.0f);
			continue;
		}
		if (clip_n >= MAX_PREPARED_CLIPS) {
			ctx->reclaim();
			_push_status_main(StatusEntry::Kind::ClipDropped, phrase.generation, phrase.events[i].track_id, phrase.events[i].note_id, 0.0f);
			continue;
		}
		clip_ctxs[clip_n] = ctx;
		clip_ticks[clip_n] = phrase.events[i].tick;
		clip_notes[clip_n] = phrase.events[i].note_id;
		clip_n++;
	}

	const int needed = 1 + clip_n;
	if (cmd_ring.remaining() < needed) {
		for (int i = 0; i < clip_n; i++) {
			clip_ctxs[i]->reclaim();
		}
		return ERR_BUSY;
	}

	const int slot = _acquire_phrase_slot();
	if (slot < 0) {
		for (int i = 0; i < clip_n; i++) {
			clip_ctxs[i]->reclaim();
		}
		return ERR_BUSY;
	}
	phrase_slots[slot].phrase = phrase;
	phrase_slots[slot].state.store((uint8_t)PhraseSlotState::Filled, std::memory_order_release);

	for (int i = 0; i < clip_n; i++) {
		MusicCmd clip_cmd;
		clip_cmd.kind = MusicCmdKind::ClipPrepared;
		clip_cmd.ctx = clip_ctxs[i];
		clip_cmd.tick = clip_ticks[i];
		clip_cmd.note_id = clip_notes[i];
		clip_cmd.generation = phrase.generation;
		if (!_try_push_cmd(clip_cmd)) {
			for (int j = i; j < clip_n; j++) {
				clip_ctxs[j]->reclaim();
			}
			phrase_slots[slot].phrase.clear();
			phrase_slots[slot].state.store((uint8_t)PhraseSlotState::Free, std::memory_order_release);
			return ERR_BUSY;
		}
	}

	MusicCmd submit;
	submit.kind = MusicCmdKind::SubmitPhrase;
	submit.slot = slot;
	submit.generation = phrase.generation;
	if (!_try_push_cmd(submit)) {
		phrase_slots[slot].phrase.clear();
		phrase_slots[slot].state.store((uint8_t)PhraseSlotState::Free, std::memory_order_release);
		return ERR_BUSY;
	}
	phrase_slots[slot].state.store((uint8_t)PhraseSlotState::Queued, std::memory_order_release);
	return OK;
}

Error AudioStreamPlaybackSymphonyMusic::replace_future(int64_t p_from_tick, int32_t p_generation) {
	ERR_FAIL_COND_V(!Thread::is_main_thread(), ERR_UNAVAILABLE);
	MusicCmd cmd;
	cmd.kind = MusicCmdKind::ReplaceFuture;
	cmd.tick = p_from_tick;
	cmd.generation = p_generation;
	if (!_try_push_cmd(cmd)) {
		return ERR_BUSY;
	}
	return OK;
}

Error AudioStreamPlaybackSymphonyMusic::schedule_tempo(float p_bpm, int64_t p_start_tick, int32_t p_ramp_ticks) {
	ERR_FAIL_COND_V(!Thread::is_main_thread(), ERR_UNAVAILABLE);
	MusicCmd cmd;
	cmd.kind = p_ramp_ticks > 0 ? MusicCmdKind::TempoRamp : MusicCmdKind::TempoStep;
	cmd.value = p_bpm;
	cmd.tick = p_start_tick;
	cmd.duration_ticks = p_ramp_ticks;
	if (!_try_push_cmd(cmd)) {
		return ERR_BUSY;
	}
	_push_status_main(StatusEntry::Kind::TempoApplied, 0, 0, 0, p_bpm);
	return OK;
}

Error AudioStreamPlaybackSymphonyMusic::schedule_track_gain(int32_t p_track_id, float p_gain, int64_t p_start_tick, int32_t p_fade_ticks) {
	ERR_FAIL_COND_V(!Thread::is_main_thread(), ERR_UNAVAILABLE);
	if (p_track_id < 0 || p_track_id >= 256) {
		return ERR_INVALID_PARAMETER;
	}
	const int pi = track_to_pool[p_track_id];
	if (pi < 0) {
		return ERR_DOES_NOT_EXIST;
	}
	MusicCmd cmd;
	cmd.kind = MusicCmdKind::TrackGain;
	cmd.track_id = p_track_id;
	cmd.value = p_gain;
	cmd.tick = p_start_tick;
	cmd.duration_ticks = p_fade_ticks;
	if (!_try_push_cmd(cmd)) {
		return ERR_BUSY;
	}
	(void)pi;
	return OK;
}

void AudioStreamPlaybackSymphonyMusic::_arm_gain(int p_pool, float p_target, uint64_t p_start_frame, uint64_t p_end_frame) {
	if (p_pool < 0 || p_pool >= pool_count) {
		return;
	}
	GainRamp &ramp = gain_ramps[p_pool];
	ramp.armed = true;
	ramp.from = track_gains[p_pool];
	ramp.to = CLAMP(p_target, 0.0f, 4.0f);
	ramp.start_frame = p_start_frame;
	ramp.end_frame = MAX(p_end_frame, p_start_frame);
}

float AudioStreamPlaybackSymphonyMusic::_gain_at(int p_pool, uint64_t p_frame) const {
	if (p_pool < 0 || p_pool >= pool_count) {
		return 1.0f;
	}
	const GainRamp &ramp = gain_ramps[p_pool];
	if (!ramp.armed) {
		return track_gains[p_pool];
	}
	if (p_frame <= ramp.start_frame) {
		return ramp.from;
	}
	if (p_frame >= ramp.end_frame || ramp.end_frame <= ramp.start_frame) {
		return ramp.to;
	}
	const float t = (float)(p_frame - ramp.start_frame) / (float)(ramp.end_frame - ramp.start_frame);
	return ramp.from + (ramp.to - ramp.from) * CLAMP(t, 0.0f, 1.0f);
}

void AudioStreamPlaybackSymphonyMusic::_drain_scheduler_notices() {
	SymphonyMusicScheduler::Notice notices[SymphonyMusicScheduler::NOTICE_CAP];
	const int n = scheduler.drain_notices(notices, SymphonyMusicScheduler::NOTICE_CAP);
	for (int i = 0; i < n; i++) {
		StatusEntry::Kind kind = StatusEntry::Kind::NoteForcedOff;
		if (notices[i].kind == SymphonyMusicScheduler::NoticeKind::PhraseLate) {
			kind = StatusEntry::Kind::PhraseLate;
		} else if (notices[i].kind == SymphonyMusicScheduler::NoticeKind::PhraseActivated) {
			kind = StatusEntry::Kind::PhraseActivated;
		}
		_push_status_audio(kind, notices[i].generation, notices[i].track_id, notices[i].note_id, notices[i].payload);
	}
	SymphonyNotePool::ForcedOff forced[SymphonyNotePool::MAX_VOICES];
	for (int p = 0; p < pool_count; p++) {
		const int fn = pools[p].drain_forced(forced, SymphonyNotePool::MAX_VOICES);
		for (int i = 0; i < fn; i++) {
			_push_status_audio(StatusEntry::Kind::NoteForcedOff, forced[i].generation, pools[p].track_id, forced[i].note_id, 1.0f);
		}
	}
}

Dictionary AudioStreamPlaybackSymphonyMusic::get_transport_snapshot() const {
	const SymphonyMusicTransport::Snapshot snap = scheduler.get_snapshot();
	Dictionary d;
	d["tick"] = snap.tick;
	d["frame"] = (int64_t)snap.frame;
	d["bpm"] = snap.bpm;
	d["beats_per_bar"] = snap.beats_per_bar;
	d["beat_unit"] = snap.beat_unit;
	d["bar_index"] = snap.bar_index;
	d["beat_in_bar"] = snap.beat_in_bar;
	d["generation"] = snap.generation;
	d["sample_rate"] = snap.sample_rate;
	d["playing"] = is_playing();
	return d;
}

Array AudioStreamPlaybackSymphonyMusic::drain_status() {
	Array out;
	auto emit = [&](const StatusEntry &e) {
		Dictionary d;
		switch (e.kind) {
			case StatusEntry::Kind::PhraseActivated:
				d["kind"] = "phrase_activated";
				break;
			case StatusEntry::Kind::NoteDropped:
				d["kind"] = "note_dropped";
				break;
			case StatusEntry::Kind::ClipDropped:
				d["kind"] = "clip_dropped";
				break;
			case StatusEntry::Kind::TempoApplied:
				d["kind"] = "tempo_applied";
				break;
			case StatusEntry::Kind::PhraseLate:
				d["kind"] = "phrase_late";
				break;
			case StatusEntry::Kind::NoteForcedOff:
				d["kind"] = "note_forced_off";
				break;
			case StatusEntry::Kind::StatusDropped:
				d["kind"] = "status_dropped";
				break;
			case StatusEntry::Kind::ClipReturned:
				d["kind"] = "clip_returned";
				break;
		}
		d["generation"] = e.generation;
		d["track_id"] = e.track_id;
		d["note_id"] = e.note_id;
		d["payload"] = e.payload;
		out.push_back(d);
	};
	StatusEntry audio_e;
	while (audio_status.try_pop(audio_e)) {
		emit(audio_e);
	}
	for (int i = 0; i < main_status_count; i++) {
		emit(main_status[i]);
	}
	main_status_count = 0;
	const uint32_t dropped = status_overflow.exchange(0, std::memory_order_acq_rel);
	if (dropped > 0) {
		StatusEntry ov;
		ov.kind = StatusEntry::Kind::StatusDropped;
		ov.payload = (float)dropped;
		emit(ov);
	}
	stretch_pool.reclaim_returned();
	_reclaim_phrase_slots();
	return out;
}

void AudioStreamPlaybackSymphonyMusic::_apply_event(const SymphonyMusicEvent &p_ev, uint64_t p_frame, int32_t p_offset) {
	switch (p_ev.kind) {
		case SymphonyMusicEventKind::NoteOn: {
			if (p_ev.track_id < 0 || p_ev.track_id >= 256) {
				return;
			}
			const int pi = track_to_pool[p_ev.track_id];
			if (pi < 0) {
				return;
			}
			const int32_t vi = pools[pi].note_on(p_ev.note_id, p_ev.midi, p_ev.velocity, p_frame, p_offset, p_ev.generation);
			if (vi < 0) {
				_push_status_audio(StatusEntry::Kind::NoteDropped, scheduler.active_generation, p_ev.track_id, p_ev.note_id, 0.0f);
			}
			break;
		}
		case SymphonyMusicEventKind::NoteOff: {
			if (p_ev.track_id < 0 || p_ev.track_id >= 256) {
				return;
			}
			const int pi = track_to_pool[p_ev.track_id];
			if (pi < 0) {
				return;
			}
			pools[pi].note_off(p_ev.note_id, p_frame, p_offset, (uint64_t)track_release_frames[pi], p_ev.generation);
			break;
		}
		case SymphonyMusicEventKind::TempoStep: {
			if (p_ev.duration_ticks > 0) {
				(void)scheduler.transport.schedule_tempo_ramp(p_ev.payload, p_ev.tick, p_ev.duration_ticks);
			} else {
				(void)scheduler.transport.schedule_tempo_step(p_ev.payload, p_ev.tick);
			}
			break;
		}
		case SymphonyMusicEventKind::TrackGain: {
			if (p_ev.track_id < 0 || p_ev.track_id >= 256) {
				return;
			}
			const int pi = track_to_pool[p_ev.track_id];
			if (pi >= 0) {
				uint64_t end_frame = p_frame;
				if (p_ev.duration_ticks > 0) {
					end_frame = scheduler.transport.tick_to_frame(p_ev.tick + p_ev.duration_ticks);
				}
				_arm_gain(pi, p_ev.payload, p_frame, end_frame);
			}
			break;
		}
		case SymphonyMusicEventKind::ClipOn: {
			SymphonyStretchContext *ctx = nullptr;
			for (int i = 0; i < prepared_clip_count; i++) {
				PreparedClip &pc = prepared_clips[i];
				if (!pc.claimed && pc.note_id == p_ev.note_id && pc.tick == p_ev.tick && pc.ctx) {
					ctx = pc.ctx;
					pc.claimed = true;
					break;
				}
			}
			if (!ctx || !ctx->activate()) {
				_push_status_audio(StatusEntry::Kind::ClipDropped, scheduler.active_generation, p_ev.track_id, p_ev.note_id, 0.0f);
				return;
			}
			if (active_clip_count < MAX_ACTIVE_CLIPS) {
				active_clips[active_clip_count++] = ctx;
			} else {
				ctx->request_return();
				_push_status_audio(StatusEntry::Kind::ClipDropped, scheduler.active_generation, p_ev.track_id, p_ev.note_id, 0.0f);
			}
			break;
		}
	}
}

void AudioStreamPlaybackSymphonyMusic::_render_block(AudioFrame *p_buffer, int p_frames, uint64_t p_block_start) {
	for (int i = 0; i < p_frames; i++) {
		p_buffer[i] = AudioFrame(0, 0);
	}

	for (int c = 0; c < active_clip_count;) {
		SymphonyStretchContext *ctx = active_clips[c];
		if (!ctx || ctx->get_ownership() != SymphonyStretchOwnership::Active || ctx->finished) {
			active_clips[c] = active_clips[active_clip_count - 1];
			active_clips[--active_clip_count] = nullptr;
			continue;
		}
		ctx->process_add(p_buffer, p_frames);
		if (ctx->finished) {
			active_clips[c] = active_clips[active_clip_count - 1];
			active_clips[--active_clip_count] = nullptr;
			continue;
		}
		c++;
	}

	const int frames = MIN(p_frames, 64);
	for (int i = 0; i < frames; i++) {
		synth_scratch[i] = AudioFrame(0, 0);
	}
	for (int p = 0; p < pool_count; p++) {
		for (int i = 0; i < frames; i++) {
			track_scratch[i] = AudioFrame(0, 0);
		}
		pools[p].render(track_scratch, frames, p_block_start);
		for (int i = 0; i < frames; i++) {
			const float g = _gain_at(p, p_block_start + (uint64_t)i);
			synth_scratch[i].left += track_scratch[i].left * g;
			synth_scratch[i].right += track_scratch[i].right * g;
		}
		if (gain_ramps[p].armed && p_block_start + (uint64_t)frames >= gain_ramps[p].end_frame) {
			track_gains[p] = gain_ramps[p].to;
			gain_ramps[p].armed = false;
		}
	}

	if (latency_active && latency_samples > 0 && latency_delay.size() > 0) {
		const int cap = latency_delay.size();
		for (int i = 0; i < frames; i++) {
			latency_delay.write[(latency_write + i) % cap] = synth_scratch[i];
		}
		for (int i = 0; i < frames; i++) {
			const int64_t abs = (int64_t)p_block_start + i;
			AudioFrame s(0, 0);
			if (abs >= latency_samples) {
				const int ri = (latency_write + i - latency_samples + cap * 8) % cap;
				s = latency_delay[ri];
			}
			p_buffer[i].left += s.left;
			p_buffer[i].right += s.right;
		}
		latency_write = (latency_write + frames) % cap;
	} else {
		for (int i = 0; i < frames; i++) {
			p_buffer[i].left += synth_scratch[i].left;
			p_buffer[i].right += synth_scratch[i].right;
		}
	}
}

void AudioStreamPlaybackSymphonyMusic::start(double p_from_pos) {
	if (p_from_pos != 0.0) {
		_report_unsupported_seek();
	}
	const bool on_main = Thread::is_main_thread();
	if (on_main) {
		float rate = mix_rate_cached.load(std::memory_order_relaxed);
		if (rate <= 0.0f) {
			rate = symphony_offline_rate();
			mix_rate_cached.store(rate, std::memory_order_relaxed);
		}
		output_rate_cached.store(rate, std::memory_order_relaxed);
		if (!prepared) {
			_prepare_pools();
		}
		set_voice_tier((int)SoundEvent::TIER_VITAL);
		set_voice_importance(SoundEvent::TIER_BAND_VITAL);
		_register_with_manager();
	}
	restart_pending.store(true, std::memory_order_release);
	resample_reset_pending.store(true, std::memory_order_release);
	auto_fire_pending.store(false, std::memory_order_release);
	active.store(true, std::memory_order_release);
}

void AudioStreamPlaybackSymphonyMusic::stop() {
	active.store(false, std::memory_order_release);
}

void AudioStreamPlaybackSymphonyMusic::seek(double p_time) {
	if (p_time != 0.0) {
		_report_unsupported_seek();
	}
}

float AudioStreamPlaybackSymphonyMusic::get_stream_sampling_rate() {
	const float rate = mix_rate_cached.load(std::memory_order_relaxed);
	return rate > 0.0f ? rate : symphony_offline_rate();
}

int AudioStreamPlaybackSymphonyMusic::_mix_internal(AudioFrame *p_buffer, int p_frames) {
	if (!active.load(std::memory_order_acquire) || p_frames <= 0) {
		return 0;
	}
	if (restart_pending.exchange(false, std::memory_order_acq_rel)) {
		_audio_restart();
	}
	_drain_commands();
	_return_stale_clips();

	if (!prepared) {
		for (int i = 0; i < p_frames; i++) {
			p_buffer[i] = AudioFrame(0, 0);
		}
		_note_rendered_frames(p_frames);
		scheduler.frames_rendered += (uint64_t)p_frames;
		return p_frames;
	}

	int frames_processed = 0;
	int guard = 0;
	while (frames_processed < p_frames && guard < p_frames * 4 + 8) {
		guard++;
		const uint64_t cursor = scheduler.frames_rendered;
		const uint64_t ev = scheduler.peek_event_frame();
		if (ev == cursor) {
			scheduler.take_events_at(cursor, [&](const SymphonyMusicEvent &event, uint64_t frame) {
				_apply_event(event, frame, 0);
			});
			_drain_scheduler_notices();
			continue;
		}
		uint64_t span_end = cursor + (uint64_t)MIN(SYMPHONY_MICRO_BLOCK_SIZE, p_frames - frames_processed);
		if (ev > cursor && ev < span_end) {
			span_end = ev;
		}
		const int span = (int)(span_end - cursor);
		if (span <= 0) {
			break;
		}
		_render_block(p_buffer + frames_processed, span, cursor);
		scheduler.commit_frames(span);
		frames_processed += span;
	}
	_drain_scheduler_notices();
	scheduler.publish_snapshot();

	_note_rendered_frames(frames_processed);
	return frames_processed;
}

int AudioStreamPlaybackSymphonyMusic::_test_active_voice_count() const {
	int n = 0;
	for (int i = 0; i < pool_count; i++) {
		n += pools[i].active_count();
	}
	return n;
}

int AudioStreamPlaybackSymphonyMusic::_test_transport_segment_count() const {
	return scheduler.transport.segment_count();
}

int AudioStreamPlaybackSymphonyMusic::_test_free_stretch_contexts() const {
	int n = 0;
	for (int i = 0; i < SymphonyStretchPool::MAX_CONTEXTS; i++) {
		if (stretch_pool.contexts[i].is_free()) {
			n++;
		}
	}
	return n;
}

void AudioStreamPlaybackSymphonyMusic::_reclaim_phrase_slots() {
	for (int i = 0; i < PHRASE_SLOTS; i++) {
		if (phrase_slots[i].state.load(std::memory_order_acquire) == (uint8_t)PhraseSlotState::Retired) {
			phrase_slots[i].phrase.clear();
			phrase_slots[i].state.store((uint8_t)PhraseSlotState::Free, std::memory_order_release);
		}
	}
}

int AudioStreamPlaybackSymphonyMusic::_acquire_phrase_slot() {
	_reclaim_phrase_slots();
	for (int i = 0; i < PHRASE_SLOTS; i++) {
		uint8_t expected = (uint8_t)PhraseSlotState::Free;
		if (phrase_slots[i].state.compare_exchange_strong(expected, (uint8_t)PhraseSlotState::Filled, std::memory_order_acq_rel)) {
			return i;
		}
	}
	return -1;
}

bool AudioStreamPlaybackSymphonyMusic::_try_push_cmd(const MusicCmd &p_cmd) {
	return cmd_ring.try_push(p_cmd);
}

void AudioStreamPlaybackSymphonyMusic::_drain_commands() {
	MusicCmd cmd;
	while (cmd_ring.try_pop(cmd)) {
		switch (cmd.kind) {
			case MusicCmdKind::SubmitPhrase: {
				if (cmd.slot < 0 || cmd.slot >= PHRASE_SLOTS) {
					break;
				}
				PhraseSlot &slot = phrase_slots[cmd.slot];
				slot.state.store((uint8_t)PhraseSlotState::Active, std::memory_order_release);
				scheduler.submit_phrase(slot.phrase);
				slot.state.store((uint8_t)PhraseSlotState::Retired, std::memory_order_release);
				break;
			}
			case MusicCmdKind::ReplaceFuture:
				scheduler.replace_future(cmd.tick, cmd.generation);
				for (int i = 0; i < prepared_clip_count;) {
					PreparedClip &pc = prepared_clips[i];
					if (!pc.claimed && pc.generation <= cmd.generation && pc.tick >= cmd.tick) {
						_return_clip_slot(i);
						continue;
					}
					i++;
				}
				break;
			case MusicCmdKind::TempoStep:
				(void)scheduler.transport.schedule_tempo_step(cmd.value, cmd.tick);
				break;
			case MusicCmdKind::TempoRamp:
				(void)scheduler.transport.schedule_tempo_ramp(cmd.value, cmd.tick, cmd.duration_ticks);
				break;
			case MusicCmdKind::TrackGain: {
				if (cmd.track_id < 0 || cmd.track_id >= 256) {
					break;
				}
				const int pi = track_to_pool[cmd.track_id];
				if (pi < 0) {
					break;
				}
				const uint64_t start_frame = scheduler.transport.tick_to_frame(MAX(cmd.tick, (int64_t)0));
				uint64_t end_frame = start_frame;
				if (cmd.duration_ticks > 0) {
					end_frame = scheduler.transport.tick_to_frame(cmd.tick + (int64_t)cmd.duration_ticks);
				}
				_arm_gain(pi, cmd.value, start_frame, end_frame);
				break;
			}
			case MusicCmdKind::ClipPrepared: {
				latency_active = true;
				if (prepared_clip_count < MAX_PREPARED_CLIPS && cmd.ctx) {
					PreparedClip &pc = prepared_clips[prepared_clip_count++];
					pc.tick = cmd.tick;
					pc.note_id = cmd.note_id;
					pc.generation = cmd.generation;
					pc.ctx = cmd.ctx;
					pc.claimed = false;
				} else if (cmd.ctx) {
					cmd.ctx->request_return();
					_push_status_audio(StatusEntry::Kind::ClipDropped, cmd.generation, cmd.track_id, cmd.note_id, 0.0f);
				}
				break;
			}
			case MusicCmdKind::Reset:
				_audio_restart();
				break;
		}
	}
}

void AudioStreamPlaybackSymphonyMusic::_audio_restart() {
	const float rate = mix_rate_cached.load(std::memory_order_relaxed);
	scheduler.reset(rate > 0.0f ? rate : symphony_offline_rate(), music_stream.is_valid() ? music_stream->get_initial_bpm() : 120.0f);
	for (int p = 0; p < pool_count; p++) {
		for (int v = 0; v < pools[p].voice_count; v++) {
			SymphonyNoteVoice &slot = pools[p].voices[v];
			if (slot.active && slot.note_input) {
				slot.note_input->note_off(0);
			}
			slot.reset_idle();
		}
	}
	for (int i = 0; i < active_clip_count; i++) {
		if (active_clips[i]) {
			active_clips[i]->request_return();
		}
		active_clips[i] = nullptr;
	}
	active_clip_count = 0;
	for (int i = 0; i < prepared_clip_count; i++) {
		if (prepared_clips[i].ctx && !prepared_clips[i].claimed) {
			if (prepared_clips[i].ctx->get_ownership() == SymphonyStretchOwnership::Prepared) {
				prepared_clips[i].ctx->set_ownership(SymphonyStretchOwnership::Returning);
			} else {
				prepared_clips[i].ctx->request_return();
			}
		}
		prepared_clips[i] = PreparedClip{};
	}
	prepared_clip_count = 0;
	for (int i = 0; i < latency_delay.size(); i++) {
		latency_delay.write[i] = AudioFrame(0, 0);
	}
	latency_write = 0;
	latency_active = false;
	frames_rendered.store(0, std::memory_order_release);
}

void AudioStreamPlaybackSymphonyMusic::_return_clip_slot(int p_index) {
	if (p_index < 0 || p_index >= prepared_clip_count) {
		return;
	}
	PreparedClip &pc = prepared_clips[p_index];
	if (pc.ctx) {
		if (pc.ctx->get_ownership() == SymphonyStretchOwnership::Prepared) {
			pc.ctx->set_ownership(SymphonyStretchOwnership::Returning);
		} else {
			pc.ctx->request_return();
		}
		_push_status_audio(StatusEntry::Kind::ClipReturned, pc.generation, 0, pc.note_id, 0.0f);
	}
	prepared_clips[p_index] = prepared_clips[prepared_clip_count - 1];
	prepared_clips[prepared_clip_count - 1] = PreparedClip{};
	prepared_clip_count--;
}

void AudioStreamPlaybackSymphonyMusic::_return_stale_clips() {
	const uint64_t now = scheduler.frames_rendered;
	for (int i = 0; i < prepared_clip_count;) {
		PreparedClip &pc = prepared_clips[i];
		if (pc.claimed || !pc.ctx) {
			i++;
			continue;
		}
		const uint64_t target = scheduler.transport.tick_to_frame(pc.tick);
		if (target + 64 < now) {
			_return_clip_slot(i);
			continue;
		}
		i++;
	}
}

AudioStreamPlaybackSymphonyMusic::~AudioStreamPlaybackSymphonyMusic() {
	_clear_runtime();
}
