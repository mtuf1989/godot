/**************************************************************************/
/*  test_symphony_music_v1.cpp                                            */
/*  Suite: [Symphony][V1] — qualification timing + phrase replacement.    */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_music_v1)

#include "modules/symphony/core/symphony_graph_description.h"
#include "modules/symphony/core/symphony_rate_policy.h"
#include "modules/symphony/runtime/sound_event.h"
#include "modules/symphony/runtime/symphony_note_pool.h"
#include "modules/symphony/stream/audio_stream_symphony.h"
#include "modules/symphony/stream/audio_stream_symphony_music.h"
#include "modules/symphony/stream/audio_stream_playback_symphony_music.h"
#include "tests/modules/symphony_test_helpers.h"

#include "core/math/math_funcs.h"
#include "servers/audio/audio_frame.h"

namespace TestSymphonyMusicV1 {

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
	adsr.params["attack"] = 0.0f;
	adsr.params["decay"] = 0.01f;
	adsr.params["sustain"] = 1.0f;
	adsr.params["release"] = 0.05f;
	desc.nodes.push_back(adsr);
	NodeDesc gain;
	gain.id = 4;
	gain.type_name = "Gain";
	gain.params["gain"] = 0.5f;
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

static Ref<AudioStreamSymphony> _instrument_stream() {
	Ref<AudioStreamSymphony> s;
	s.instantiate();
	s->set_graph_description(_make_instrument());
	s->set_mix_rate(symphony_offline_rate());
	return s;
}

static int _first_energy_sample(const AudioFrame *buf, int n, float thresh = 1e-4f) {
	for (int i = 0; i < n; i++) {
		if (Math::abs(buf[i].left) > thresh || Math::abs(buf[i].right) > thresh) {
			return i;
		}
	}
	return -1;
}

TEST_CASE("[Symphony][V1] Note-on sample offsets 0/1/31/32/63 within micro-block") {
	const float rate = symphony_offline_rate();
	const int offsets[] = { 0, 1, 31, 32, 63 };
	for (int oi = 0; oi < 5; oi++) {
		const int want = offsets[oi];
		if (want >= SYMPHONY_MICRO_BLOCK_SIZE) {
			// Platform micro-block is smaller (e.g. web=32); skip out-of-range offsets.
			continue;
		}
		SymphonyNotePool pool;
		REQUIRE(pool.prepare(_make_instrument(), rate, 2, 0x51ULL));
		REQUIRE(pool.note_on(1, 69, 1.0f, 0, want) >= 0);

		Vector<AudioFrame> buf;
		buf.resize(SYMPHONY_MICRO_BLOCK_SIZE);
		for (int i = 0; i < buf.size(); i++) {
			buf.write[i] = AudioFrame(0, 0);
		}
		pool.render(buf.ptrw(), SYMPHONY_MICRO_BLOCK_SIZE, 0);
		const int first = _first_energy_sample(buf.ptr(), buf.size());
		REQUIRE(first >= 0);
		CHECK(Math::abs(first - want) <= 1);
	}
}

TEST_CASE("[Symphony][V1] replace_future keeps active release; no stale note-off steal") {
	const float rate = symphony_offline_rate();
	Ref<AudioStreamSymphonyMusic> music;
	music.instantiate();
	music->set_mix_rate(rate);
	music->set_initial_bpm(120.0f);
	REQUIRE(music->set_track_instrument(0, _instrument_stream(), 4, true) == OK);

	Ref<AudioStreamPlaybackSymphonyMusic> pb = music->instantiate_playback();
	pb->start();

	Dictionary phrase;
	phrase["start_tick"] = 0;
	phrase["generation"] = 1;
	Array events;
	Dictionary on;
	on["kind"] = "note_on";
	on["tick"] = 0;
	on["track"] = 0;
	on["note_id"] = 7;
	on["midi"] = 48;
	on["velocity"] = 0.9;
	on["duration_ticks"] = 960;
	events.push_back(on);
	phrase["events"] = events;
	REQUIRE(pb->submit_phrase(phrase) == OK);

	Vector<AudioFrame> buf;
	buf.resize(1024);
	REQUIRE(pb->mix_graph_rate(buf.ptrw(), 1024) == 1024);
	float peak_before = 0.0f;
	for (int i = 0; i < 1024; i++) {
		peak_before = MAX(peak_before, Math::abs(buf[i].left));
	}
	CHECK(peak_before > 0.01f);

	REQUIRE(pb->replace_future(480, 2) == OK);

	Dictionary next;
	next["start_tick"] = 1920;
	next["generation"] = 2;
	Array next_ev;
	Dictionary on2;
	on2["kind"] = "note_on";
	on2["tick"] = 1920;
	on2["track"] = 0;
	on2["note_id"] = 7;
	on2["midi"] = 60;
	on2["duration_ticks"] = 240;
	next_ev.push_back(on2);
	next["events"] = next_ev;
	REQUIRE(pb->submit_phrase(next) == OK);

	REQUIRE(pb->mix_graph_rate(buf.ptrw(), 1024) == 1024);
	float peak_after = 0.0f;
	for (int i = 0; i < 1024; i++) {
		peak_after = MAX(peak_after, Math::abs(buf[i].left));
		CHECK(Math::is_finite(buf[i].left));
	}
	CHECK(peak_after > 1e-4f);
	CHECK(pb->get_voice_tier() == (int)SoundEvent::TIER_VITAL);
	pb->stop();
}

TEST_CASE("[Symphony][V1] Callback size 32 vs 64 preserves coarse energy envelope") {
	const float rate = symphony_offline_rate();
	auto render = [&](int chunk) -> Vector<float> {
		Ref<AudioStreamSymphonyMusic> music;
		music.instantiate();
		music->set_mix_rate(rate);
		music->set_initial_bpm(120.0f);
		REQUIRE(music->set_track_instrument(0, _instrument_stream(), 2, false) == OK);
		Ref<AudioStreamPlaybackSymphonyMusic> pb = music->instantiate_playback();
		pb->start();
		Dictionary phrase;
		phrase["start_tick"] = 0;
		phrase["generation"] = 1;
		Array events;
		for (int i = 0; i < 4; i++) {
			Dictionary on;
			on["kind"] = "note_on";
			on["tick"] = i * 240;
			on["track"] = 0;
			on["note_id"] = i + 1;
			on["midi"] = 60 + i;
			on["velocity"] = 0.8;
			on["duration_ticks"] = 180;
			events.push_back(on);
		}
		phrase["events"] = events;
		REQUIRE(pb->submit_phrase(phrase) == OK);
		const int total = (int)(rate * 0.5f);
		Vector<float> mono;
		mono.resize(total);
		Vector<AudioFrame> buf;
		buf.resize(chunk);
		int written = 0;
		while (written < total) {
			const int n = MIN(chunk, total - written);
			REQUIRE(pb->mix_graph_rate(buf.ptrw(), n) == n);
			for (int i = 0; i < n; i++) {
				mono.write[written + i] = buf[i].left;
			}
			written += n;
		}
		pb->stop();
		return mono;
	};

	Vector<float> a = render(32);
	Vector<float> b = render(64);
	REQUIRE(a.size() == b.size());
	// Compare RMS in 8 equal windows — same symbolic schedule, allow block-split drift.
	const int windows = 8;
	const int win = a.size() / windows;
	int energetic = 0;
	for (int w = 0; w < windows; w++) {
		double ea = 0.0;
		double eb = 0.0;
		for (int i = 0; i < win; i++) {
			const int idx = w * win + i;
			ea += (double)a[idx] * (double)a[idx];
			eb += (double)b[idx] * (double)b[idx];
		}
		ea = Math::sqrt(ea / (double)win);
		eb = Math::sqrt(eb / (double)win);
		if (ea > 0.01 || eb > 0.01) {
			energetic++;
			const double denom = MAX(MAX(ea, eb), 1e-9);
			CHECK(Math::abs(ea - eb) / denom < 0.5);
		}
	}
	CHECK(energetic >= 2);
}

} // namespace TestSymphonyMusicV1
