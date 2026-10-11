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

void AudioStreamPlaybackSymphonyMusic::_push_status(StatusEntry::Kind p_kind, int32_t p_gen, int32_t p_track, int32_t p_note, float p_payload) {
	const uint32_t w = status_w.load(std::memory_order_relaxed);
	const uint32_t r = status_r.load(std::memory_order_acquire);
	if ((w - r) >= STATUS_CAP) {
		return; // drop oldest-path overflow silently
	}
	StatusEntry &e = status_ring[w % STATUS_CAP];
	e.kind = p_kind;
	e.generation = p_gen;
	e.track_id = p_track;
	e.note_id = p_note;
	e.payload = p_payload;
	status_w.store(w + 1, std::memory_order_release);
}

void AudioStreamPlaybackSymphonyMusic::_clear_runtime() {
	for (int i = 0; i < pool_count; i++) {
		pools[i].clear();
	}
	pool_count = 0;
	for (int i = 0; i < 256; i++) {
		track_to_pool[i] = -1;
	}
	prepared_clips.clear();
	active_clip_count = 0;
	for (int i = 0; i < MAX_ACTIVE_CLIPS; i++) {
		active_clips[i] = nullptr;
	}
	stretch_pool.reclaim_returned();
	for (int i = 0; i < SymphonyStretchPool::MAX_CONTEXTS; i++) {
		if (stretch_pool.contexts[i].ownership != SymphonyStretchOwnership::Free) {
			stretch_pool.contexts[i].ownership = SymphonyStretchOwnership::Returning;
		}
	}
	stretch_pool.reclaim_returned();
	latency_delay.clear();
	latency_samples = 0;
	latency_write = 0;
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
			_push_status(StatusEntry::Kind::NoteDropped, 0, tc.track_id, -1, 0.0f);
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

	SymphonyMusicPhrase phrase;
	String err;
	if (!_parse_phrase(p_phrase, phrase, err)) {
		ERR_PRINT(vformat("AudioStreamPlaybackSymphonyMusic::submit_phrase: %s", err));
		return ERR_INVALID_PARAMETER;
	}

	const Array events = p_phrase.get("events", Array());
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
		if (clip_index < 0 || music_stream.is_null() || clip_index >= music_stream->clip_count || !music_stream->clips[clip_index].registered) {
			_push_status(StatusEntry::Kind::ClipDropped, phrase.generation, phrase.events[i].track_id, phrase.events[i].note_id, 0.0f);
			continue;
		}
		phrase.events[i].payload = (float)clip_index;
		const AudioStreamSymphonyMusic::ClipConfig &cc = music_stream->clips[clip_index];
		const float tempo_ratio = phrase.events[i].velocity > 0.0f ? phrase.events[i].velocity : 1.0f;
		const float transpose = (float)phrase.events[i].midi;
		SymphonyStretchContext *ctx = stretch_pool.acquire_prepared(cc.desc, tempo_ratio, transpose);
		if (!ctx) {
			_push_status(StatusEntry::Kind::ClipDropped, phrase.generation, phrase.events[i].track_id, phrase.events[i].note_id, 0.0f);
			continue;
		}
		PreparedClip pc;
		pc.tick = phrase.events[i].tick;
		pc.note_id = phrase.events[i].note_id;
		pc.ctx = ctx;
		pc.claimed = false;
		prepared_clips.push_back(pc);
	}

	const int lat = stretch_pool.common_latency();
	if (lat > latency_samples) {
		latency_samples = lat;
		latency_delay.resize(latency_samples + SYMPHONY_MICRO_BLOCK_SIZE * 2);
		for (int i = 0; i < latency_delay.size(); i++) {
			latency_delay.write[i] = AudioFrame(0, 0);
		}
		latency_write = 0;
	}

	if (!scheduler.submit_phrase(phrase)) {
		return ERR_INVALID_PARAMETER;
	}
	_push_status(StatusEntry::Kind::PhraseActivated, phrase.generation, 0, 0, (float)phrase.event_count);
	return OK;
}

Error AudioStreamPlaybackSymphonyMusic::replace_future(int64_t p_from_tick, int32_t p_generation) {
	ERR_FAIL_COND_V(!Thread::is_main_thread(), ERR_UNAVAILABLE);
	scheduler.replace_future(p_from_tick, p_generation);
	return OK;
}

