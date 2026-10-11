/**************************************************************************/
/*  symphony_music_scheduler.h                                            */
/**************************************************************************/
/* Bounded phrase handoff + sample-accurate event execution (music N1).   */
/* Phrase switches happen at the boundary frame, including mid-block.     */
/* Note-offs that belong to the outgoing phrase are carried, not dropped. */
/**************************************************************************/

#pragma once

#include "../core/symphony_seqlock.h"
#include "symphony_music_transport.h"

#include <cstdint>

enum class SymphonyMusicEventKind : uint8_t {
	NoteOff = 0, // before NoteOn at equal timestamps
	NoteOn = 1,
	TempoStep = 2,
	TrackGain = 3,
	ClipOn = 4, // note_id = clip instance; midi = transpose_st; velocity = tempo_ratio; payload = clip_index
};

struct SymphonyMusicEvent {
	int64_t tick = 0;
	SymphonyMusicEventKind kind = SymphonyMusicEventKind::NoteOn;
	int32_t track_id = 0;
	int32_t note_id = 0;
	int32_t midi = 60;
	float velocity = 1.0f;
	float payload = 0.0f;
	int32_t duration_ticks = 0;
	int32_t generation = 0;
};

struct SymphonyMusicPhrase {
	static constexpr int MAX_EVENTS = 4096;
	int64_t start_tick = 0;
	int32_t generation = 0;
	int32_t event_count = 0;
	SymphonyMusicEvent events[MAX_EVENTS];

	void clear() {
		start_tick = 0;
		generation = 0;
		event_count = 0;
	}

	bool push(const SymphonyMusicEvent &p_ev) {
		if (event_count >= MAX_EVENTS) {
			return false;
		}
		events[event_count++] = p_ev;
		return true;
	}

	void sort_stable() {
		for (int i = 1; i < event_count; i++) {
			SymphonyMusicEvent key = events[i];
			int j = i - 1;
			auto before = [](const SymphonyMusicEvent &a, const SymphonyMusicEvent &b) -> bool {
				if (a.tick != b.tick) {
					return a.tick < b.tick;
				}
				if (a.kind != b.kind) {
					return (uint8_t)a.kind < (uint8_t)b.kind;
				}
				return a.note_id < b.note_id;
			};
			while (j >= 0 && before(key, events[j])) {
				events[j + 1] = events[j];
				j--;
			}
			events[j + 1] = key;
		}
	}
};

class SymphonyMusicScheduler {
public:
	using Snapshot = SymphonyMusicTransport::Snapshot;

	enum class NoticeKind : uint8_t {
		PhraseLate = 0,
		NoteForcedOff = 1,
	};

	struct Notice {
		NoticeKind kind = NoticeKind::PhraseLate;
		int32_t generation = 0;
		int32_t track_id = 0;
		int32_t note_id = 0;
		float payload = 0.0f;
	};

	static constexpr int CARRY_CAP = 512;
	static constexpr int NOTICE_CAP = 32;

	SymphonyMusicTransport transport;
	SymphonyMusicPhrase current;
	SymphonyMusicPhrase next;
	bool has_current = false;
	bool has_next = false;
	int event_cursor = 0;
	uint64_t frames_rendered = 0;
	int32_t active_generation = 0;
	SymphonySeqLock<Snapshot> snapshot_lock;

	SymphonyMusicEvent carry_events[CARRY_CAP];
	uint64_t carry_frames[CARRY_CAP] = {};
	int carry_count = 0;
	int carry_cursor = 0;

	Notice notices[NOTICE_CAP];
	int notice_count = 0;

	void reset(float p_sample_rate, float p_bpm = 120.0f) {
		transport.reset(p_sample_rate, p_bpm);
		current.clear();
		next.clear();
		has_current = false;
		has_next = false;
		event_cursor = 0;
		frames_rendered = 0;
		active_generation = 0;
		carry_count = 0;
		carry_cursor = 0;
		notice_count = 0;
		snapshot_lock.store(transport.snapshot_at_frame(0));
	}

	bool submit_phrase(const SymphonyMusicPhrase &p_phrase) {
		if (p_phrase.event_count < 0 || p_phrase.event_count > SymphonyMusicPhrase::MAX_EVENTS) {
			return false;
		}
		SymphonyMusicPhrase incoming = p_phrase;
		for (int i = 0; i < incoming.event_count; i++) {
			incoming.events[i].generation = incoming.generation;
		}
		incoming.sort_stable();
		// The first phrase is the active one. A later submit is pending until its boundary.
		if (!has_current) {
			current = incoming;
			has_current = true;
			event_cursor = 0;
			active_generation = current.generation;
			_skip_elapsed_on_activate();
			return true;
		}
		next = incoming;
		has_next = true;
		return true;
	}

	// B1: generation is recorded but not yet enforced (B2 command path honours it).
	bool replace_future(int64_t p_from_tick, int32_t p_generation) {
		(void)p_generation;
		if (has_next && next.start_tick >= p_from_tick) {
			next.clear();
			has_next = false;
		}
		return true;
	}

	[[nodiscard]] uint64_t peek_event_frame() const {
		uint64_t best = UINT64_MAX;
		if (carry_cursor < carry_count) {
			best = MIN(best, carry_frames[carry_cursor]);
		}
		if (has_next) {
			const uint64_t boundary = transport.tick_to_frame(next.start_tick);
			best = MIN(best, boundary < frames_rendered ? frames_rendered : boundary);
		}
		for (int i = event_cursor; i < current.event_count; i++) {
			if (has_next && current.events[i].tick >= next.start_tick) {
				break;
			}
			const uint64_t ef = transport.tick_to_frame(current.events[i].tick);
			if (ef < frames_rendered) {
				continue;
			}
			best = MIN(best, ef);
			break;
		}
		return best;
	}

