/**************************************************************************/
/*  test_symphony_music_n1.cpp                                            */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_music_n1)

#include "modules/symphony/core/symphony_graph_compiler.h"
#include "modules/symphony/core/symphony_graph_description.h"
#include "modules/symphony/core/symphony_prepared_graph_package.h"
#include "modules/symphony/nodes/envelopes/symphony_adsr.h"
#include "modules/symphony/runtime/beat_clock.h"
#include "modules/symphony/runtime/symphony_music_scheduler.h"
#include "modules/symphony/runtime/symphony_music_transport.h"
#include "modules/symphony/stream/audio_stream_playback_symphony.h"
#include "modules/symphony/stream/audio_stream_symphony.h"
#include "servers/audio/audio_server.h"
#include "tests/modules/symphony_test_helpers.h"

#include <cmath>

using SymphonyTestHelpers::execute_mix_frames;
using SymphonyTestHelpers::rms;

namespace TestSymphonyMusicN1 {

TEST_CASE("[Symphony][N1][Transport] Constant BPM tick↔frame is exact at 120/48k") {
	SymphonyMusicTransport t;
	t.reset(48000.0f, 120.0f);
	// 1 quarter at 120 BPM = 0.5 s = 24000 frames; 960 ticks.
	CHECK(t.tick_to_frame(SymphonyMusicTransport::PPQN) == 24000);
	CHECK(t.frame_to_tick(24000) == SymphonyMusicTransport::PPQN);
	CHECK(t.tick_to_frame(0) == 0);
	CHECK(Math::abs(t.bpm_at_tick(0) - 120.0f) < 1e-4f);
}

TEST_CASE("[Symphony][N1][Transport] Tempo step does not rescale history") {
	SymphonyMusicTransport t;
	t.reset(48000.0f, 120.0f);
	const uint64_t at_960 = t.tick_to_frame(960);
	REQUIRE(t.schedule_tempo_step(240.0f, 960));
	// Past mapping unchanged.
	CHECK(t.tick_to_frame(960) == at_960);
	// Next quarter at 240 BPM = 0.25 s = 12000 frames.
	CHECK(t.tick_to_frame(1920) == at_960 + 12000);
}

TEST_CASE("[Symphony][N1][Transport] Tempo ramp has no cumulative drift beyond 1 sample") {
	SymphonyMusicTransport t;
	t.reset(48000.0f, 120.0f);
	// Ramp 120→180 over 1 bar (4 quarters = 3840 ticks).
	REQUIRE(t.schedule_tempo_ramp(180.0f, 0, 3840));
	const uint64_t end = t.tick_to_frame(3840);
	// Analytic: (60/960)*∫_0^3840 du/bpm(u), bpm=120+60*u/3840
	// = (60/960)*(3840/60)*ln(180/120) = 4*ln(1.5) seconds
	const double expect_s = 4.0 * Math::log(1.5);
	const uint64_t expect_f = (uint64_t)Math::floor(expect_s * 48000.0 + 1e-9);
	CHECK(Math::abs((int64_t)end - (int64_t)expect_f) <= 1);
	// Round-trip: frame→tick→frame within 1 sample.
	const int64_t tick = t.frame_to_tick(end);
	const uint64_t back = t.tick_to_frame(tick);
	CHECK(Math::abs((int64_t)back - (int64_t)end) <= 1);
}

TEST_CASE("[Symphony][N1][Scheduler] Events fire at exact frames including micro-block edges") {
	SymphonyMusicScheduler sched;
	sched.reset(48000.0f, 120.0f);
	// At 120 BPM / 48k: frame = tick * 24000/960 = tick * 25.
	SymphonyMusicPhrase phrase;
	phrase.start_tick = 0;
	phrase.generation = 1;
	const int64_t ticks[] = { 0, 1, 31, 32, 63 };
	for (int64_t tk : ticks) {
		SymphonyMusicEvent ev;
		ev.tick = tk;
		ev.kind = SymphonyMusicEventKind::NoteOn;
		ev.note_id = (int32_t)tk;
		REQUIRE(phrase.push(ev));
	}
	REQUIRE(sched.submit_phrase(phrase));
	Vector<uint64_t> fired;
	sched.advance(64 * 25, [&](const SymphonyMusicEvent &ev, uint64_t frame) {
		fired.push_back(frame);
		CHECK(frame == (uint64_t)ev.tick * 25);
	});
	CHECK(fired.size() == 5);
	CHECK(fired[0] == 0);
	CHECK(fired[1] == 25);
	CHECK(fired[2] == 775);
	CHECK(fired[3] == 800);
	CHECK(fired[4] == 1575);
}

TEST_CASE("[Symphony][N1][Scheduler] NoteOff before NoteOn at equal timestamps") {
	SymphonyMusicPhrase phrase;
	SymphonyMusicEvent on;
	on.tick = 100;
	on.kind = SymphonyMusicEventKind::NoteOn;
	on.note_id = 1;
	SymphonyMusicEvent off;
	off.tick = 100;
	off.kind = SymphonyMusicEventKind::NoteOff;
	off.note_id = 2;
	REQUIRE(phrase.push(on));
	REQUIRE(phrase.push(off));
	phrase.sort_stable();
	CHECK(phrase.events[0].kind == SymphonyMusicEventKind::NoteOff);
	CHECK(phrase.events[1].kind == SymphonyMusicEventKind::NoteOn);
}

TEST_CASE("[Symphony][N1][Audio][Trigger] trigger_at frame 1000 starts ADSR at sample 1000") {
	REQUIRE(AudioServer::get_singleton() != nullptr);

	GraphDescription desc;
	desc.smooth_parameters = false;
	NodeDesc tin;
	tin.id = 1;
	tin.type_name = "TriggerInput";
	tin.params["trigger_name"] = "gate";
	tin.params["auto_trigger_on_play"] = 0.0f;
	desc.nodes.push_back(tin);
	NodeDesc osc;
	osc.id = 2;
	osc.type_name = "Oscillator";
	osc.params["frequency"] = 440.0f;
	desc.nodes.push_back(osc);
	NodeDesc adsr;
	adsr.id = 3;
	adsr.type_name = "ADSR";
	adsr.params["attack"] = 0.0f;
	adsr.params["decay"] = 0.0f;
	adsr.params["sustain"] = 1.0f;
	adsr.params["release"] = 0.0f;
	desc.nodes.push_back(adsr);
	NodeDesc out;
	out.id = 4;
	out.type_name = "GraphOutput";
	desc.nodes.push_back(out);
	desc.connections.push_back({ 2, 0, 3, 0, false });
	desc.connections.push_back({ 1, 0, 3, 1, false });
	desc.connections.push_back({ 3, 0, 4, 0, false });

	Ref<AudioStreamSymphony> stream;
	stream.instantiate();
	stream->set_mix_rate(48000.0f);
	stream->set_graph_description(desc);

	Ref<AudioStreamPlayback> base = stream->instantiate_playback();
	Ref<AudioStreamPlaybackSymphony> playback = base;
	REQUIRE(playback.is_valid());
	playback->start();

	// Silence until the scheduled trigger.
	REQUIRE(playback->trigger_at(StringName("gate"), 1000, 1.0f));

	const int total = 1200;
	Vector<AudioFrame> buf;
	buf.resize(total);
	for (int i = 0; i < total; i++) {
		buf.write[i] = AudioFrame(0, 0);
	}
	playback->mix_graph_rate(buf.ptrw(), total);

	// Find first non-silent sample.
	int first = -1;
	for (int i = 0; i < total; i++) {
		if (Math::abs(buf[i].left) > 1e-4f) {
			first = i;
			break;
		}
	}
	INFO("first_non_silent=", first);
	CHECK(first >= 0);
	CHECK(Math::abs(first - 1000) <= 1);
}

TEST_CASE("[Symphony][N1][BeatClock] Looped stream continues accumulator across wrap") {
	BeatClock *clock = BeatClock::get_singleton();
	REQUIRE(clock != nullptr);
	clock->start(120.0f, 4);
	clock->set_loop_length_hint(2.0); // 4 beats at 120 BPM

	clock->update_playback_position(0.0);
	clock->process(0.016);
	clock->update_playback_position(1.9);
	clock->process(0.016);
	const float beat_before = clock->get_current_beat();
	CHECK(beat_before > 3.0f);

	clock->update_playback_position(0.01); // wrap
	clock->process(0.016);
	CHECK(clock->get_current_beat() > beat_before);
	clock->stop();
}

TEST_CASE("[Symphony][N1][BeatClock] Hitch emits multiple beats in order (capped)") {
	BeatClock *clock = BeatClock::get_singleton();
	REQUIRE(clock != nullptr);
	clock->start(180.0f, 4); // 180 BPM → beat every 1/3 s
	clock->update_playback_position(0.0);
	clock->process(0.016);

	clock->update_playback_position(0.40);
	const float before = clock->get_current_beat();
	clock->process(0.016);
	const float after = clock->get_current_beat();
	CHECK(after - before >= 1.0f);
	CHECK(after - before <= 8.5f);
	clock->stop();
}

TEST_CASE("[Symphony][N1][BeatClock] set_bpm mid-bar keeps accumulator monotonic") {
	BeatClock *clock = BeatClock::get_singleton();
	REQUIRE(clock != nullptr);
	clock->start(120.0f, 4);
	clock->update_playback_position(0.5);
	clock->process(0.016);
	const float b0 = clock->get_current_beat();
	clock->set_bpm(140.0f);
	clock->update_playback_position(0.6);
	clock->process(0.016);
	const float b1 = clock->get_current_beat();
	CHECK(b1 >= b0);
	clock->stop();
}

} // namespace TestSymphonyMusicN1
