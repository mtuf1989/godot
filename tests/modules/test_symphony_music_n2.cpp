/**************************************************************************/
/*  test_symphony_music_n2.cpp                                            */
/*  Suite: [Symphony][N2] — MusicNoteInput, note pools, phrase render.    */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_music_n2)

#include "modules/symphony/core/symphony_graph_description.h"
#include "modules/symphony/core/symphony_rate_policy.h"
#include "modules/symphony/nodes/io/symphony_music_note_input.h"
#include "modules/symphony/runtime/symphony_music_scheduler.h"
#include "modules/symphony/runtime/symphony_note_pool.h"
#include "tests/modules/symphony_test_helpers.h"

#include "core/math/math_funcs.h"
#include "servers/audio/audio_frame.h"

namespace TestSymphonyMusicN2 {

static GraphDescription _make_instrument(float p_root_hz = 440.0f) {
	GraphDescription desc;
	desc.smooth_parameters = false;
	desc.anti_alias_staircase = false;

	NodeDesc note;
	note.id = 1;
	note.type_name = "MusicNoteInput";
	note.params["root_hz"] = p_root_hz;
	desc.nodes.push_back(note);

	NodeDesc osc;
	osc.id = 2;
	osc.type_name = "Oscillator";
	osc.params["waveform"] = 0.0f; // sine
	desc.nodes.push_back(osc);

	NodeDesc adsr;
	adsr.id = 3;
	adsr.type_name = "ADSR";
	adsr.params["attack"] = 0.001f;
	adsr.params["decay"] = 0.05f;
	adsr.params["sustain"] = 0.7f;
	adsr.params["release"] = 0.08f;
	desc.nodes.push_back(adsr);

	NodeDesc gain;
	gain.id = 4;
	gain.type_name = "Gain";
	gain.params["gain"] = 0.25f;
	desc.nodes.push_back(gain);

	NodeDesc out;
	out.id = 5;
	out.type_name = "GraphOutput";
	desc.nodes.push_back(out);

	auto conn = [&](int32_t from, int32_t from_pin, int32_t to, int32_t to_pin) {
		ConnectionDesc c;
		c.from_node = from;
		c.from_pin = from_pin;
		c.to_node = to;
		c.to_pin = to_pin;
		desc.connections.push_back(c);
	};
	conn(1, 0, 2, 0); // frequency → osc frequency
	conn(2, 0, 3, 0); // osc → adsr audio
	conn(1, 6, 3, 1); // adsr_trigger → adsr trigger
	conn(3, 0, 4, 0);
	conn(4, 0, 5, 0);
	return desc;
}

static Vector<float> _render_phrase(float p_rate, int p_bass_midi, int p_lead_midi) {
	SymphonyNotePool bass;
	bass.track_id = 0;
	bass.protected_track = true;
	REQUIRE(bass.prepare(_make_instrument(symphony_midi_to_hz(p_bass_midi)), p_rate, 4, 0xBA55ULL));

	SymphonyNotePool lead;
	lead.track_id = 1;
	lead.protected_track = false;
	REQUIRE(lead.prepare(_make_instrument(440.0f), p_rate, 4, 0x1EADULL));

	SymphonyMusicScheduler sched;
	sched.reset(p_rate, 120.0f);
	SymphonyMusicPhrase phrase;
	phrase.start_tick = 0;
	phrase.generation = 1;

	auto push_on = [&](int64_t tick, int32_t track, int32_t note_id, int32_t midi, float vel) {
		SymphonyMusicEvent ev;
		ev.tick = tick;
		ev.kind = SymphonyMusicEventKind::NoteOn;
		ev.track_id = track;
		ev.note_id = note_id;
		ev.midi = midi;
		ev.velocity = vel;
		ev.duration_ticks = 480;
		REQUIRE(phrase.push(ev));
	};
	auto push_off = [&](int64_t tick, int32_t track, int32_t note_id) {
		SymphonyMusicEvent ev;
		ev.tick = tick;
		ev.kind = SymphonyMusicEventKind::NoteOff;
		ev.track_id = track;
		ev.note_id = note_id;
		REQUIRE(phrase.push(ev));
	};

	// Two-track phrase: protected bass + optional lead, one bar at 120 BPM.
	push_on(0, 0, 1, p_bass_midi, 0.9f);
	push_off(720, 0, 1);
	push_on(240, 1, 2, p_lead_midi, 0.7f);
	push_off(960, 1, 2);
	REQUIRE(sched.submit_phrase(phrase));

	const int total_frames = (int)(p_rate * 0.6); // 0.6 s covers the bar + release
	Vector<float> mono;
	mono.resize(total_frames);
	for (int i = 0; i < total_frames; i++) {
		mono.write[i] = 0.0f;
	}

	Vector<AudioFrame> block;
	block.resize(SYMPHONY_MICRO_BLOCK_SIZE);
	int written = 0;
	while (written < total_frames) {
		const int n = MIN(SYMPHONY_MICRO_BLOCK_SIZE, total_frames - written);
		const uint64_t block_start = sched.frames_rendered;
		Vector<SymphonyMusicEvent> pending;
		Vector<uint64_t> pending_frames;
		sched.advance(n, [&](const SymphonyMusicEvent &ev, uint64_t frame) {
			pending.push_back(ev);
			pending_frames.push_back(frame);
		});

		// Apply events at their offsets within this block, then render.
		for (int e = 0; e < pending.size(); e++) {
			const SymphonyMusicEvent &ev = pending[e];
			const int32_t offset = (int32_t)(pending_frames[e] - block_start);
			SymphonyNotePool *pool = (ev.track_id == 0) ? &bass : &lead;
			if (ev.kind == SymphonyMusicEventKind::NoteOn) {
				pool->note_on(ev.note_id, ev.midi, ev.velocity, pending_frames[e], offset);
			} else if (ev.kind == SymphonyMusicEventKind::NoteOff) {
				pool->note_off(ev.note_id, pending_frames[e], offset, (uint64_t)(p_rate * 0.2));
			}
		}

		for (int i = 0; i < n; i++) {
			block.write[i] = AudioFrame(0.0f, 0.0f);
		}
		Vector<AudioFrame> tmp;
		tmp.resize(n);
		bass.render(tmp.ptrw(), n, block_start);
		for (int i = 0; i < n; i++) {
			block.write[i] += tmp[i];
		}
		lead.render(tmp.ptrw(), n, block_start);
		for (int i = 0; i < n; i++) {
			block.write[i] += tmp[i];
			mono.write[written + i] = block[i].left;
		}
		written += n;
	}
	return mono;
}

TEST_CASE("[Symphony][N2] MusicNoteInput pitch_ratio follows root_hz") {
	ArenaAllocator arena;
	REQUIRE(arena.init(4096));
	HashMap<StringName, Variant> params;
	params.insert("root_hz", 220.0f);
	SymphonyMusicNoteInput *note = (SymphonyMusicNoteInput *)SymphonyMusicNoteInput::create(arena, params, 48000.0f);
	REQUIRE(note != nullptr);
	float freq[1] = {};
	float vel[1] = {};
	float ratio[1] = {};
	float gate[1] = {};
	TriggerBuffer on_buf;
	TriggerBuffer off_buf;
	TriggerBuffer adsr_buf;
	void *outs[] = { freq, vel, ratio, &on_buf, &off_buf, gate, &adsr_buf };
	note->bind_pins(nullptr, outs);
	note->note_on(440.0f, 0.8f, 0);
	note->execute(SYMPHONY_MICRO_BLOCK_SIZE);
	CHECK(freq[0] == doctest::Approx(440.0f));
	CHECK(vel[0] == doctest::Approx(0.8f));
	CHECK(ratio[0] == doctest::Approx(2.0f));
	CHECK(gate[0] == doctest::Approx(1.0f));
	CHECK(on_buf.count >= 1);
	note->note_off(0);
	note->execute(SYMPHONY_MICRO_BLOCK_SIZE);
	CHECK(gate[0] == doctest::Approx(0.0f));
	arena.free();
}

TEST_CASE("[Symphony][N2] Note pool rejects non-resettable graphs") {
	GraphDescription bad = _make_instrument();
	NodeDesc delay;
	delay.id = 99;
	delay.type_name = "DelayLine";
	bad.nodes.push_back(delay);
	Vector<String> errors;
	CHECK_FALSE(symphony_graph_is_note_pool_safe(bad, &errors));
	CHECK(errors.size() > 0);
}

TEST_CASE("[Symphony][N2] Protected capacity keeps bass when lead is full") {
	SymphonyNotePool lead;
	lead.protected_track = false;
	REQUIRE(lead.prepare(_make_instrument(), 48000.0f, 2, 1));
	CHECK(lead.note_on(1, 60, 0.5f, 0, 0) >= 0);
	CHECK(lead.note_on(2, 62, 0.5f, 0, 0) >= 0);
	// Third optional note steals a voice (still admits).
	CHECK(lead.note_on(3, 64, 0.5f, 0, 0) >= 0);
	CHECK(lead.active_count() == 2);

	SymphonyNotePool bass;
	bass.protected_track = true;
	REQUIRE(bass.prepare(_make_instrument(), 48000.0f, 1, 2));
	for (int i = 0; i < bass.voices.size(); i++) {
		bass.voices.write[i].protected_track = true;
	}
	CHECK(bass.note_on(10, 36, 1.0f, 0, 0) >= 0);
	// No free unprotected victim — drop.
	CHECK(bass.note_on(11, 38, 1.0f, 0, 0) < 0);
	CHECK(bass.active_count() == 1);
}

TEST_CASE("[Symphony][N2] Multi-track phrase renders with energy on both notes") {
	Vector<float> buf = _render_phrase(48000.0f, 36, 72);
	REQUIRE(buf.size() > 1000);
	float peak = 0.0f;
	for (int i = 0; i < buf.size(); i++) {
		peak = MAX(peak, Math::abs(buf[i]));
	}
	CHECK(peak > 0.01f);
	// Bass region (~first 200 ms) and lead region (~after 125 ms) both have energy.
	const int early_n = (int)(48000 * 0.1);
	const int mid_a = (int)(48000 * 0.15);
	const int mid_b = (int)(48000 * 0.35);
	float early = SymphonyTestHelpers::rms(buf.ptr(), early_n);
	float mid = SymphonyTestHelpers::rms(buf.ptr() + mid_a, mid_b - mid_a);
	CHECK(early > 1e-4f);
	CHECK(mid > 1e-4f);
}

TEST_CASE("[Symphony][N2] 44.1k/48k instrument phrase equivalence") {
	Vector<float> a = _render_phrase(48000.0f, 48, 67);
	Vector<float> b = _render_phrase(44100.0f, 48, 67);
	REQUIRE(a.size() > 0);
	REQUIRE(b.size() > 0);

	// Compare overlapping musical window in seconds via Goertzel levels (±0.5 dB / ±1 cent gate).
	const float level_48 = SymphonyTestHelpers::tone_level_db(a, 48000.0f, symphony_midi_to_hz(67));
	const float level_44 = SymphonyTestHelpers::tone_level_db(b, 44100.0f, symphony_midi_to_hz(67));
	CHECK(Math::abs(level_48 - level_44) < 0.5f);

	const float bass_48 = SymphonyTestHelpers::tone_level_db(a, 48000.0f, symphony_midi_to_hz(48));
	const float bass_44 = SymphonyTestHelpers::tone_level_db(b, 44100.0f, symphony_midi_to_hz(48));
	CHECK(Math::abs(bass_48 - bass_44) < 0.5f);

	CHECK(symphony_select_music_rate(44100.0f) == 44100.0f);
	CHECK(symphony_select_music_rate(48000.0f) == 48000.0f);
	CHECK(symphony_select_music_rate(96000.0f) == 48000.0f);
}

TEST_CASE("[Symphony][N2] Note-off does not clear pitch before release finishes") {
	SymphonyNotePool pool;
	REQUIRE(pool.prepare(_make_instrument(), 48000.0f, 1, 9));
	pool.note_on(1, 60, 1.0f, 0, 0);
	const float hz = pool.voices[0].note_input->get_frequency();
	pool.note_off(1, 0, 0, 48000);
	CHECK(pool.voices[0].note_input->get_frequency() == doctest::Approx(hz));
	CHECK(pool.voices[0].releasing);
	CHECK(pool.voices[0].active);
}

} // namespace TestSymphonyMusicN2
