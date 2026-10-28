/**************************************************************************/
/*  test_symphony_web_publication.cpp                                     */
/*  Suite: [Symphony][Web] — lock-free publication and PFFFT SIMD select. */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_web_publication)

#include "modules/symphony/core/symphony_published_u64.h"
#include "modules/symphony/core/symphony_rate_policy.h"
#include "modules/symphony/core/symphony_seqlock.h"
#include "modules/symphony/runtime/voice_manager.h"
#include "modules/symphony/thirdparty/pffft/pffft.h"
#include "tests/modules/symphony_test_helpers.h"

#include "core/os/os.h"
#include "core/os/thread.h"

#include <atomic>
#include <cstdint>

namespace TestSymphonyWebPublication {

TEST_CASE("[Symphony][Web] callback publication uses lock-free 32-bit atomics") {
	static_assert(std::atomic<uint32_t>::is_always_lock_free);
	static_assert(std::atomic<float>::is_always_lock_free);
	static_assert(!std::atomic<SymphonyVoicePool::VoiceMetrics>::is_always_lock_free);

	SymphonyPublishedU64 published;
	CHECK(published.load() == 0);
	CHECK(published.fetch_add(5) == 0);
	CHECK(published.load() == 5);
	published.publish(uint64_t(0x100000001ULL));
	CHECK(published.load() == uint64_t(0x100000001ULL));
	CHECK(published.writer_value() == uint64_t(0x100000001ULL));

	SymphonySeqLock<SymphonyVoicePool::VoiceMetrics> metrics;
	SymphonyVoicePool::VoiceMetrics written;
	written.active = 3;
	written.virtual_count = 1;
	written.stolen_this_frame = 2;
	written.budget_percent = 12.5f;
	written.occupancy_percent = 40.0f;
	metrics.store(written);
	const SymphonyVoicePool::VoiceMetrics read = metrics.load();
	CHECK(read.active == 3);
	CHECK(read.virtual_count == 1);
	CHECK(read.stolen_this_frame == 2);
	CHECK(read.budget_percent == 12.5f);
	CHECK(read.occupancy_percent == 40.0f);
}

TEST_CASE("[Symphony][Web] PFFFT scalar or SIMD kernel matches the build flags") {
	const char *arch = pffft_simd_arch();
	const int simd = pffft_simd_size();
	REQUIRE(arch != nullptr);
	MESSAGE("pffft arch=", String(arch), " simd_size=", simd);
	CHECK((simd == 1 || simd == 4));

#if defined(PFFFT_SIMD_DISABLE)
	CHECK(simd == 1);
	CHECK(String(arch) == "Scalar");
#elif defined(__wasm_simd128__)
	CHECK(simd == 4);
	CHECK(String(arch) == "SSE1");
#else
	if (String(arch) == "Scalar") {
		CHECK(simd == 1);
	} else {
		CHECK(simd == 4);
	}
#endif

	constexpr int N = 256;
	PFFFT_Setup *setup = pffft_new_setup(N, PFFFT_REAL);
	REQUIRE(setup != nullptr);
	float *in = (float *)pffft_aligned_malloc(sizeof(float) * N);
	float *spec = (float *)pffft_aligned_malloc(sizeof(float) * N);
	float *out = (float *)pffft_aligned_malloc(sizeof(float) * N);
	float *work = (float *)pffft_aligned_malloc(sizeof(float) * N);
	REQUIRE(in != nullptr);
	REQUIRE(spec != nullptr);
	REQUIRE(out != nullptr);
	REQUIRE(work != nullptr);
	for (int i = 0; i < N; i++) {
		in[i] = (i == 0) ? 1.0f : 0.0f;
		spec[i] = 0.0f;
		out[i] = 0.0f;
	}
	pffft_transform_ordered(setup, in, spec, work, PFFFT_FORWARD);
	pffft_transform_ordered(setup, spec, out, work, PFFFT_BACKWARD);
	const float scale = 1.0f / (float)N;
	CHECK(Math::abs(out[0] * scale - 1.0f) < 1.0e-4f);
	CHECK(Math::abs(out[1] * scale) < 1.0e-4f);
	pffft_aligned_free(in);
	pffft_aligned_free(spec);
	pffft_aligned_free(out);
	pffft_aligned_free(work);
	pffft_destroy_setup(setup);
}

static_assert(alignof(SymphonyPublishedU64) <= alignof(std::max_align_t));
static_assert(alignof(SymphonySeqLock<SymphonyVoicePool::VoiceMetrics>) <= alignof(std::max_align_t));
static_assert(alignof(SymphonySpscRing<AudioStreamPlaybackSymphonyMusic::MusicCmd, 64>) <= alignof(std::max_align_t));
static_assert(alignof(SymphonySpscRing<AudioStreamPlaybackSymphonyMusic::StatusEntry, 64>) <= alignof(std::max_align_t));
static_assert(alignof(SymphonyVoicePool) <= alignof(std::max_align_t));
static_assert(alignof(AudioStreamPlaybackSymphony) <= alignof(std::max_align_t));
static_assert(alignof(AudioStreamPlaybackSymphonyMusic) <= alignof(std::max_align_t));

struct JoinThread {
	Thread thread;
	std::atomic<bool> *stop = nullptr;
	bool joined = false;

