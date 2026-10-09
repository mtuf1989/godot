/**************************************************************************/
/*  symphony_music_scheduler.h                                            */
/**************************************************************************/
/* Bounded phrase handoff + sample-accurate event execution (music N1).   */
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
	float payload = 0.0f; // tempo bpm / gain
	int32_t duration_ticks = 0;
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

	// Stable total order: tick, then NoteOff before NoteOn, then note_id.
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

// Main-thread builds phrases; audio advances frames and fires events at exact samples.
class SymphonyMusicScheduler {
public:
	using Snapshot = SymphonyMusicTransport::Snapshot;

	SymphonyMusicTransport transport;
	SymphonyMusicPhrase current;
	SymphonyMusicPhrase next;
	bool has_next = false;
	int event_cursor = 0;
	uint64_t frames_rendered = 0;
	int32_t active_generation = 0;
	SymphonySeqLock<Snapshot> snapshot_lock;

	void reset(float p_sample_rate, float p_bpm = 120.0f) {
		transport.reset(p_sample_rate, p_bpm);
		current.clear();
		next.clear();
		has_next = false;
		event_cursor = 0;
		frames_rendered = 0;
		active_generation = 0;
		snapshot_lock.store(transport.snapshot_at_frame(0));
	}

	bool submit_phrase(const SymphonyMusicPhrase &p_phrase) {
		if (p_phrase.event_count < 0 || p_phrase.event_count > SymphonyMusicPhrase::MAX_EVENTS) {
			return false;
		}
		next = p_phrase;
		next.sort_stable();
		has_next = true;
		return true;
	}

	bool replace_future(int64_t p_from_tick, int32_t p_generation) {
		(void)p_generation;
		if (has_next && next.start_tick >= p_from_tick) {
			next.clear();
			has_next = false;
		}
		return true;
	}

	// Advance by p_frames; callback(ev, absolute_frame) for each event in range.
	template <typename Fn>
	void advance(int p_frames, Fn &&p_on_event) {
		if (p_frames <= 0) {
			return;
		}
		const uint64_t start = frames_rendered;
		const uint64_t end = start + (uint64_t)p_frames;

		if (has_next) {
			const uint64_t next_frame = transport.tick_to_frame(next.start_tick);
			if (next_frame <= start) {
				current = next;
				current.sort_stable();
				active_generation = current.generation;
				event_cursor = 0;
				next.clear();
				has_next = false;
				while (event_cursor < current.event_count) {
					const uint64_t ef = transport.tick_to_frame(current.events[event_cursor].tick);
					if (ef >= start) {
						break;
					}
					event_cursor++;
				}
			}
		}

		while (event_cursor < current.event_count) {
			const SymphonyMusicEvent &ev = current.events[event_cursor];
			const uint64_t ef = transport.tick_to_frame(ev.tick);
			if (ef >= end) {
				break;
			}
			if (ef >= start) {
				p_on_event(ev, ef);
			}
			event_cursor++;
		}

		frames_rendered = end;
		snapshot_lock.store(transport.snapshot_at_frame(frames_rendered));
	}

	[[nodiscard]] Snapshot get_snapshot() const { return snapshot_lock.load(); }
};
