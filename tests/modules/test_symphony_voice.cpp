/**************************************************************************/
/*  test_symphony_voice.cpp                                               */
/*  Suite: [Symphony][Voice] — voice pool, stealing, RTPC, triggers.      */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_voice)

#include "modules/symphony/core/symphony_trigger.h"
#include "modules/symphony/nodes/io/symphony_trigger_input.h"
#include "modules/symphony/core/symphony_arena_allocator.h"
#include "modules/symphony/core/symphony_graph_package_retirement.h"
#include "modules/symphony/core/symphony_prepared_graph_package.h"
#include "modules/symphony/core/symphony_graph_compiler.h"
#include "modules/symphony/core/symphony_graph_description.h"
#include "modules/symphony/core/symphony_runtime_metrics.h"
#include "modules/symphony/runtime/rtpc_engine.h"
#include "modules/symphony/runtime/voice_manager.h"
#include "modules/symphony/runtime/event_dispatcher.h"
#include "modules/symphony/runtime/sound_event.h"
#include "modules/symphony/stream/audio_stream_symphony.h"
#include "modules/symphony/stream/audio_stream_playback_symphony.h"
#include "modules/symphony/core/symphony_voice_manager.h"
#include "scene/resources/audio/audio_stream_wav.h"
#include "tests/modules/symphony_test_helpers.h"
#include "core/config/project_settings.h"
#include "core/os/os.h"

