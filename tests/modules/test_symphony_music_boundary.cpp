/**************************************************************************/
/*  test_symphony_music_boundary.cpp                                      */
/*  Suite: [Symphony][B1] — phrase boundaries, gains, allocation-free mix.*/
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_music_boundary)

#include "modules/symphony/core/symphony_rate_policy.h"
#include "modules/symphony/runtime/symphony_music_transport.h"
#include "modules/symphony/stream/audio_stream_playback_symphony_music.h"
#include "modules/symphony/stream/audio_stream_symphony.h"
#include "modules/symphony/stream/audio_stream_symphony_music.h"
#include "tests/modules/symphony_test_helpers.h"

#include "core/os/memory.h"

using namespace SymphonyTestHelpers;

namespace TestSymphonyMusicBoundary {

static Ref<AudioStreamPlaybackSymphonyMusic> _playback(float p_rate, float p_bpm) {
	Ref<AudioStreamSymphony> instrument;
	instrument.instantiate();
	instrument->set_mix_rate(p_rate);
	instrument->set_graph_description(make_click_instrument());

	Ref<AudioStreamSymphonyMusic> music;
	music.instantiate();
	music->set_mix_rate(p_rate);
	music->set_initial_bpm(p_bpm);
	REQUIRE(music->set_track_instrument(0, instrument, 4, true) == OK);
	Ref<AudioStreamPlaybackSymphonyMusic> pb = music->instantiate_playback();
	REQUIRE(pb.is_valid());
	pb->start();
	return pb;
}

static Dictionary _note(const char *p_kind, int64_t p_tick, int p_note, int p_dur = 0) {
	Dictionary ev;
	ev["kind"] = p_kind;
	ev["tick"] = p_tick;
	ev["track"] = 0;
	ev["note_id"] = p_note;
	ev["midi"] = 60;
	ev["velocity"] = 1.0;
	if (p_dur > 0) {
		ev["duration_ticks"] = p_dur;
	}
	return ev;
}

static Dictionary _phrase(int64_t p_start, int p_gen, const Array &p_events) {
	Dictionary phrase;
	phrase["start_tick"] = p_start;
	phrase["generation"] = p_gen;
	phrase["events"] = p_events;
	return phrase;
}

TEST_CASE("[Symphony][B1] Back-to-back phrases keep the downbeat and release") {
	const float rates[] = { 48000.0f, 44100.0f, 48000.0f, 44100.0f };
	const float bpms[] = { 120.0f, 120.0f, 128.0f, 97.0f };
	for (int i = 0; i < 4; i++) {
		const float rate = rates[i];
		const float bpm = bpms[i];
		SymphonyMusicTransport transport;
		transport.reset(rate, bpm);
		const int64_t phrase_ticks = 4 * 4 * SymphonyMusicTransport::PPQN;
		const uint64_t boundary = transport.tick_to_frame(phrase_ticks);
		const uint64_t second_off = transport.tick_to_frame(phrase_ticks + 480);

		Ref<AudioStreamPlaybackSymphonyMusic> pb = _playback(rate, bpm);
		Array ev1;
		ev1.push_back(_note("note_on", 0, 1, 240));
		REQUIRE(pb->submit_phrase(_phrase(0, 1, ev1)) == OK);
		Array ev2;
		ev2.push_back(_note("note_on", phrase_ticks, 2, 480));
		REQUIRE(pb->submit_phrase(_phrase(phrase_ticks, 2, ev2)) == OK);

		const int gap = (int)transport.tick_to_frame(240) + (int)(rate * 0.25f);
		const int total = (int)second_off + (int)(rate * 0.5f);
		Vector<AudioFrame> audio = render_music(pb.ptr(), total, 256);
		CHECK(first_onset_frame(audio, 0.2f, 0) == 0);
		CHECK(first_onset_frame(audio, 0.2f, gap) == (int)boundary);
		CHECK(pb->_test_active_voice_count() == 0);
		pb->stop();
	}
}

TEST_CASE("[Symphony][B1] Note held across a phrase boundary releases at its own tick") {
	const float rate = 48000.0f;
	SymphonyMusicTransport transport;
	transport.reset(rate, 120.0f);
	const int64_t phrase_ticks = 4 * SymphonyMusicTransport::PPQN; // 1 bar
	const int64_t off_tick = phrase_ticks + 480;
	const uint64_t boundary = transport.tick_to_frame(phrase_ticks);
	const uint64_t off_frame = transport.tick_to_frame(off_tick);

	Ref<AudioStreamPlaybackSymphonyMusic> pb = _playback(rate, 120.0f);
	Array ev1;
	ev1.push_back(_note("note_on", 0, 3, (int)off_tick));
	REQUIRE(pb->submit_phrase(_phrase(0, 1, ev1)) == OK);
	Array ev2;
	ev2.push_back(_note("note_on", phrase_ticks, 4, 120));
	REQUIRE(pb->submit_phrase(_phrase(phrase_ticks, 2, ev2)) == OK);

	const int total = (int)off_frame + (int)(rate * 0.4f);
	Vector<AudioFrame> audio = render_music(pb.ptr(), total, 256);
	CHECK(Math::abs(audio[(int)boundary + 8].left) > 0.2f);
	CHECK(pb->_test_active_voice_count() == 0);
	pb->stop();
}

TEST_CASE("[Symphony][B1] Reused note_id in a new generation does not steal the old voice") {
	const float rate = 48000.0f;
	SymphonyMusicTransport transport;
	transport.reset(rate, 120.0f);
	const int64_t phrase_ticks = 2 * SymphonyMusicTransport::PPQN;
	const uint64_t boundary = transport.tick_to_frame(phrase_ticks);

	Ref<AudioStreamPlaybackSymphonyMusic> pb = _playback(rate, 120.0f);
	Array ev1;
	ev1.push_back(_note("note_on", 0, 7, (int)(phrase_ticks * 3)));
	REQUIRE(pb->submit_phrase(_phrase(0, 1, ev1)) == OK);
	Array ev2;
	Dictionary off = _note("note_off", phrase_ticks, 7);
	ev2.push_back(off);
	REQUIRE(pb->submit_phrase(_phrase(phrase_ticks, 2, ev2)) == OK);

	Vector<AudioFrame> audio = render_music(pb.ptr(), (int)boundary + 64, 128);
	CHECK(Math::abs(audio[(int)boundary + 32].left) > 0.2f);
	CHECK(pb->_test_active_voice_count() == 1);
	pb->stop();
}

TEST_CASE("[Symphony][B1] A phrase submitted after its start tick is reported late") {
	Ref<AudioStreamPlaybackSymphonyMusic> pb = _playback(48000.0f, 120.0f);
	Vector<AudioFrame> skip = render_music(pb.ptr(), 48000, 256);
	(void)skip;
	Array ev;
	ev.push_back(_note("note_on", 0, 1, 480));
	REQUIRE(pb->submit_phrase(_phrase(0, 9, ev)) == OK);
	Vector<AudioFrame> more = render_music(pb.ptr(), 128, 64);
	(void)more;
	Array status = pb->drain_status();
	bool late = false;
	for (int i = 0; i < status.size(); i++) {
		const Dictionary d = status[i];
		if (String(d.get("kind", "")) == "phrase_late") {
			late = true;
		}
	}
	CHECK(late);
	pb->stop();
}

TEST_CASE("[Symphony][B1] Tempo changes over a long render stay within the segment cap") {
	Ref<AudioStreamPlaybackSymphonyMusic> pb = _playback(48000.0f, 120.0f);
	AudioFrame chunk[256];
	int rejected = 0;
	int voices_peak = 0;
	// 10 minutes at 48 kHz, a tempo step every 3 seconds (200 steps).
	const int total = 48000 * 60 * 10;
	int rendered = 0;
	int steps = 0;
	while (rendered < total) {
		const int n = MIN(256, total - rendered);
		REQUIRE(pb->mix_graph_rate(chunk, n) == n);
		rendered += n;
		voices_peak = MAX(voices_peak, pb->_test_active_voice_count());
		if (steps < 200 && rendered >= (steps + 1) * 48000 * 3) {
			const int64_t tick = (int64_t)pb->get_transport_snapshot().get("tick", 0);
			const float bpm = 80.0f + (float)(steps % 40);
			if (pb->schedule_tempo(bpm, tick + 960, 0) != OK) {
				rejected++;
			}
			steps++;
		}
	}
	CHECK(steps == 200);
	CHECK(rejected == 0);
	CHECK(pb->_test_transport_segment_count() <= 64);
	CHECK(voices_peak < 8);
	pb->stop();
}

TEST_CASE("[Symphony][B1] Steady mix does not allocate") {
	Ref<AudioStreamPlaybackSymphonyMusic> pb = _playback(48000.0f, 120.0f);
	Array ev;
	ev.push_back(_note("note_on", 0, 1, 480));
	Dictionary gain;
	gain["kind"] = "track_gain";
	gain["tick"] = 960;
	gain["track"] = 0;
	gain["gain"] = 0.5;
	gain["fade_ticks"] = 480;
	ev.push_back(gain);
	Dictionary tempo;
	tempo["kind"] = "tempo";
	tempo["tick"] = 1920;
	tempo["bpm"] = 140.0;
	tempo["ramp_ticks"] = 960;
	ev.push_back(tempo);
	REQUIRE(pb->submit_phrase(_phrase(0, 1, ev)) == OK);
	AudioFrame chunk[128];
	REQUIRE(pb->mix_graph_rate(chunk, 128) == 128);
	const int steady = 48000 * 5;
	int done = 0;
	while (done < steady) {
		SYMPHONY_CHECK_NO_ALLOC(pb->mix_graph_rate(chunk, 128));
		done += 128;
	}
	pb->stop();
}

} // namespace TestSymphonyMusicBoundary