Error AudioStreamPlaybackSymphonyMusic::schedule_tempo(float p_bpm, int64_t p_start_tick, int32_t p_ramp_ticks) {
	ERR_FAIL_COND_V(!Thread::is_main_thread(), ERR_UNAVAILABLE);
	bool ok = false;
	if (p_ramp_ticks > 0) {
		ok = scheduler.transport.schedule_tempo_ramp(p_bpm, p_start_tick, p_ramp_ticks);
	} else {
		ok = scheduler.transport.schedule_tempo_step(p_bpm, p_start_tick);
	}
	if (ok) {
		_push_status(StatusEntry::Kind::TempoApplied, scheduler.active_generation, 0, 0, p_bpm);
		return OK;
	}
	return ERR_INVALID_PARAMETER;
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
	const uint64_t start_frame = scheduler.transport.tick_to_frame(MAX(p_start_tick, (int64_t)0));
	uint64_t end_frame = start_frame;
	if (p_fade_ticks > 0) {
		end_frame = scheduler.transport.tick_to_frame(p_start_tick + (int64_t)p_fade_ticks);
	}
	_arm_gain(pi, p_gain, start_frame, end_frame);
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
		const StatusEntry::Kind kind = notices[i].kind == SymphonyMusicScheduler::NoticeKind::PhraseLate
				? StatusEntry::Kind::PhraseLate
				: StatusEntry::Kind::NoteForcedOff;
		_push_status(kind, notices[i].generation, notices[i].track_id, notices[i].note_id, notices[i].payload);
	}
	SymphonyNotePool::ForcedOff forced[SymphonyNotePool::MAX_VOICES];
	for (int p = 0; p < pool_count; p++) {
		const int fn = pools[p].drain_forced(forced, SymphonyNotePool::MAX_VOICES);
		for (int i = 0; i < fn; i++) {
			_push_status(StatusEntry::Kind::NoteForcedOff, forced[i].generation, pools[p].track_id, forced[i].note_id, 1.0f);
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
	uint32_t r = status_r.load(std::memory_order_relaxed);
	const uint32_t w = status_w.load(std::memory_order_acquire);
	while (r != w) {
		const StatusEntry &e = status_ring[r % STATUS_CAP];
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
		}
		d["generation"] = e.generation;
		d["track_id"] = e.track_id;
		d["note_id"] = e.note_id;
		d["payload"] = e.payload;
		out.push_back(d);
		r++;
	}
	status_r.store(r, std::memory_order_release);
	stretch_pool.reclaim_returned();
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
				_push_status(StatusEntry::Kind::NoteDropped, scheduler.active_generation, p_ev.track_id, p_ev.note_id, 0.0f);
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
			for (int i = 0; i < (int)prepared_clips.size(); i++) {
				PreparedClip &pc = prepared_clips[i];
				if (!pc.claimed && pc.note_id == p_ev.note_id && pc.tick == p_ev.tick && pc.ctx) {
					ctx = pc.ctx;
					pc.claimed = true;
					break;
				}
			}
			if (!ctx || !ctx->activate()) {
				_push_status(StatusEntry::Kind::ClipDropped, scheduler.active_generation, p_ev.track_id, p_ev.note_id, 0.0f);
				return;
			}
			if (active_clip_count < MAX_ACTIVE_CLIPS) {
				active_clips[active_clip_count++] = ctx;
			} else {
				ctx->request_return();
				_push_status(StatusEntry::Kind::ClipDropped, scheduler.active_generation, p_ev.track_id, p_ev.note_id, 0.0f);
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
		if (!ctx || ctx->ownership != SymphonyStretchOwnership::Active || ctx->finished) {
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

	if (latency_samples > 0 && latency_delay.size() > 0) {
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
		} else {
			scheduler.reset(rate, music_stream.is_valid() ? music_stream->get_initial_bpm() : 120.0f);
		}
		set_voice_tier((int)SoundEvent::TIER_VITAL);
		set_voice_importance(SoundEvent::TIER_BAND_VITAL);
		_register_with_manager();
	}
	frames_rendered.store(0, std::memory_order_release);
	scheduler.frames_rendered = 0;
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

AudioStreamPlaybackSymphonyMusic::~AudioStreamPlaybackSymphonyMusic() {
	_clear_runtime();
}