	void join() {
		if (stop) {
			stop->store(true, std::memory_order_release);
		}
		if (!joined && thread.is_started()) {
			thread.wait_to_finish();
			joined = true;
		}
	}

	~JoinThread() { join(); }
};

Dictionary _note_phrase(int64_t p_tick, int p_generation) {
	Dictionary phrase;
	phrase["start_tick"] = p_tick;
	phrase["generation"] = p_generation;
	Array events;
	Dictionary on;
	on["kind"] = "note_on";
	on["tick"] = p_tick;
	on["track"] = 0;
	on["note_id"] = p_generation;
	on["midi"] = 60;
	on["velocity"] = 1.0;
	on["duration_ticks"] = 240;
	events.push_back(on);
	phrase["events"] = events;
	return phrase;
}

TEST_CASE("[Symphony][Web] Godot-heap owners publish and ring under concurrent use") {
	using namespace SymphonyTestHelpers;

	SymphonyVoicePool *const previous_pool = SymphonyVoicePool::get_singleton();
	SymphonyVoicePool *pool = memnew(SymphonyVoicePool);
	REQUIRE(pool != nullptr);
	CHECK(SymphonyVoicePool::get_singleton() == pool);

	struct MetricsState {
		SymphonyVoicePool *pool = nullptr;
		std::atomic<bool> stop{ false };
		std::atomic<uint32_t> loads{ 0 };
		std::atomic<uint32_t> bad{ 0 };
	} metrics;
	metrics.pool = pool;
	JoinThread metrics_reader;
	metrics_reader.stop = &metrics.stop;
	metrics_reader.thread.start(
			[](void *p) {
				MetricsState *s = static_cast<MetricsState *>(p);
				while (!s->stop.load(std::memory_order_acquire)) {
					const float budget = s->pool->get_budget_percent();
					const int active = s->pool->get_active_voice_count();
					if (!(budget >= 0.0f && budget <= 100.0f) || active < 0) {
						s->bad.fetch_add(1, std::memory_order_relaxed);
					}
					s->loads.fetch_add(1, std::memory_order_relaxed);
				}
			},
			&metrics);
	const uint64_t reader_deadline = OS::get_singleton()->get_ticks_usec() + 2000000;
	int published = 0;
	while (metrics.loads.load(std::memory_order_relaxed) < 100 && OS::get_singleton()->get_ticks_usec() < reader_deadline) {
		pool->set_measured_budget_percent((float)(published % 101));
		published++;
	}
	pool->set_measured_budget_percent(100.0f);
	metrics_reader.join();
	CHECK(metrics.loads.load(std::memory_order_relaxed) >= 100);
	CHECK(metrics.bad.load(std::memory_order_relaxed) == 0);
	CHECK(pool->get_budget_percent() == 100.0f);
	memdelete(pool);
	pool = nullptr;
	CHECK(SymphonyVoicePool::get_singleton() == previous_pool);

	Ref<AudioStreamSymphony> instrument;
	instrument.instantiate();
	instrument->set_mix_rate(symphony_offline_rate());
	instrument->set_graph_description(make_click_instrument());

	Ref<AudioStreamSymphonyMusic> music;
	music.instantiate();
	music->set_mix_rate(symphony_offline_rate());
	music->set_initial_bpm(120.0f);
	REQUIRE(music->set_track_instrument(0, instrument, 2, false) == OK);
	Ref<AudioStreamPlaybackSymphonyMusic> music_pb = music->instantiate_playback();
	REQUIRE(music_pb.is_valid());
	music_pb->start();

	struct MusicMix {
		AudioStreamPlaybackSymphonyMusic *voice = nullptr;
		std::atomic<bool> stop{ false };
		std::atomic<uint64_t> mixed{ 0 };
	} music_mix;
	music_mix.voice = music_pb.ptr();
	JoinThread music_thread;
	music_thread.stop = &music_mix.stop;
	music_thread.thread.start(
			[](void *p) {
				MusicMix *s = static_cast<MusicMix *>(p);
				AudioFrame chunk[64];
				while (!s->stop.load(std::memory_order_acquire)) {
					if (s->voice->is_playing()) {
						const int n = s->voice->mix_graph_rate(chunk, 64);
						if (n > 0) {
							s->mixed.fetch_add((uint64_t)n, std::memory_order_relaxed);
						}
					}
				}
			},
			&music_mix);

	int accepted = 0;
	int status_count = 0;
	int64_t music_frame_first = -1;
	int64_t music_frame_last = -1;
	for (int i = 0; i < 16; i++) {
		if (music_pb->submit_phrase(_note_phrase((int64_t)i * 960, i + 1)) == OK) {
			accepted++;
		}
		(void)music_pb->schedule_tempo(100.0f + (float)(i % 5), (int64_t)(i + 2) * 960, 0);
		status_count += music_pb->drain_status().size();
		const Dictionary snap = music_pb->get_transport_snapshot();
		const int64_t frame = (int64_t)snap["frame"];
		if (music_frame_first < 0) {
			music_frame_first = frame;
		}
		music_frame_last = frame;
		OS::get_singleton()->delay_usec(200);
	}
	CHECK(music_frame_last > music_frame_first);
	music_thread.join();
	status_count += music_pb->drain_status().size();
	CHECK(music_mix.mixed.load(std::memory_order_relaxed) > 0);
	CHECK(accepted > 0);
	CHECK(status_count > 0);
	CHECK(music_pb->get_frames_rendered() > 0);
	music_pb->stop();
	music_pb.unref();
	CHECK(music_pb.is_null());

	Ref<AudioStreamPlayback> playback = instrument->instantiate_playback();
	Ref<AudioStreamPlaybackSymphony> voice = playback;
	REQUIRE(voice.is_valid());
	voice->start();

	struct VoiceMix {
		AudioStreamPlaybackSymphony *voice = nullptr;
		std::atomic<bool> stop{ false };
	} voice_mix;
	voice_mix.voice = voice.ptr();
	JoinThread voice_thread;
	voice_thread.stop = &voice_mix.stop;
	voice_thread.thread.start(
			[](void *p) {
				VoiceMix *s = static_cast<VoiceMix *>(p);
				AudioFrame chunk[64];
				while (!s->stop.load(std::memory_order_acquire)) {
					if (s->voice->is_playing()) {
						s->voice->mix_graph_rate(chunk, 64);
					}
				}
			},
			&voice_mix);

	uint64_t rendered = 0;
	for (int i = 0; i < 50; i++) {
		rendered = voice->get_frames_rendered();
		if (rendered > 0) {
			break;
		}
		OS::get_singleton()->delay_usec(200);
	}
	voice_thread.join();
	CHECK(voice->get_frames_rendered() > 0);
	voice->stop();
	voice.unref();
	playback.unref();
	CHECK(voice.is_null());
	CHECK(playback.is_null());
}

} // namespace TestSymphonyWebPublication
