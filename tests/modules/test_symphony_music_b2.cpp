/**************************************************************************/
/*  test_symphony_music_b2.cpp                                            */
/*  Suite: [Symphony][B2][Music] — command ring, generation, restart.     */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_music_b2)

#include "modules/symphony/core/symphony_rate_policy.h"
#include "modules/symphony/stream/audio_stream_playback_symphony_music.h"
#include "modules/symphony/stream/audio_stream_symphony.h"
#include "modules/symphony/stream/audio_stream_symphony_music.h"
#include "tests/modules/symphony_test_helpers.h"

#include "core/os/os.h"
#include "core/os/thread.h"

#include <atomic>

using namespace SymphonyTestHelpers;

namespace TestSymphonyMusicB2 {

static Ref<AudioStreamPlaybackSymphonyMusic> _playback() {
	const float rate = symphony_offline_rate();
	Ref<AudioStreamSymphony> instrument;
	instrument.instantiate();
	instrument->set_mix_rate(rate);
	instrument->set_graph_description(make_click_instrument());
	Ref<AudioStreamSymphonyMusic> music;
	music.instantiate();
	music->set_mix_rate(rate);
	music->set_initial_bpm(120.0f);
	REQUIRE(music->set_track_instrument(0, instrument, 4, true) == OK);
	Ref<AudioStreamPlaybackSymphonyMusic> pb = music->instantiate_playback();
	REQUIRE(pb.is_valid());
	pb->start();
	return pb;
}

static Dictionary _phrase(int64_t p_start, int p_gen, int64_t p_tick, int p_note, int p_dur) {
	Dictionary phrase;
	phrase["start_tick"] = p_start;
	phrase["generation"] = p_gen;
	Array events;
	Dictionary on;
	on["kind"] = "note_on";
	on["tick"] = p_tick;
	on["track"] = 0;
	on["note_id"] = p_note;
	on["midi"] = 60;
	on["velocity"] = 1.0;
	on["duration_ticks"] = p_dur;
	events.push_back(on);
	phrase["events"] = events;
	return phrase;
}

TEST_CASE("[Symphony][B2] Full command ring and phrase slots return ERR_BUSY") {
	Ref<AudioStreamPlaybackSymphonyMusic> pb = _playback();
	int busy_slots = 0;
	for (int i = 0; i < 8; i++) {
		const Error err = pb->submit_phrase(_phrase(i * 3840, i + 1, i * 3840, i + 1, 240));
		if (err == ERR_BUSY) {
			busy_slots++;
		} else {
			CHECK(err == OK);
		}
	}
	CHECK(busy_slots >= 1);

	int busy_cmds = 0;
	int accepted = 0;
	for (int i = 0; i < 80; i++) {
		const Error err = pb->schedule_tempo(100.0f + (float)(i % 10), 960 * (i + 2), 0);
		if (err == ERR_BUSY) {
			busy_cmds++;
		} else {
			accepted++;
		}
	}
	CHECK(busy_cmds >= 1);
	CHECK(accepted <= 64);
	pb->stop();
}

TEST_CASE("[Symphony][B2] replace_future with an older generation keeps the newer phrase") {
	Ref<AudioStreamPlaybackSymphonyMusic> pb = _playback();
	REQUIRE(pb->submit_phrase(_phrase(0, 1, 0, 1, 960)) == OK);
	REQUIRE(pb->submit_phrase(_phrase(1920, 3, 1920, 2, 480)) == OK);
	REQUIRE(pb->replace_future(960, 1) == OK);
	AudioFrame chunk[256];
	for (int i = 0; i < 200; i++) {
		REQUIRE(pb->mix_graph_rate(chunk, 256) == 256);
	}
	Array status = pb->drain_status();
	bool saw_gen3 = false;
	for (int i = 0; i < status.size(); i++) {
		const Dictionary d = status[i];
		if (String(d.get("kind", "")) == "phrase_activated" && (int)d.get("generation", 0) == 3) {
			saw_gen3 = true;
		}
	}
	CHECK(saw_gen3);
	pb->stop();
}

TEST_CASE("[Symphony][B2] Restart mid-playback silences old voices") {
	Ref<AudioStreamPlaybackSymphonyMusic> pb = _playback();
	REQUIRE(pb->submit_phrase(_phrase(0, 1, 0, 9, 3840)) == OK);
	AudioFrame chunk[128];
	REQUIRE(pb->mix_graph_rate(chunk, 128) == 128);
	CHECK(pb->_test_active_voice_count() >= 1);
	pb->start();
	REQUIRE(pb->mix_graph_rate(chunk, 128) == 128);
	float peak = 0.0f;
	for (int i = 0; i < 128; i++) {
		peak = MAX(peak, Math::abs(chunk[i].left));
	}
	CHECK(peak < 1e-4f);
	CHECK(pb->_test_active_voice_count() == 0);
	pb->stop();
}

TEST_CASE("[Symphony][Music][Stress] Mixer thread against main submit/tempo/gain/replace") {
	Ref<AudioStreamPlaybackSymphonyMusic> pb = _playback();

	struct MixState {
		AudioStreamPlaybackSymphonyMusic *voice = nullptr;
		std::atomic<bool> stop{ false };
	} state;
	state.voice = pb.ptr();

	Thread mixer;
	mixer.start(
			[](void *p) {
				MixState *s = static_cast<MixState *>(p);
				AudioFrame chunk[64];
				while (!s->stop.load(std::memory_order_acquire)) {
					if (s->voice && s->voice->is_playing()) {
						s->voice->mix_graph_rate(chunk, 64);
					}
				}
			},
			&state);

	const uint64_t start_usec = OS::get_singleton()->get_ticks_usec();
	int iter = 0;
	while (OS::get_singleton()->get_ticks_usec() - start_usec < 3000000) {
		(void)pb->submit_phrase(_phrase(iter * 960, iter + 1, iter * 960, (iter % 7) + 1, 240));
		(void)pb->schedule_tempo(90.0f + (float)(iter % 20), (iter + 2) * 960, 0);
		(void)pb->schedule_track_gain(0, iter % 2 ? 0.5f : 1.0f, iter * 480, 120);
		if (iter % 3 == 0) {
			(void)pb->replace_future(iter * 480, iter);
		}
		pb->drain_status();
		iter++;
		OS::get_singleton()->delay_usec(1000);
	}
	state.stop.store(true, std::memory_order_release);
	mixer.wait_to_finish();
	CHECK(iter > 10);
	pb->stop();
}

} // namespace TestSymphonyMusicB2
