/**************************************************************************/
/*  test_symphony_profile.cpp                                             */
/*  Suite: [Symphony][Profile] — release callback P50/P95/P99 harness.    */
/*                                                                        */
/*  Default: ~3 s smoke (always). Full 30 min:                             */
/*    SYMPHONY_PROFILE_SECONDS=1800 bin/godot… --test \                    */
/*      --test-case='*Symphony*Profile*30*'                                */
/*  Prefer template_release for gate numbers.                              */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_profile)

#include "modules/symphony/core/symphony_voice_manager.h"
#include "modules/symphony/stream/audio_stream_playback_symphony.h"
#include "modules/symphony/stream/audio_stream_symphony.h"
#include "tests/modules/symphony_test_helpers.h"

#include "core/os/os.h"
#include "core/string/ustring.h"
#include "servers/audio/audio_driver_dummy.h"

namespace TestSymphonyProfile {

using SymphonyTestHelpers::make_sine_graph;

static int _profile_seconds() {
	const String env = OS::get_singleton()->get_environment("SYMPHONY_PROFILE_SECONDS");
	if (env.is_empty()) {
		return 3;
	}
	return MAX(1, env.to_int());
}

static void _percentile(Vector<uint64_t> &p_samples, uint64_t &r_p50, uint64_t &r_p95, uint64_t &r_p99) {
	REQUIRE(p_samples.size() > 0);
	p_samples.sort();
	const int n = p_samples.size();
	r_p50 = p_samples[(n - 1) / 2];
	r_p95 = p_samples[CLAMP((int)((n - 1) * 0.95), 0, n - 1)];
	r_p99 = p_samples[CLAMP((int)((n - 1) * 0.99), 0, n - 1)];
}

static void _run_callback_profile(float p_mix_rate, int p_callback_frames, int p_seconds, bool p_require_gate) {
	AudioDriverDummy *dummy = AudioDriverDummy::get_dummy_singleton();
	REQUIRE(dummy != nullptr);
	struct Restore {
		AudioDriverDummy *d;
		int prev;
		~Restore() {
			if (d) {
				d->set_mix_rate(prev);
			}
		}
	} restore{ dummy, dummy->get_mix_rate() };
	dummy->set_mix_rate((int)p_mix_rate);

	// Demonstration-ish workload: 8 concurrent SFX voices + one music stream if available.
	static constexpr int VOICES = 8;
	Ref<AudioStreamSymphony> streams[VOICES];
	Ref<AudioStreamPlayback> bases[VOICES];
	Ref<AudioStreamPlaybackSymphony> voices[VOICES];
	for (int i = 0; i < VOICES; i++) {
		streams[i].instantiate();
		streams[i]->set_mix_rate(p_mix_rate);
		streams[i]->set_graph_description(make_sine_graph(110.0f * float(i + 1)));
		if (i % 2 == 0) {
			streams[i]->set_lod_variant(1, make_sine_graph(220.0f));
		}
		bases[i] = streams[i]->instantiate_playback();
		voices[i] = bases[i];
		REQUIRE(voices[i].is_valid());
		voices[i]->start();
	}

	const uint64_t deadline_us = (uint64_t)((double)p_callback_frames / (double)p_mix_rate * 1e6);
	const uint64_t end_us = OS::get_singleton()->get_ticks_usec() + uint64_t(p_seconds) * 1'000'000ull;
	Vector<uint64_t> samples;
	uint64_t misses = 0;
	AudioFrame *chunk = (AudioFrame *)memalloc(sizeof(AudioFrame) * p_callback_frames);
	REQUIRE(chunk != nullptr);

	while (OS::get_singleton()->get_ticks_usec() < end_us) {
		const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
		for (int i = 0; i < VOICES; i++) {
			if (voices[i].is_valid() && voices[i]->is_playing()) {
				voices[i]->mix(chunk, 1.0f, p_callback_frames);
			}
		}
		const uint64_t dt = OS::get_singleton()->get_ticks_usec() - t0;
		samples.push_back(dt);
		if (dt > deadline_us) {
			misses++;
		}
	}
	memfree(chunk);

	uint64_t p50 = 0, p95 = 0, p99 = 0;
	_percentile(samples, p50, p95, p99);
	MESSAGE("profile rate=", p_mix_rate, " cb=", p_callback_frames,
			" s=", p_seconds, " n=", samples.size(),
			" p50_us=", p50, " p95_us=", p95, " p99_us=", p99,
			" deadline_us=", deadline_us, " misses=", misses);

	for (int i = 0; i < VOICES; i++) {
		if (voices[i].is_valid()) {
			voices[i]->stop();
		}
		voices[i].unref();
		bases[i].unref();
		streams[i].unref();
	}
	if (SymphonyVoiceManager::get_singleton()) {
		SymphonyVoiceManager::get_singleton()->process_deferred_lod();
	}

	// Smoke always asserts we collected samples. Full gate only when requested.
	CHECK(samples.size() > 10);
	if (p_require_gate) {
		CHECK(p99 < deadline_us / 2); // < 50% of a callback at P99
		CHECK(misses == 0);
	}
}

TEST_CASE("[Symphony][Profile] smoke callback percentiles at 48 kHz / 512") {
	_run_callback_profile(48000.0f, 512, MIN(3, _profile_seconds()), false);
}

TEST_CASE("[Symphony][Profile] smoke callback percentiles at 44.1 kHz / 512") {
	_run_callback_profile(44100.0f, 512, MIN(3, _profile_seconds()), false);
}

TEST_CASE("[Symphony][Profile][30min] full release gate when SYMPHONY_PROFILE_SECONDS set") {
	const int seconds = _profile_seconds();
	if (seconds < 60) {
		MESSAGE("Skipping 30-min gate (set SYMPHONY_PROFILE_SECONDS=1800 for full run).");
		return;
	}
	_run_callback_profile(48000.0f, 512, seconds, true);
	_run_callback_profile(44100.0f, 512, MIN(seconds, 120), true); // second rate: cap at 2 min unless env already short
}

} // namespace TestSymphonyProfile
