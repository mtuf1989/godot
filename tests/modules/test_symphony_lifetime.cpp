/**************************************************************************/
/*  test_symphony_lifetime.cpp                                            */
/*  Suite: [Symphony][Lifetime] — P1 voice reclaim, stop/start, denormals. */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_lifetime)

#include "modules/symphony/core/symphony_arena_allocator.h"
#include "modules/symphony/core/symphony_denormal_guard.h"
#include "modules/symphony/core/symphony_graph_compiler.h"
#include "modules/symphony/core/symphony_graph_description.h"
#include "modules/symphony/core/symphony_graph_package_retirement.h"
#include "modules/symphony/core/symphony_memory_budget.h"
#include "modules/symphony/core/symphony_voice_manager.h"
#include "modules/symphony/nodes/synthesis/symphony_modal_bank.h"
#include "modules/symphony/stream/audio_stream_playback_symphony.h"
#include "modules/symphony/stream/audio_stream_symphony.h"
#include "tests/modules/symphony_test_helpers.h"

#include "core/os/os.h"
#include "core/os/thread.h"
#include "servers/audio/audio_server.h"

#include <cfloat>
#include <cmath>
#include <cstring>

namespace TestSymphonyLifetime {

using SymphonyTestHelpers::make_sine_graph;
using SymphonyTestHelpers::render_graph;

TEST_CASE("[Symphony][Lifetime] T1 unit reap restores package counters") {
	SymphonyVoiceManager *mgr = SymphonyVoiceManager::get_singleton();
	REQUIRE(mgr != nullptr);
	SymphonyMemoryBudget *budget = SymphonyMemoryBudget::get_singleton();
	REQUIRE(budget != nullptr);

	// Clear any orphaned voices left by earlier cases so the baseline is honest.
	mgr->process_deferred_lod();
	GraphPackageRetirement::drain();
	const size_t reserved_before = budget->get_snapshot().reserved_bytes;
	const int32_t active_before = budget->get_snapshot().active_packages;
	const int32_t retained_before = mgr->get_retained_voice_count();

	Ref<AudioStreamSymphony> stream;
	stream.instantiate();
	stream->set_mix_rate(48000.0f);
	stream->set_graph_description(make_sine_graph());
	Ref<AudioStreamPlayback> base = stream->instantiate_playback();
	Ref<AudioStreamPlaybackSymphony> playback = base;
	REQUIRE(playback.is_valid());
	playback->start();
	CHECK(playback->is_registered_with_voice_manager());
	CHECK(mgr->get_retained_voice_count() == retained_before + 1);

	playback->stop();
	CHECK_FALSE(playback->is_playing());
	// Parked while Ref alive.
	CHECK(playback->is_registered_with_voice_manager());

	playback.unref();
	base.unref();
	mgr->process_deferred_lod();
	GraphPackageRetirement::drain();

	CHECK(mgr->get_retained_voice_count() == retained_before);
	CHECK(budget->get_snapshot().active_packages == active_before);
	CHECK(budget->get_snapshot().reserved_bytes == reserved_before);
	CHECK(GraphPackageRetirement::get_pending_count() == 0);
}

TEST_CASE("[Symphony][Lifetime] T3 external Ref keeps the voice parked until unref") {
	SymphonyVoiceManager *mgr = SymphonyVoiceManager::get_singleton();
	REQUIRE(mgr != nullptr);
	mgr->process_deferred_lod();
	GraphPackageRetirement::drain();
	const int32_t retained_before = mgr->get_retained_voice_count();

	Ref<AudioStreamSymphony> stream;
	stream.instantiate();
	stream->set_mix_rate(48000.0f);
	stream->set_graph_description(make_sine_graph());
	Ref<AudioStreamPlayback> base = stream->instantiate_playback();
	Ref<AudioStreamPlaybackSymphony> playback = base;
	REQUIRE(playback.is_valid());
	playback->start();
	playback->stop();
	CHECK_FALSE(playback->is_playing());
	CHECK(playback->is_registered_with_voice_manager());

	// An external Ref keeps the manager entry parked.
	Ref<AudioStreamPlaybackSymphony> hold = playback;
	playback.unref();
	base.unref();
	mgr->process_deferred_lod();
	CHECK(mgr->get_retained_voice_count() == retained_before + 1);
	CHECK(hold->is_registered_with_voice_manager());

	hold.unref();
	mgr->process_deferred_lod();
	GraphPackageRetirement::drain();
	CHECK(mgr->get_retained_voice_count() == retained_before);
}

TEST_CASE("[Symphony][Lifetime] T5 stop then start produces audible RMS") {
	Ref<AudioStreamSymphony> stream;
	stream.instantiate();
	stream->set_mix_rate(48000.0f);
	stream->set_graph_description(make_sine_graph());
	Ref<AudioStreamPlayback> base = stream->instantiate_playback();
	Ref<AudioStreamPlaybackSymphony> playback = base;
	REQUIRE(playback.is_valid());

	playback->start();
	AudioFrame chunk[256];
	playback->mix(chunk, 1.0f, 256);
	playback->stop();
	CHECK_FALSE(playback->is_playing());

	playback->start();
	CHECK(playback->is_playing());
	const int got = playback->mix(chunk, 1.0f, 256);
	REQUIRE(got > 0);
	double sum_sq = 0.0;
	for (int i = 0; i < got; i++) {
		sum_sq += (double)chunk[i].left * chunk[i].left + (double)chunk[i].right * chunk[i].right;
	}
	const float rms = (float)Math::sqrt(sum_sq / (2.0 * (double)got));
	CHECK(rms > 0.01f);
	playback->stop();
}

TEST_CASE("[Symphony][Lifetime][A] T10 enforce skips when voice list lock is held") {
	SymphonyVoiceManager *mgr = SymphonyVoiceManager::get_singleton();
	REQUIRE(mgr != nullptr);
	const uint64_t skipped_before = mgr->get_enforce_skipped_count();
	REQUIRE(mgr->_test_try_lock_voice_list());
	const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	mgr->enforce_voice_limits();
	const uint64_t elapsed = OS::get_singleton()->get_ticks_usec() - t0;
	mgr->_test_unlock_voice_list();
	CHECK(mgr->get_enforce_skipped_count() == skipped_before + 1);
	CHECK(elapsed < 1000); // < 1 ms
}

TEST_CASE("[Symphony][Lifetime][V] T12 demote after sustained warning load") {
	SymphonyVoiceManager *mgr = SymphonyVoiceManager::get_singleton();
	REQUIRE(mgr != nullptr);

	Ref<AudioStreamSymphony> stream;
	stream.instantiate();
	stream->set_mix_rate(48000.0f);
	stream->set_graph_description(make_sine_graph());
	// Need a LOD variant so demotion has somewhere to go.
	GraphDescription lod1 = make_sine_graph();
	stream->set_lod_variant(1, lod1);

	Ref<AudioStreamPlayback> base = stream->instantiate_playback();
	Ref<AudioStreamPlaybackSymphony> playback = base;
	REQUIRE(playback.is_valid());
	playback->start();
	AudioFrame chunk[64];
	playback->mix(chunk, 1.0f, 64);

	playback->_test_set_cost_ewma(75.0f);
	const uint64_t t0 = 1'000'000;
	mgr->_enforce_voice_limits_at(t0);
	CHECK(playback->get_budget_lod_floor() == 0); // first tick only arms the warn timer

	// Refresh mix_seq so the voice stays in the snapshot (callback_seq advances each enforce).
	playback->_test_set_cost_ewma(75.0f);
	mgr->_enforce_voice_limits_at(t0 + 40'000); // 40 ms above W
	CHECK(playback->get_budget_lod_floor() >= 1);

	playback->_test_set_cost_ewma(62.0f);
	const int floor_after = playback->get_budget_lod_floor();
	mgr->_enforce_voice_limits_at(t0 + 100'000);
	CHECK(playback->get_budget_lod_floor() == floor_after); // hold

	playback->stop();
	playback.unref();
	base.unref();
	mgr->process_deferred_lod();
}

TEST_CASE("[Symphony][Lifetime][H] T17 denormal guard flushes subnormals") {
	REQUIRE(SymphonyDenormalGuard::is_supported());
	SymphonyDenormalGuard guard;
	CHECK(SymphonyDenormalGuard::is_flush_active());
	volatile float x = FLT_MIN * 0.5f;
	CHECK(x == 0.0f);
}

TEST_CASE("[Symphony][Lifetime][H] T18 mix restores FPCR after guard") {
	const bool before = SymphonyDenormalGuard::is_flush_active();
	Ref<AudioStreamSymphony> stream;
	stream.instantiate();
	stream->set_mix_rate(48000.0f);
	stream->set_graph_description(make_sine_graph());
	Ref<AudioStreamPlayback> base = stream->instantiate_playback();
	Ref<AudioStreamPlaybackSymphony> playback = base;
	REQUIRE(playback.is_valid());
	playback->start();
	AudioFrame chunk[64];
	playback->mix(chunk, 1.0f, 64);
	playback->stop();
	CHECK(SymphonyDenormalGuard::is_flush_active() == before);
}

TEST_CASE("[Symphony][Lifetime][H] T19 ModalBank flushes dead mode state") {
	ArenaAllocator arena;
	REQUIRE(arena.init(64 * 1024));
	HashMap<StringName, Variant> params;
	params.insert("max_modes", 1.0f);
	params.insert("num_modes", 1.0f);
	PackedFloat32Array freqs;
	freqs.push_back(440.0f);
	PackedFloat32Array decays;
	decays.push_back(10.0f); // long t60
	PackedFloat32Array gains;
	gains.push_back(1.0f);
	params.insert("frequencies", freqs);
	params.insert("decay_times", decays);
	params.insert("gains", gains);

	SymphonyModalBank *bank = (SymphonyModalBank *)SymphonyModalBank::create(arena, params, 48000.0f);
	REQUIRE(bank != nullptr);

	float excitation[SYMPHONY_MICRO_BLOCK_SIZE] = {};
	float pitch_ratio[1] = { 1.0f };
	float out[SYMPHONY_MICRO_BLOCK_SIZE] = {};
	void *in_ptrs[2] = { excitation, pitch_ratio };
	void *out_ptrs[1] = { out };
	bank->bind_pins(in_ptrs, out_ptrs);

	// Strike once, then let it ring into silence.
	excitation[0] = 1.0f;
	bank->execute(SYMPHONY_MICRO_BLOCK_SIZE);
	excitation[0] = 0.0f;
	for (int i = 0; i < 20000; i++) {
		bank->execute(SYMPHONY_MICRO_BLOCK_SIZE);
		if (bank->activity == 0) {
			break;
		}
	}
	CHECK(bank->activity == 0);

	uint8_t state[512];
	const size_t n = bank->export_state(state, sizeof(state));
	// Layout: num_modes (i32) + current_ratio (f32) + y1[max] + y2[max].
	REQUIRE(n > sizeof(int32_t) + sizeof(float));
	const size_t y_off = sizeof(int32_t) + sizeof(float);
	// After activity=0, both y1 and y2 for the mode must be exact zeros (no subnormals).
	float y1 = 0.0f;
	float y2 = 0.0f;
	memcpy(&y1, state + y_off, sizeof(float));
	memcpy(&y2, state + y_off + sizeof(float), sizeof(float));
	CHECK(y1 == 0.0f);
	CHECK(y2 == 0.0f);
}

TEST_CASE("[Symphony][Lifetime] Render helpers still produce a tone after P1") {
	Vector<float> buf = render_graph(make_sine_graph(440.0f), 2048, 48000.0f);
	REQUIRE(buf.size() == 2048);
	float peak = 0.0f;
	for (int i = 0; i < buf.size(); i++) {
		peak = MAX(peak, Math::abs(buf[i]));
	}
	CHECK(peak > 0.1f);
}

} // namespace TestSymphonyLifetime
