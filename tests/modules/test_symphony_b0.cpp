/**************************************************************************/
/*  test_symphony_b0.cpp                                                  */
/*  Suite: [Symphony][B0] — alloc counter, helpers, mixer scaffold.       */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_b0)

#include "modules/symphony/core/symphony_rate_policy.h"
#include "modules/symphony/stream/audio_stream_playback_symphony_music.h"
#include "modules/symphony/stream/audio_stream_symphony.h"
#include "modules/symphony/stream/audio_stream_symphony_music.h"
#include "tests/modules/symphony_test_helpers.h"

#include "core/os/memory.h"
#include "core/os/os.h"
#include "core/os/thread.h"

#include <atomic>

using namespace SymphonyTestHelpers;

namespace TestSymphonyB0 {

TEST_CASE("[Symphony][B0] Thread alloc counter and SYMPHONY_CHECK_NO_ALLOC") {
	const uint64_t before = Memory::get_thread_alloc_count();
	Vector<int> grown;
	grown.resize(4096);
	const uint64_t after = Memory::get_thread_alloc_count();
#ifdef DEBUG_ENABLED
	CHECK(after > before);
#else
	CHECK(after == 0);
	(void)before;
#endif
	SYMPHONY_CHECK_NO_ALLOC({
		const int x = 1 + 2;
		(void)x;
	});
	CHECK(first_onset_frame(Vector<AudioFrame>(), 0.1f, 0) == -1);
}

TEST_CASE("[Symphony][B0] Mixer-thread scaffold runs against a music playback") {
	Ref<AudioStreamSymphony> instrument;
	instrument.instantiate();
	instrument->set_mix_rate(symphony_offline_rate());
	instrument->set_graph_description(make_click_instrument());

	Ref<AudioStreamSymphonyMusic> music;
	music.instantiate();
	music->set_mix_rate(symphony_offline_rate());
	music->set_initial_bpm(120.0f);
	REQUIRE(music->set_track_instrument(0, instrument, 2, false) == OK);

	Ref<AudioStreamPlaybackSymphonyMusic> pb = music->instantiate_playback();
	REQUIRE(pb.is_valid());
	pb->start();

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

	for (int i = 0; i < 20; i++) {
		pb->drain_status();
		CHECK(pb->_test_transport_segment_count() >= 1);
		CHECK(pb->_test_free_stretch_contexts() == SymphonyStretchPool::MAX_CONTEXTS);
		OS::get_singleton()->delay_usec(1000);
	}
	state.stop.store(true, std::memory_order_release);
	mixer.wait_to_finish();
	pb->stop();
}

} // namespace TestSymphonyB0
