/**************************************************************************/
/*  test_symphony_hardening.cpp                                           */
/*  Suite: [Symphony][Hardening] — budget, clip leases, carry, offline.   */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_hardening)

#include "modules/symphony/core/symphony_memory_budget.h"
#include "modules/symphony/core/symphony_rate_policy.h"
#include "modules/symphony/core/symphony_voice_manager.h"
#include "modules/symphony/runtime/rtpc_engine.h"
#include "modules/symphony/runtime/symphony_music_scheduler.h"
#include "modules/symphony/runtime/symphony_music_stereo_pcm.h"
#include "modules/symphony/runtime/symphony_music_stretch.h"
#include "modules/symphony/stream/audio_stream_playback_symphony_music.h"
#include "modules/symphony/stream/audio_stream_symphony.h"
#include "modules/symphony/stream/audio_stream_symphony_music.h"
#include "tests/modules/symphony_test_helpers.h"

#include "core/math/math_funcs.h"

using namespace SymphonyTestHelpers;

namespace TestSymphonyHardening {

struct BudgetRestore {
	SymphonyVoiceManager *vm = nullptr;
	~BudgetRestore() {
		if (vm) {
			vm->set_memory_budget_mib(0);
		}
	}
};

TEST_CASE("[Symphony][Hardening] Memory budget rejects invalid and busy limits") {
	SymphonyVoiceManager *vm = SymphonyVoiceManager::get_singleton();
	REQUIRE(vm != nullptr);
	BudgetRestore restore{ vm };
	REQUIRE(vm->set_memory_budget_mib(0) == OK);
	const int resolved = vm->get_memory_budget_mib();
	CHECK(resolved > 0);
	CHECK(vm->set_memory_budget_mib(-5) == ERR_INVALID_PARAMETER);
	CHECK(vm->get_memory_budget_mib() == resolved);
	CHECK(vm->set_memory_budget_mib(int64_t(1) << 50) == ERR_INVALID_PARAMETER);
	CHECK(vm->get_memory_budget_mib() == resolved);

	const Error lowered = vm->set_memory_budget_mib(1);
	if (lowered == ERR_BUSY) {
		CHECK(vm->get_memory_budget_mib() == resolved);
	} else {
		CHECK(lowered == OK);
		CHECK(vm->get_memory_budget_mib() == 1);
		Ref<AudioStreamSymphonyMusic> music;
		music.instantiate();
		PackedFloat32Array pcm;
		pcm.resize(2 * 1024 * 1024);
		for (int i = 0; i < pcm.size(); i++) {
			pcm.write[i] = 0.01f;
		}
		const int32_t before = music->get_clip_count();
		CHECK(music->register_clip_pcm("harden_oom", pcm, 48000.0f) == ERR_OUT_OF_MEMORY);
		CHECK(music->get_clip_count() == before);
	}
	REQUIRE(vm->set_memory_budget_mib(resolved) == OK);
	CHECK(vm->set_memory_budget_mib(resolved + 16) == OK);
	CHECK(vm->get_memory_budget_mib() == resolved + 16);
}

TEST_CASE("[Symphony][Hardening] Stretch context keeps its PCM lease across registry release") {
	SymphonyMusicStereoCache *cache = SymphonyMusicStereoCache::get_singleton();
	REQUIRE(cache != nullptr);
	PackedFloat32Array pcm;
	pcm.resize(256);
	for (int i = 0; i < pcm.size(); i++) {
		pcm.write[i] = (i & 1) ? 0.2f : -0.2f;
	}
	const int before = cache->get_entry_count();
	const StringName key = SymphonyMusicStereoCache::make_cache_key("harden_lease", 48000.0f, 128, SymphonyMusicStereoCache::hash_pcm(pcm.ptr(), 128));
	const float *stored = cache->acquire(key, pcm.ptr(), 128, 48000.0f);
	REQUIRE(stored != nullptr);
	SymphonyStretchPool pool;
	SymphonyMusicClipDesc desc;
	desc.pcm_key = key;
	desc.pcm = stored;
	desc.frames = 128;
	desc.sample_rate = 48000.0f;
	desc.body_end = 128;
	SymphonyStretchContext *ctx = pool.acquire_prepared(desc, 1.0f, 0.0f);
	REQUIRE(ctx != nullptr);
	cache->release(key);
	CHECK(cache->get_entry_count() == before + 1);
	CHECK(cache->get_frames(key) == 128);
	ctx->request_return();
	pool.reclaim_returned();
	CHECK(cache->get_entry_count() == before);
}

TEST_CASE("[Symphony][Hardening] Carry capacity is simultaneous, not lifetime") {
	SymphonyMusicScheduler scheduler;
	scheduler.reset(48000.0f, 120.0f);
	SymphonyMusicPhrase first;
	first.generation = 1;
	first.start_tick = 0;
	SymphonyMusicEvent on;
	on.kind = SymphonyMusicEventKind::NoteOn;
	on.tick = 0;
	on.note_id = 1;
	REQUIRE(first.push(on));
	for (int i = 0; i < SymphonyMusicScheduler::CARRY_CAP; i++) {
		SymphonyMusicEvent off;
		off.kind = SymphonyMusicEventKind::NoteOff;
		off.tick = 400 + i;
		off.note_id = 1000 + i;
		REQUIRE(first.push(off));
	}
	REQUIRE(scheduler.submit_phrase(first));
	SymphonyMusicPhrase second;
	second.generation = 2;
	second.start_tick = 200;
	SymphonyMusicEvent on2 = on;
	on2.tick = 200;
	on2.note_id = 2;
	REQUIRE(second.push(on2));
	REQUIRE(scheduler.submit_phrase(second));
	scheduler.advance(48000, [](const SymphonyMusicEvent &, uint64_t) {});
	SymphonyMusicScheduler::Notice notices[32];
	while (scheduler.drain_notices(notices, 32) > 0) {
	}

	SymphonyMusicPhrase third;
	third.generation = 3;
	third.start_tick = 5000;
	SymphonyMusicEvent off;
	off.kind = SymphonyMusicEventKind::NoteOff;
	off.tick = 5200;
	off.note_id = 3;
	REQUIRE(third.push(off));
	REQUIRE(scheduler.submit_phrase(third));
	scheduler.advance(160000, [](const SymphonyMusicEvent &, uint64_t) {});
	const int n = scheduler.drain_notices(notices, 32);
	for (int i = 0; i < n; i++) {
		CHECK(notices[i].kind != SymphonyMusicScheduler::NoticeKind::NoteForcedOff);
	}
}

TEST_CASE("[Symphony][Hardening] RTPC coefficients are computed on the mix callback") {
	RTPCEngine *rtpc = RTPCEngine::get_singleton();
	REQUIRE(rtpc != nullptr);
	const RTPCEngine::Handle handle = rtpc->register_parameter("harden_rtpc", 0.0f, 5.0f);
	REQUIRE(handle != RTPCEngine::INVALID_HANDLE);
	REQUIRE(rtpc->set_target(handle, 1.0f));
	rtpc->smooth_all(64);
	CHECK(rtpc->get_current_value(handle) > 0.0f);
}

TEST_CASE("[Symphony][Hardening] Offline playback stays at 48 kHz and chunks") {
	Ref<AudioStreamSymphony> instrument;
	instrument.instantiate();
	instrument->set_graph_description(make_click_instrument());
	Ref<AudioStreamSymphonyMusic> music;
	music.instantiate();
	music->set_mix_rate(44100.0f);
	music->set_initial_bpm(120.0f);
	REQUIRE(music->set_track_instrument(0, instrument, 2, true) == OK);
	Ref<AudioStreamPlaybackSymphonyMusic> offline = music->instantiate_offline_playback(4);
	REQUIRE(offline.is_valid());
	Dictionary phrase;
	phrase["start_tick"] = 0;
	phrase["generation"] = 1;
	Array events;
	Dictionary note;
	note["kind"] = "note_on";
	note["tick"] = 0;
	note["track"] = 0;
	note["note_id"] = 1;
	note["midi"] = 60;
	note["velocity"] = 0.8;
	note["duration_ticks"] = 480;
	events.push_back(note);
	phrase["events"] = events;
	REQUIRE(offline->submit_phrase(phrase) == OK);
	PackedVector2Array chunk = offline->render_offline(4096);
	REQUIRE(chunk.size() == 4096);
	const Dictionary snap = offline->get_transport_snapshot();
	CHECK((float)snap["sample_rate"] == doctest::Approx(48000.0f));
	CHECK(offline->render_offline(4097).is_empty());

	Ref<AudioStreamPlaybackSymphonyMusic> live = music->instantiate_playback();
	CHECK(live->render_offline(64).is_empty());
}

} // namespace TestSymphonyHardening