	template <typename Fn>
	void take_events_at(uint64_t p_frame, Fn &&p_on_event) {
		if (has_next) {
			const uint64_t boundary = transport.tick_to_frame(next.start_tick);
			if (boundary <= p_frame) {
				_switch_to_next(p_frame, p_on_event);
			}
		}
		while (carry_cursor < carry_count && carry_frames[carry_cursor] <= p_frame) {
			if (carry_frames[carry_cursor] == p_frame) {
				p_on_event(carry_events[carry_cursor], p_frame);
			}
			carry_cursor++;
		}
		while (event_cursor < current.event_count) {
			if (has_next && current.events[event_cursor].tick >= next.start_tick) {
				break;
			}
			const uint64_t ef = transport.tick_to_frame(current.events[event_cursor].tick);
			if (ef > p_frame) {
				break;
			}
			if (ef == p_frame) {
				p_on_event(current.events[event_cursor], p_frame);
			}
			event_cursor++;
		}
	}

	void commit_frames(int p_frames) {
		if (p_frames <= 0) {
			return;
		}
		frames_rendered += (uint64_t)p_frames;
		transport.set_rendered_frame(frames_rendered);
	}

	void publish_snapshot() {
		transport.set_snapshot_generation(active_generation);
		snapshot_lock.store(transport.snapshot_at_frame(frames_rendered));
	}

	template <typename Fn>
	void advance(int p_frames, Fn &&p_on_event) {
		if (p_frames <= 0) {
			return;
		}
		const uint64_t end = frames_rendered + (uint64_t)p_frames;
		int guard = 0;
		while (frames_rendered < end && guard < p_frames * 4 + 8) {
			guard++;
			const uint64_t ev = peek_event_frame();
			if (ev == frames_rendered) {
				take_events_at(frames_rendered, p_on_event);
				continue;
			}
			uint64_t step_end = end;
			if (ev > frames_rendered && ev < step_end) {
				step_end = ev;
			}
			if (step_end <= frames_rendered) {
				take_events_at(frames_rendered, p_on_event);
				frames_rendered++;
				transport.set_rendered_frame(frames_rendered);
				continue;
			}
			frames_rendered = step_end;
			transport.set_rendered_frame(frames_rendered);
		}
		publish_snapshot();
	}

	[[nodiscard]] Snapshot get_snapshot() const { return snapshot_lock.load(); }

	[[nodiscard]] int drain_notices(Notice *p_out, int p_max) {
		const int n = MIN(notice_count, p_max);
		for (int i = 0; i < n; i++) {
			p_out[i] = notices[i];
		}
		notice_count = 0;
		return n;
	}

private:
	void _push_notice(NoticeKind p_kind, int32_t p_gen, int32_t p_track, int32_t p_note, float p_payload) {
		if (notice_count >= NOTICE_CAP) {
			return;
		}
		Notice &n = notices[notice_count++];
		n.kind = p_kind;
		n.generation = p_gen;
		n.track_id = p_track;
		n.note_id = p_note;
		n.payload = p_payload;
	}

	void _skip_elapsed_on_activate() {
		const uint64_t start_frame = transport.tick_to_frame(current.start_tick);
		if (start_frame >= frames_rendered) {
			return;
		}
		int skipped = 0;
		while (event_cursor < current.event_count) {
			const uint64_t ef = transport.tick_to_frame(current.events[event_cursor].tick);
			if (ef >= frames_rendered) {
				break;
			}
			if (current.events[event_cursor].kind == SymphonyMusicEventKind::NoteOn) {
				skipped++;
			}
			event_cursor++;
		}
		_push_notice(NoticeKind::PhraseLate, active_generation, 0, 0, (float)skipped);
	}

	bool _push_carry(const SymphonyMusicEvent &p_ev, uint64_t p_frame) {
		if (carry_count >= CARRY_CAP) {
			return false;
		}
		int i = carry_count;
		while (i > carry_cursor && carry_frames[i - 1] > p_frame) {
			carry_events[i] = carry_events[i - 1];
			carry_frames[i] = carry_frames[i - 1];
			i--;
		}
		carry_events[i] = p_ev;
		carry_frames[i] = p_frame;
		carry_count++;
		return true;
	}

	template <typename Fn>
	void _switch_to_next(uint64_t p_frame, Fn &&p_on_event) {
		const uint64_t boundary = transport.tick_to_frame(next.start_tick);
		const bool late = boundary < frames_rendered;
		const uint64_t switch_frame = late ? p_frame : boundary;
		for (int i = event_cursor; i < current.event_count; i++) {
			const SymphonyMusicEvent &ev = current.events[i];
			if (ev.tick < next.start_tick || ev.kind != SymphonyMusicEventKind::NoteOff) {
				continue;
			}
			uint64_t ef = transport.tick_to_frame(ev.tick);
			if (ef < switch_frame) {
				ef = switch_frame;
			}
			if (!_push_carry(ev, ef)) {
				p_on_event(ev, switch_frame);
				_push_notice(NoticeKind::NoteForcedOff, ev.generation, ev.track_id, ev.note_id, 1.0f);
			}
		}
		current = next;
		has_current = true;
		active_generation = current.generation;
		event_cursor = 0;
		next.clear();
		has_next = false;
		if (late) {
			int skipped = 0;
			while (event_cursor < current.event_count) {
				const uint64_t ef = transport.tick_to_frame(current.events[event_cursor].tick);
				if (ef >= p_frame) {
					break;
				}
				if (current.events[event_cursor].kind == SymphonyMusicEventKind::NoteOn) {
					skipped++;
				}
				event_cursor++;
			}
			_push_notice(NoticeKind::PhraseLate, active_generation, 0, 0, (float)skipped);
		}
	}
};