namespace TestSymphonyVoice {

TEST_CASE("[Symphony][Voice] TriggerInput SPSC queue drops when full") {
	ArenaAllocator arena;
	REQUIRE(arena.init(4096));

	HashMap<StringName, Variant> params;
	SymphonyOperator *op = SymphonyTriggerInput::create(arena, params, 48000.0f);
	REQUIRE(op != nullptr);
	SymphonyTriggerInput *tin = static_cast<SymphonyTriggerInput *>(op);

	TriggerBuffer out;
	void *outs[1] = { &out };
	tin->bind_pins(nullptr, outs);

	const uint64_t before = symphony_dropped_trigger_count().load();

	int accepted = 0;
	for (int i = 0; i < 80; i++) {
		if (tin->fire(1.0f)) {
			accepted++;
		}
	}
	CHECK(accepted == 64);
	CHECK(symphony_dropped_trigger_count().load() == before + 16);

	tin->execute(SYMPHONY_MICRO_BLOCK_SIZE);
	CHECK(out.count == SYMPHONY_MAX_TRIGGERS_PER_BLOCK);
}

TEST_CASE("[Symphony][Voice] RTPCEngine register returns handle; no auto-create") {
	RTPCEngine *engine = RTPCEngine::get_singleton();
	REQUIRE(engine != nullptr);

	const uint64_t missing_before = engine->get_missing_handle_count();
	CHECK(engine->set_parameter_target("rtpc_test_unregistered", 1.0f) == false);
	CHECK(engine->get_missing_handle_count() == missing_before + 1);

	RTPCEngine::Handle h = engine->register_global_parameter("rtpc_test_param", 0.25f, 5.0f);
	REQUIRE(h != RTPCEngine::INVALID_HANDLE);
	CHECK(engine->find_global_parameter("rtpc_test_param") == h);
	CHECK(engine->set_target(h, 0.75f) == true);
	CHECK(engine->get_target_value(h) == doctest::Approx(0.75f));
	CHECK(engine->set_parameter_target_by_handle(h, 0.6f) == true);
	CHECK(engine->get_target_value(h) == doctest::Approx(0.6f));
	CHECK(engine->set_parameter_target_by_handle(-1, 1.0f) == false);
	CHECK(engine->set_parameter_target("rtpc_test_param", 0.5f) == true);

	RTPCEngine::Handle ah = engine->register_analysis("rtpc_test_analysis");
	REQUIRE(ah != RTPCEngine::INVALID_HANDLE);
	CHECK(engine->find_analysis("rtpc_test_analysis") == ah);
	CHECK(engine->set_analysis_by_handle(ah, 0.4f) == true);
	CHECK(engine->get_analysis_by_handle(ah) == doctest::Approx(0.4f));
	CHECK(engine->set_analysis_by_handle(-1, 1.0f) == false);
}

TEST_CASE("[Symphony][Voice] acquire_slot is free-only; dispatcher steals at event cap") {
	SymphonyVoicePool *pool = SymphonyVoicePool::get_singleton();
	SymphonyEventDispatcher *dispatcher = SymphonyEventDispatcher::get_singleton();
	REQUIRE(pool != nullptr);
	REQUIRE(dispatcher != nullptr);

	// Fill with free acquires — no steal inside acquire_slot.
	for (int i = 0; i < pool->get_pool_size(); i++) {
		CHECK(pool->acquire_slot(50) >= 0);
	}
	CHECK(pool->acquire_slot(50) == -1);

	// Free all for clean dispatcher test.
	for (int i = 0; i < pool->get_pool_size(); i++) {
		pool->release_slot(i, true);
	}

	Ref<SoundEvent> event;
	event.instantiate();
	event->set_max_voices(1);
	event->set_steal_mode(SoundEvent::STEAL_OLDEST);
	event->set_priority(40);

	Ref<AudioStreamWAV> stream;
	stream.instantiate();
	TypedArray<AudioStream> streams;
	streams.push_back(stream);
	event->set_streams(streams);

	SymphonyEventDispatcher::PlayResult pr;
	StringName reason;
	int slot0 = dispatcher->dispatch(event, pr, reason);
	REQUIRE(slot0 >= 0);
	CHECK(pr == SymphonyEventDispatcher::RESULT_PLAYED);

	int slot1 = dispatcher->dispatch(event, pr, reason);
	REQUIRE(slot1 >= 0);
	CHECK(slot1 == slot0);
	CHECK(pr == SymphonyEventDispatcher::RESULT_STOLEN);
	CHECK(reason == StringName("oldest"));

	Dictionary stolen = dispatcher->play_event(event);
	CHECK((int)stolen["slot"] == slot1);
	CHECK((int)stolen["result"] == (int)SymphonyEventDispatcher::RESULT_STOLEN);
	CHECK(StringName(stolen["steal_reason"]) == StringName("oldest"));

	pool->release_slot(slot1, true);
	dispatcher->on_voice_stopped(event->get_instance_id());
}

TEST_CASE("[Symphony][Voice] play_event validates stream before steal") {
	SymphonyVoicePool *pool = SymphonyVoicePool::get_singleton();
	SymphonyEventDispatcher *dispatcher = SymphonyEventDispatcher::get_singleton();
	REQUIRE(pool != nullptr);
	REQUIRE(dispatcher != nullptr);

	for (int i = 0; i < pool->get_pool_size(); i++) {
		REQUIRE(pool->acquire_slot(10) >= 0);
	}

	Ref<SoundEvent> event;
	event.instantiate();
	event->set_priority(99);
	event->set_max_voices(0);
	TypedArray<AudioStream> streams;
	streams.push_back(Variant()); // nil stream — must reject without stealing
	event->set_streams(streams);

	const int active_before = pool->get_active_voice_count();
	Dictionary result = dispatcher->play_event(event);
	CHECK((int)result["result"] == (int)SymphonyEventDispatcher::RESULT_REJECTED_NO_STREAMS);
	CHECK((int)result["slot"] == -1);
	CHECK(pool->get_active_voice_count() == active_before);

	for (int i = 0; i < pool->get_pool_size(); i++) {
		pool->release_slot(i, true);
	}
}

TEST_CASE("[Symphony][Voice] transition cost estimate scales with units") {
	SymphonyVoiceManager *mgr = SymphonyVoiceManager::get_singleton();
	REQUIRE(mgr != nullptr);

	const float light = mgr->estimate_cpu_fraction_for_cost(64.0f, 48000.0f, 512);
	const float heavy = mgr->estimate_cpu_fraction_for_cost(64.0f * 48.0f, 48000.0f, 512);
	CHECK(light > 0.0f);
	CHECK(heavy > light);
}

TEST_CASE("[Symphony][Voice] get_debug_metrics exposes transition trigger retirement memory") {
	SymphonyVoiceManager *mgr = SymphonyVoiceManager::get_singleton();
	REQUIRE(mgr != nullptr);

	const uint64_t destroyed_before = mgr->get_packages_destroyed_count();
	const uint64_t crossfade_before = mgr->get_crossfade_transition_count();
	const uint64_t fallback_before = mgr->get_fallback_transition_count();
	const uint64_t underflow_before = mgr->get_spectral_underflow_count();

	mgr->note_crossfade_transition();
	mgr->note_fallback_transition();
	symphony_note_spectral_underflow();

	GraphDescription desc;
	NodeDesc gin;
	gin.id = 1;
	gin.type_name = "GraphInput";
	gin.params.insert("parameter_name", "x");
	gin.params.insert("default_value", 0.0f);
	desc.nodes.push_back(gin);
	NodeDesc out;
	out.id = 2;
	out.type_name = "GraphOutput";
	desc.nodes.push_back(out);
	ConnectionDesc c;
	c.from_node = 1;
	c.from_pin = 0;
	c.to_node = 2;
	c.to_pin = 0;
	desc.connections.push_back(c);

	GraphCompiler::CompileResult compiled = GraphCompiler::compile(desc, 48000.0f);
	REQUIRE(compiled.success());
	PreparedGraphPackage *pkg = PreparedGraphPackage::create_from_graph(compiled.graph, compiled.arena_bytes, compiled.total_package_bytes);
	REQUIRE(pkg != nullptr);
	GraphPackageRetirement::retire(pkg);
	CHECK(GraphPackageRetirement::get_pending_count() >= 1);
	GraphPackageRetirement::drain();

	CHECK(mgr->get_crossfade_transition_count() == crossfade_before + 1);
	CHECK(mgr->get_fallback_transition_count() == fallback_before + 1);
	CHECK(mgr->get_spectral_underflow_count() == underflow_before + 1);
	CHECK(mgr->get_packages_destroyed_count() == destroyed_before + 1);

	Dictionary metrics = mgr->get_debug_metrics();
	CHECK(metrics.has("dropped_trigger_count"));
	CHECK(metrics.has("spectral_underflow_count"));
	CHECK(metrics.has("crossfade_transition_count"));
	CHECK(metrics.has("fallback_transition_count"));
	CHECK(metrics.has("retirement_pending"));
	CHECK(metrics.has("packages_destroyed"));
	CHECK(metrics.has("memory_global_used_bytes"));
	CHECK(metrics.has("packages_active"));
	CHECK(metrics.has("rt_violations"));
	CHECK((int64_t)metrics["packages_destroyed"] == (int64_t)mgr->get_packages_destroyed_count());
	CHECK((int)metrics["rt_violations"] == (int)mgr->get_rt_violation_count());
}

TEST_CASE("[Symphony][Voice] generation handles reject stale control") {
	SymphonyVoicePool *pool = SymphonyVoicePool::get_singleton();
	REQUIRE(pool != nullptr);
	int slot = pool->acquire_slot(40);
	REQUIRE(slot >= 0);
	int64_t handle = pool->make_voice_handle(slot);
	CHECK(handle > 0);
	CHECK(pool->resolve_voice_handle(handle) == slot);
	CHECK(pool->resolve_voice_handle(slot) == -1);
	pool->release_slot(slot, true);
	CHECK(pool->resolve_voice_handle(handle) == -1);
	int again = pool->acquire_slot(40);
	REQUIRE(again >= 0);
	int64_t next = pool->make_voice_handle(again);
	CHECK(next != handle);
	CHECK(pool->resolve_voice_handle(handle) == -1);
	CHECK(pool->resolve_voice_handle(next) == again);
	pool->release_slot(again, true);
}

TEST_CASE("[Symphony][Voice] occupancy and measured budget are separate") {
	SymphonyVoicePool *pool = SymphonyVoicePool::get_singleton();
	REQUIRE(pool != nullptr);
	pool->set_measured_budget_percent(12.5f);
	pool->process_frame();
	CHECK(pool->get_budget_percent() == doctest::Approx(12.5f));
	float expected = pool->get_pool_size() > 0 ? (100.0f * pool->get_active_voice_count() / pool->get_pool_size()) : 0.0f;
	CHECK(pool->get_occupancy_percent() == doctest::Approx(expected));
}

TEST_CASE("[Symphony][Voice][P8a] N13 resets position; non-positional distance_factor is 1") {
	SymphonyVoicePool *pool = SymphonyVoicePool::get_singleton();
	REQUIRE(pool != nullptr);

	pool->set_listener_position(Vector3(1000, 0, 0));
	pool->set_reference_distance(100.0f);

	int slot = pool->acquire_slot(50);
	REQUIRE(slot >= 0);
	// Stale position left over would have crushed importance before N13.
	pool->set_slot_position(slot, Vector3(1000, 0, 0));
	pool->set_slot_spatial_mode(slot, (int)SoundEvent::SPATIAL_NON_POSITIONAL);
	pool->set_slot_tier(slot, (int)SoundEvent::TIER_IMPORTANT);
	pool->set_slot_importance_weight(slot, 1.0f);
	pool->set_slot_category(slot, (int)SoundEvent::CATEGORY_SFX);

	SymphonyVoicePool::VoiceSlot *vs = pool->get_slot(slot);
	REQUIRE(vs != nullptr);
	// Re-activate to prove position clears on acquire/reclaim.
	pool->reclaim_slot(slot, 50);
	vs = pool->get_slot(slot);
	CHECK(vs->position == Vector3());

	pool->set_slot_spatial_mode(slot, (int)SoundEvent::SPATIAL_NON_POSITIONAL);
	pool->set_slot_position(slot, Vector3(1000, 0, 0)); // must be ignored
	pool->set_slot_tier(slot, (int)SoundEvent::TIER_IMPORTANT);
	pool->set_slot_importance_weight(slot, 1.0f);
	pool->update_importance_all();
	// tier_band(IMPORTANT) + 50 * 1 * 1 * 1
	CHECK(pool->get_slot_importance(slot) == doctest::Approx(SoundEvent::TIER_BAND_IMPORTANT + 50.0f));

	pool->release_slot(slot, true);
}

TEST_CASE("[Symphony][Voice][P8a] VITAL survives budget steal at critical load") {
	SymphonyVoiceManager *mgr = SymphonyVoiceManager::get_singleton();
	REQUIRE(mgr != nullptr);

	const int32_t prev_max = mgr->get_max_voices();
	const float prev_warn = mgr->get_warning_threshold();
	const float prev_crit = mgr->get_critical_threshold();
	mgr->set_max_voices(0);
	mgr->set_warning_threshold(0.3f);
	mgr->set_critical_threshold(0.5f);

	Ref<AudioStreamSymphony> stream_vital;
	stream_vital.instantiate();
	stream_vital->set_mix_rate(48000.0f);
	stream_vital->set_graph_description(SymphonyTestHelpers::make_sine_graph());
	Ref<AudioStreamPlayback> base_v = stream_vital->instantiate_playback();
	Ref<AudioStreamPlaybackSymphony> vital = base_v;
	REQUIRE(vital.is_valid());
	vital->start();
	AudioFrame chunk[64];
	vital->mix(chunk, 1.0f, 64);
	vital->set_voice_tier((int)SoundEvent::TIER_VITAL);
	vital->set_voice_importance(SoundEvent::TIER_BAND_VITAL + 10.0f);
	vital->_test_set_cost_ewma(80.0f);

	Ref<AudioStreamSymphony> stream_opt;
	stream_opt.instantiate();
	stream_opt->set_mix_rate(48000.0f);
	stream_opt->set_graph_description(SymphonyTestHelpers::make_sine_graph());
	Ref<AudioStreamPlayback> base_o = stream_opt->instantiate_playback();
	Ref<AudioStreamPlaybackSymphony> optional = base_o;
	REQUIRE(optional.is_valid());
	optional->start();
	optional->mix(chunk, 1.0f, 64);
	optional->set_voice_tier((int)SoundEvent::TIER_OPTIONAL);
	optional->set_voice_importance(10.0f);
	optional->_test_set_cost_ewma(80.0f);

	const uint64_t t0 = 2'000'000;
	// Refresh mix_seq so both stay in the snapshot across enforces.
	vital->_test_set_cost_ewma(80.0f);
	optional->_test_set_cost_ewma(80.0f);
	mgr->_enforce_voice_limits_at(t0);
	vital->_test_set_cost_ewma(80.0f);
	optional->_test_set_cost_ewma(80.0f);
	mgr->_enforce_voice_limits_at(t0 + 40'000);

	CHECK(vital->is_playing());
	CHECK_FALSE(optional->is_playing());

	vital->stop();
	optional->stop();
	vital.unref();
	optional.unref();
	base_v.unref();
	base_o.unref();
	mgr->process_deferred_lod();
	mgr->set_max_voices(prev_max);
	mgr->set_warning_threshold(prev_warn);
	mgr->set_critical_threshold(prev_crit);
}

TEST_CASE("[Symphony][Voice][P8a] pool pushes importance to bound playback") {
	SymphonyVoicePool *pool = SymphonyVoicePool::get_singleton();
	REQUIRE(pool != nullptr);

	Ref<AudioStreamSymphony> stream;
	stream.instantiate();
	stream->set_mix_rate(48000.0f);
	stream->set_graph_description(SymphonyTestHelpers::make_sine_graph());
	Ref<AudioStreamPlayback> base = stream->instantiate_playback();
	Ref<AudioStreamPlaybackSymphony> playback = base;
	REQUIRE(playback.is_valid());

	int slot = pool->acquire_slot(40);
	REQUIRE(slot >= 0);
	pool->set_slot_tier(slot, (int)SoundEvent::TIER_IMPORTANT);
	pool->set_slot_importance_weight(slot, 2.0f);
	pool->set_slot_spatial_mode(slot, (int)SoundEvent::SPATIAL_NON_POSITIONAL);
	pool->set_slot_playback(slot, playback.ptr());
	pool->update_importance_all();

	const float expected = SoundEvent::TIER_BAND_IMPORTANT + 40.0f * 2.0f;
	CHECK(pool->get_slot_importance(slot) == doctest::Approx(expected));
	CHECK(playback->get_voice_importance() == doctest::Approx(expected));
	CHECK(playback->get_voice_tier() == (int)SoundEvent::TIER_IMPORTANT);

	pool->clear_slot_playback(slot);
	pool->release_slot(slot, true);
}

TEST_CASE("[Symphony][Voice][P8a] lower tier cannot steal VITAL slot") {
	SymphonyVoicePool *pool = SymphonyVoicePool::get_singleton();
	SymphonyEventDispatcher *dispatcher = SymphonyEventDispatcher::get_singleton();
	REQUIRE(pool != nullptr);
	REQUIRE(dispatcher != nullptr);

	for (int i = 0; i < pool->get_pool_size(); i++) {
		pool->release_slot(i, true);
	}

	Ref<SoundEvent> vital_event;
	vital_event.instantiate();
	vital_event->set_priority(10);
	vital_event->set_tier(SoundEvent::TIER_VITAL);
	vital_event->set_max_voices(0);
	Ref<AudioStreamWAV> stream;
	stream.instantiate();
	TypedArray<AudioStream> streams;
	streams.push_back(stream);
	vital_event->set_streams(streams);

	SymphonyEventDispatcher::PlayResult pr;
	StringName reason;
	int vital_slot = dispatcher->dispatch(vital_event, pr, reason);
	REQUIRE(vital_slot >= 0);

	// Fill the rest of the pool so the next dispatch must steal.
	while (pool->acquire_slot(1) >= 0) {
	}

	Ref<SoundEvent> optional_event;
	optional_event.instantiate();
	optional_event->set_priority(99);
	optional_event->set_tier(SoundEvent::TIER_OPTIONAL);
	optional_event->set_max_voices(0);
	optional_event->set_streams(streams);

	int stolen = dispatcher->dispatch(optional_event, pr, reason);
	// Optional may steal a low-priority filler, but never the VITAL slot.
	CHECK(stolen != vital_slot);
	CHECK(pool->get_slot(vital_slot)->tier == (int)SoundEvent::TIER_VITAL);
	CHECK(pool->get_slot(vital_slot)->event_id == (uint64_t)vital_event->get_instance_id());

	if (stolen >= 0) {
		pool->release_slot(stolen, true);
		dispatcher->on_voice_stopped(optional_event->get_instance_id());
	}
	pool->release_slot(vital_slot, true);
	dispatcher->on_voice_stopped(vital_event->get_instance_id());
	for (int i = 0; i < pool->get_pool_size(); i++) {
		pool->release_slot(i, true);
	}
}

TEST_CASE("[Symphony][Voice][P8b] category cap steals within category or rejects") {
	SymphonyVoicePool *pool = SymphonyVoicePool::get_singleton();
	SymphonyEventDispatcher *dispatcher = SymphonyEventDispatcher::get_singleton();
	REQUIRE(pool != nullptr);
	REQUIRE(dispatcher != nullptr);

	for (int i = 0; i < pool->get_pool_size(); i++) {
		pool->release_slot(i, true);
	}

	const int prev_sfx_cap = (int)ProjectSettings::get_singleton()->get_setting("audio/symphony/category_caps/sfx");
	ProjectSettings::get_singleton()->set_setting("audio/symphony/category_caps/sfx", 2);

	Ref<AudioStreamWAV> stream;
	stream.instantiate();
	TypedArray<AudioStream> streams;
	streams.push_back(stream);

	Ref<SoundEvent> sfx_a;
	sfx_a.instantiate();
	sfx_a->set_category(SoundEvent::CATEGORY_SFX);
	sfx_a->set_priority(40);
	sfx_a->set_max_voices(0);
	sfx_a->set_steal_mode(SoundEvent::STEAL_OLDEST);
	sfx_a->set_streams(streams);

	Ref<SoundEvent> sfx_b;
	sfx_b.instantiate();
	sfx_b->set_category(SoundEvent::CATEGORY_SFX);
	sfx_b->set_priority(40);
	sfx_b->set_max_voices(0);
	sfx_b->set_steal_mode(SoundEvent::STEAL_OLDEST);
	sfx_b->set_streams(streams);

	Ref<SoundEvent> ui_event;
	ui_event.instantiate();
	ui_event->set_category(SoundEvent::CATEGORY_UI);
	ui_event->set_priority(40);
	ui_event->set_max_voices(0);
	ui_event->set_streams(streams);

	SymphonyEventDispatcher::PlayResult pr;
	StringName reason;
	int s0 = dispatcher->dispatch(sfx_a, pr, reason);
	int s1 = dispatcher->dispatch(sfx_b, pr, reason);
	REQUIRE(s0 >= 0);
	REQUIRE(s1 >= 0);
	CHECK(s0 != s1);

	int ui_slot = dispatcher->dispatch(ui_event, pr, reason);
	REQUIRE(ui_slot >= 0);

	// Cap=2: third SFX steals within SFX, not the UI voice.
	int s2 = dispatcher->dispatch(sfx_a, pr, reason);
	REQUIRE(s2 >= 0);
	CHECK(pr == SymphonyEventDispatcher::RESULT_STOLEN);
	CHECK(s2 != ui_slot);
	CHECK((s2 == s0 || s2 == s1));

	// Fill remaining free slots with UI so only UI (+ capped SFX) remain.
	while (pool->acquire_slot(1) >= 0) {
	}
	// Mark fillers as UI category so they aren't same-category victims.
	for (int i = 0; i < pool->get_pool_size(); i++) {
		SymphonyVoicePool::VoiceSlot *vs = pool->get_slot(i);
		if (!vs || (vs->state != SymphonyVoicePool::VOICE_PLAYING && vs->state != SymphonyVoicePool::VOICE_TO_PLAY)) {
			continue;
		}
		if (vs->event_id == 0) {
			vs->category = (int)SoundEvent::CATEGORY_UI;
			vs->priority = 99;
			vs->tier = (int)SoundEvent::TIER_IMPORTANT;
		}
	}

	// Both SFX slots still at cap; steal one SFX again.
	int s3 = dispatcher->dispatch(sfx_b, pr, reason);
	REQUIRE(s3 >= 0);
	CHECK(pool->get_slot(s3)->category == (int)SoundEvent::CATEGORY_SFX);

	ProjectSettings::get_singleton()->set_setting("audio/symphony/category_caps/sfx", prev_sfx_cap);
	for (int i = 0; i < pool->get_pool_size(); i++) {
		pool->release_slot(i, true);
	}
	dispatcher->on_voice_stopped(sfx_a->get_instance_id());
	dispatcher->on_voice_stopped(sfx_b->get_instance_id());
	dispatcher->on_voice_stopped(ui_event->get_instance_id());
}

TEST_CASE("[Symphony][Voice][P8b] N8 forced victim honours event max_voices") {
	SymphonyVoicePool *pool = SymphonyVoicePool::get_singleton();
	SymphonyEventDispatcher *dispatcher = SymphonyEventDispatcher::get_singleton();
	REQUIRE(pool != nullptr);
	REQUIRE(dispatcher != nullptr);

	for (int i = 0; i < pool->get_pool_size(); i++) {
		pool->release_slot(i, true);
	}

	Ref<AudioStreamWAV> stream;
	stream.instantiate();
	TypedArray<AudioStream> streams;
	streams.push_back(stream);

	Ref<SoundEvent> limited;
	limited.instantiate();
	limited->set_max_voices(1);
	limited->set_priority(50);
	limited->set_streams(streams);

	Ref<SoundEvent> other;
	other.instantiate();
	other->set_max_voices(0);
	other->set_priority(10);
	other->set_streams(streams);

	SymphonyEventDispatcher::PlayResult pr;
	StringName reason;
	int limited_slot = dispatcher->dispatch(limited, pr, reason);
	REQUIRE(limited_slot >= 0);
	int other_slot = dispatcher->dispatch(other, pr, reason);
	REQUIRE(other_slot >= 0);
	REQUIRE(other_slot != limited_slot);

	// At event cap: forced reclaim of a different event's slot must reject.
	Dictionary rejected = dispatcher->play_event(limited, Vector3(), false, other_slot);
	CHECK((int)rejected["slot"] == -1);
	CHECK((int)rejected["result"] == (int)SymphonyEventDispatcher::RESULT_REJECTED_VOICE_LIMIT);
	CHECK(StringName(rejected["steal_reason"]) == StringName("event_cap"));

	// Same-event forced victim still steals.
	Dictionary stolen = dispatcher->play_event(limited, Vector3(), false, limited_slot);
	CHECK((int)stolen["slot"] == limited_slot);
	CHECK((int)stolen["result"] == (int)SymphonyEventDispatcher::RESULT_STOLEN);

	pool->release_slot(limited_slot, true);
	pool->release_slot(other_slot, true);
	dispatcher->on_voice_stopped(limited->get_instance_id());
	dispatcher->on_voice_stopped(other->get_instance_id());
}

TEST_CASE("[Symphony][Voice][P8b] N12 STEAL_QUIETEST prefers valid RMS then quietest") {
	SymphonyVoicePool *pool = SymphonyVoicePool::get_singleton();
	SymphonyEventDispatcher *dispatcher = SymphonyEventDispatcher::get_singleton();
	REQUIRE(pool != nullptr);
	REQUIRE(dispatcher != nullptr);

	for (int i = 0; i < pool->get_pool_size(); i++) {
		pool->release_slot(i, true);
	}

	Ref<AudioStreamWAV> stream;
	stream.instantiate();
	TypedArray<AudioStream> streams;
	streams.push_back(stream);

	Ref<SoundEvent> loud;
	loud.instantiate();
	loud->set_priority(20);
	loud->set_max_voices(0);
	loud->set_importance_weight(1.0f);
	loud->set_streams(streams);

	Ref<SoundEvent> quiet;
	quiet.instantiate();
	quiet->set_priority(20);
	quiet->set_max_voices(0);
	quiet->set_importance_weight(1.0f);
	quiet->set_streams(streams);

	Ref<SoundEvent> no_rms;
	no_rms.instantiate();
	no_rms->set_priority(20);
	no_rms->set_max_voices(0);
	no_rms->set_importance_weight(0.1f); // lowest importance if RMS ignored
	no_rms->set_streams(streams);

	SymphonyEventDispatcher::PlayResult pr;
	StringName reason;
	int loud_slot = dispatcher->dispatch(loud, pr, reason);
	int quiet_slot = dispatcher->dispatch(quiet, pr, reason);
	int no_rms_slot = dispatcher->dispatch(no_rms, pr, reason);
	REQUIRE(loud_slot >= 0);
	REQUIRE(quiet_slot >= 0);
	REQUIRE(no_rms_slot >= 0);

	pool->set_slot_rms(loud_slot, 0.9f);
	pool->set_slot_rms(quiet_slot, 0.1f);
	// no_rms_slot left with rms_valid=false

	while (pool->acquire_slot(1) >= 0) {
	}

	Ref<SoundEvent> incoming;
	incoming.instantiate();
	incoming->set_priority(50);
	incoming->set_max_voices(0);
	incoming->set_steal_mode(SoundEvent::STEAL_QUIETEST);
	incoming->set_streams(streams);

	int stolen = dispatcher->dispatch(incoming, pr, reason);
	REQUIRE(stolen >= 0);
	CHECK(stolen == quiet_slot);
	CHECK(reason == StringName("quietest"));
	CHECK(pr == SymphonyEventDispatcher::RESULT_STOLEN);

	for (int i = 0; i < pool->get_pool_size(); i++) {
		pool->release_slot(i, true);
	}
	dispatcher->on_voice_stopped(loud->get_instance_id());
	dispatcher->on_voice_stopped(quiet->get_instance_id());
	dispatcher->on_voice_stopped(no_rms->get_instance_id());
	dispatcher->on_voice_stopped(incoming->get_instance_id());
}

TEST_CASE("[Symphony][Authoring] operator schema lists trigger names and modal arrays") {
	Dictionary schema = AudioStreamSymphony::get_operator_schema();
	CHECK((int)schema["schema_version"] == 2);
	Array operators = schema["operators"];
	CHECK(operators.size() >= 44);
	bool saw_trigger = false;
	bool saw_modal = false;
	bool saw_finish = false;
	bool noise_brown = false;
	bool lfo_triangle_at_1 = false;
	int declared_arrays = 0;
	String previous;
	for (int i = 0; i < operators.size(); i++) {
		Dictionary op = operators[i];
		String type = op["type"];
		if (!previous.is_empty()) {
			CHECK(previous.nocasecmp_to(type) <= 0);
		}
		previous = type;
		Array params = op["params"];
		if (type == "TriggerInput") {
			for (int p = 0; p < params.size(); p++) {
				Dictionary param = params[p];
				if (String(param["name"]) == "trigger_name") {
					saw_trigger = true;
				}
			}
		}
		if (type == "ResonatorAnalyzer" || type == "Waveshaper") {
			// Array params the factories read must be declared, or strict
			// (schema_version 2) validation rejects them as unknown.
			const String array_param = type == "Waveshaper" ? "table_data" : "frequencies";
			for (int p = 0; p < params.size(); p++) {
				Dictionary param = params[p];
				if (String(param["name"]) == array_param && String(param["type"]) == "float_array") {
					declared_arrays++;
				}
			}
		}
		if (type == "ModalBank") {
			for (int p = 0; p < params.size(); p++) {
				Dictionary param = params[p];
				if (String(param["name"]) == "frequencies") {
					saw_modal = true;
				}
			}
		}
		if (type == "GraphOutput") {
			Array inputs = op["inputs"];
			for (int p = 0; p < inputs.size(); p++) {
				Dictionary pin = inputs[p];
				if (String(pin["name"]) == "finish") {
					saw_finish = true;
				}
			}
		}
		// Enum labels are read by index, so each label set must cover the
		// operator's integer range exactly.
		for (int p = 0; p < params.size(); p++) {
			Dictionary param = params[p];
			Array values = param["enum"];
			if (values.is_empty()) {
				continue;
			}
			int span = (int)(float)param["max"] - (int)(float)param["min"] + 1;
			CHECK_MESSAGE(values.size() == span, type, ".", String(param["name"]));
			if (type == "LFO" && String(param["name"]) == "waveform") {
				lfo_triangle_at_1 = values.size() > 1 && String(values[1]) == "triangle";
			}
		}
		if (type == "Noise") {
			for (int p = 0; p < params.size(); p++) {
				Dictionary param = params[p];
				if (String(param["name"]) == "mode") {
					Array values = param["enum"];
					for (int e = 0; e < values.size(); e++) {
						if (String(values[e]) == "brown") {
							noise_brown = true;
						}
					}
				}
			}
		}
	}
	CHECK(saw_trigger);
	CHECK(saw_modal);
	CHECK(saw_finish);
	CHECK_FALSE(noise_brown);
	CHECK(lfo_triangle_at_1);
	CHECK(declared_arrays == 2);
}

} // namespace TestSymphonyVoice
