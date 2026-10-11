/**************************************************************************/
/*  test_symphony_music_stream.cpp                                        */
/*  Suite: [Symphony][MusicStream] — AudioStreamSymphonyMusic playback.   */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_music_stream)

#include "modules/symphony/core/symphony_graph_description.h"
#include "modules/symphony/core/symphony_rate_policy.h"
#include "modules/symphony/core/symphony_voice_manager.h"
#include "modules/symphony/runtime/sound_event.h"
#include "modules/symphony/stream/audio_stream_symphony.h"
#include "modules/symphony/stream/audio_stream_symphony_music.h"
#include "modules/symphony/stream/audio_stream_playback_symphony_music.h"
#include "tests/modules/symphony_test_helpers.h"

#include "core/math/math_funcs.h"
#include "servers/audio/audio_frame.h"

namespace TestSymphonyMusicStream {

static GraphDescription _make_instrument() {
	GraphDescription desc;
	desc.smooth_parameters = false;
	NodeDesc note;
	note.id = 1;
	note.type_name = "MusicNoteInput";
	note.params["root_hz"] = 440.0f;
	desc.nodes.push_back(note);
	NodeDesc osc;
	osc.id = 2;
	osc.type_name = "Oscillator";
	osc.params["waveform"] = 0.0f;
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
	auto conn = [&](int a, int ap, int b, int bp) {
		ConnectionDesc c;
		c.from_node = a;
		c.from_pin = ap;
		c.to_node = b;
		c.to_pin = bp;
		desc.connections.push_back(c);
	};
	conn(1, 0, 2, 0);
	conn(2, 0, 3, 0);
	conn(1, 6, 3, 1);
	conn(3, 0, 4, 0);
	conn(4, 0, 5, 0);
	return desc;
}

static Ref<AudioStreamSymphony> _make_instrument_stream() {
	Ref<AudioStreamSymphony> s;
	s.instantiate();
	s->set_graph_description(_make_instrument());
	s->set_mix_rate(symphony_offline_rate());
	return s;
}

TEST_CASE("[Symphony][MusicStream] Phrase submit mixes notes and reports VITAL") {
	Ref<AudioStreamSymphonyMusic> music;
	music.instantiate();
	music->set_mix_rate(symphony_offline_rate());
	music->set_initial_bpm(120.0f);
	REQUIRE(music->set_track_instrument(0, _make_instrument_stream(), 4, true) == OK);
	REQUIRE(music->set_track_instrument(1, _make_instrument_stream(), 4, false) == OK);

	Ref<AudioStreamPlayback> pb_base = music->instantiate_playback();
	Ref<AudioStreamPlaybackSymphonyMusic> pb = pb_base;
	REQUIRE(pb.is_valid());

	pb->start();
	CHECK(pb->is_playing());
	CHECK(pb->get_voice_tier() == (int)SoundEvent::TIER_VITAL);

	Dictionary phrase;
	phrase["start_tick"] = 0;
	phrase["generation"] = 1;
	Array events;
	{
		Dictionary on;
		on["kind"] = "note_on";
		on["tick"] = 0;
		on["track"] = 0;
		on["note_id"] = 1;
		on["midi"] = 48;
		on["velocity"] = 0.9;
		on["duration_ticks"] = 480;
		events.push_back(on);
	}
	{
		Dictionary on;
		on["kind"] = "note_on";
		on["tick"] = 240;
		on["track"] = 1;
		on["note_id"] = 2;
		on["midi"] = 72;
		on["velocity"] = 0.7;
		on["duration_ticks"] = 480;
		events.push_back(on);
	}
	phrase["events"] = events;
	REQUIRE(pb->submit_phrase(phrase) == OK);

	const int total = (int)(symphony_offline_rate() * 0.55f);
	Vector<AudioFrame> buf;
	buf.resize(total);
	int written = 0;
	while (written < total) {
		const int n = MIN(512, total - written);
		const int got = pb->mix_graph_rate(buf.ptrw() + written, n);
		REQUIRE(got == n);
		written += n;
	}

	float peak = 0.0f;
	for (int i = 0; i < total; i++) {
		peak = MAX(peak, MAX(Math::abs(buf[i].left), Math::abs(buf[i].right)));
		CHECK(Math::is_finite(buf[i].left));
	}
	CHECK(peak > 0.01f);

	Dictionary snap = pb->get_transport_snapshot();
	CHECK((int64_t)snap["frame"] > 0);
	CHECK((float)snap["bpm"] == doctest::Approx(120.0f));

	Array status = pb->drain_status();
	bool saw_phrase = false;
	for (int i = 0; i < status.size(); i++) {
		const Dictionary d = status[i];
		if (String(d.get("kind", "")) == "phrase_activated") {
			saw_phrase = true;
		}
	}
	CHECK(saw_phrase);

	pb->stop();
	CHECK_FALSE(pb->is_playing());
}

TEST_CASE("[Symphony][MusicStream] Clip phrase + tempo schedule") {
	Ref<AudioStreamSymphonyMusic> music;
	music.instantiate();
	music->set_mix_rate(symphony_offline_rate());
	music->set_initial_bpm(120.0f);
	REQUIRE(music->set_track_instrument(0, _make_instrument_stream(), 2, false) == OK);

	const float rate = symphony_offline_rate();
	const int clip_frames = (int)(rate * 0.4f);
	PackedFloat32Array pcm;
	pcm.resize(clip_frames * 2);
	float *pcm_w = pcm.ptrw();
	for (int i = 0; i < clip_frames; i++) {
		const float s = Math::sin(Math::TAU * 330.0f * (float)i / rate) * 0.4f;
		pcm_w[i * 2] = s;
		pcm_w[i * 2 + 1] = s;
	}
	REQUIRE(music->register_clip_pcm("stream_clip", pcm, rate, 120.0f) == OK);
	CHECK(music->find_clip_index("stream_clip") == 0);

	Ref<AudioStreamPlaybackSymphonyMusic> pb = music->instantiate_playback();
	REQUIRE(pb.is_valid());
	pb->start();

	Dictionary phrase;
	phrase["start_tick"] = 0;
	phrase["generation"] = 2;
	Array events;
	{
		Dictionary clip;
		clip["kind"] = "clip_on";
		clip["tick"] = 0;
		clip["track"] = 0;
		clip["note_id"] = 10;
		clip["clip_key"] = StringName("stream_clip");
		clip["transpose"] = 0;
		clip["tempo_ratio"] = 1.0;
		events.push_back(clip);
	}
	{
		Dictionary on;
		on["kind"] = "note_on";
		on["tick"] = 0;
		on["track"] = 0;
		on["note_id"] = 1;
		on["midi"] = 69;
		on["velocity"] = 0.6;
		on["duration_ticks"] = 480;
		events.push_back(on);
	}
	phrase["events"] = events;
	REQUIRE(pb->submit_phrase(phrase) == OK);
	REQUIRE(pb->schedule_tempo(140.0f, 960, 0) == OK);

	const int total = (int)(rate * 0.5f);
	Vector<AudioFrame> buf;
	buf.resize(total);
	int written = 0;
	while (written < total) {
		const int n = MIN(512, total - written);
		REQUIRE(pb->mix_graph_rate(buf.ptrw() + written, n) == n);
		written += n;
	}
	float peak = 0.0f;
	for (int i = 0; i < total; i++) {
		peak = MAX(peak, Math::abs(buf[i].left));
	}
	CHECK(peak > 1e-4f);

	pb->stop();
}

TEST_CASE("[Symphony][MusicStream] replace_future clears pending phrase") {
	Ref<AudioStreamSymphonyMusic> music;
	music.instantiate();
	music->set_mix_rate(symphony_offline_rate());
	REQUIRE(music->set_track_instrument(0, _make_instrument_stream(), 2, false) == OK);
	Ref<AudioStreamPlaybackSymphonyMusic> pb = music->instantiate_playback();
	pb->start();

	Dictionary phrase;
	phrase["start_tick"] = 960; // future bar
	phrase["generation"] = 3;
	Array events;
	Dictionary on;
	on["kind"] = "note_on";
	on["tick"] = 960;
	on["track"] = 0;
	on["note_id"] = 1;
	on["midi"] = 60;
	on["duration_ticks"] = 240;
	events.push_back(on);
	phrase["events"] = events;
	REQUIRE(pb->submit_phrase(phrase) == OK);
	REQUIRE(pb->replace_future(960, 3) == OK);

	// Mix a bit — no audible note at t=0.
	Vector<AudioFrame> buf;
	buf.resize(1024);
	REQUIRE(pb->mix_graph_rate(buf.ptrw(), 1024) == 1024);
	float peak = 0.0f;
	for (int i = 0; i < 1024; i++) {
		peak = MAX(peak, Math::abs(buf[i].left));
	}
	CHECK(peak < 1e-4f);
	pb->stop();
}

TEST_CASE("[Symphony][B7] render_offline duration, repeatability, and player refusal") {
	Ref<AudioStreamSymphonyMusic> music;
	music.instantiate();
	music->set_mix_rate(symphony_offline_rate());
	music->set_initial_bpm(120.0f);
	REQUIRE(music->set_track_instrument(0, _make_instrument_stream(), 4, true) == OK);

	Dictionary phrase;
	phrase["start_tick"] = 0;
	phrase["generation"] = 1;
	Array events;
	Dictionary on;
	on["kind"] = "note_on";
	on["tick"] = 0;
	on["track"] = 0;
	on["note_id"] = 1;
	on["midi"] = 60;
	on["velocity"] = 0.9;
	on["duration_ticks"] = 960;
	events.push_back(on);
	phrase["events"] = events;

	const int frames = (int)symphony_offline_rate(); // 1 s
	PackedVector2Array a;
	PackedVector2Array b;
	{
		Ref<AudioStreamPlaybackSymphonyMusic> pb = music->instantiate_playback();
		REQUIRE(pb.is_valid());
		REQUIRE_FALSE(pb->is_registered_with_voice_manager());
		REQUIRE(pb->submit_phrase(phrase) == OK);
		a = pb->render_offline(frames);
	}
	{
		Ref<AudioStreamPlaybackSymphonyMusic> pb = music->instantiate_playback();
		REQUIRE(pb->submit_phrase(phrase) == OK);
		b = pb->render_offline(frames);
	}
	REQUIRE(a.size() == frames);
	REQUIRE(b.size() == frames);
	CHECK(a == b);
	float peak = 0.0f;
	for (int i = 0; i < frames; i++) {
		peak = MAX(peak, MAX(Math::abs(a[i].x), Math::abs(a[i].y)));
		CHECK(Math::is_finite(a[i].x));
		CHECK(Math::is_finite(a[i].y));
	}
	CHECK(peak > 0.01f);

	Ref<AudioStreamPlaybackSymphonyMusic> live = music->instantiate_playback();
	live->start();
	CHECK(live->is_registered_with_voice_manager());
	PackedVector2Array refused = live->render_offline(64);
	CHECK(refused.is_empty());
	live->stop();
}

} // namespace TestSymphonyMusicStream
