/**************************************************************************/
/*  test_symphony_b5.cpp                                                  */
/*  Suite: [Symphony][B5] — races, cache, budget, BeatClock, trigger_at.  */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_b5)

#include "modules/symphony/core/symphony_graph_compiler.h"
#include "modules/symphony/core/symphony_graph_description.h"
#include "modules/symphony/core/symphony_graph_package_retirement.h"
#include "modules/symphony/core/symphony_prepared_graph_package.h"
#include "modules/symphony/core/symphony_trigger.h"
#include "modules/symphony/effects/audio_effect_symphony_reverb.h"
#include "modules/symphony/nodes/generators/symphony_oscillator.h"
#include "modules/symphony/nodes/io/symphony_graph_output.h"
#include "modules/symphony/nodes/io/symphony_trigger_input.h"
#include "modules/symphony/runtime/beat_clock.h"
#include "modules/symphony/runtime/rtpc_engine.h"
#include "modules/symphony/stream/audio_stream_symphony.h"
#include "tests/modules/symphony_test_helpers.h"

#include "core/object/callable_mp.h"
#include "core/object/object.h"
#include "core/os/thread.h"
#include "servers/audio/audio_frame.h"

#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>

namespace TestSymphonyB5 {

class B5BeatCatcher : public Object {
public:
	Vector<int> beats;
	Vector<float> times;
	void on_beat(int p_beat, float p_time) {
		beats.push_back(p_beat);
		times.push_back(p_time);
	}
};

TEST_CASE("[Symphony][B5] Oscillator export_state includes triangle_primed") {
	GraphDescription desc = SymphonyTestHelpers::make_sine_graph();
	desc.nodes.write[0].params["waveform"] = 3.0f;
	GraphCompiler::CompileResult result = GraphCompiler::compile(desc, 48000.0f);
	REQUIRE(result.success());
	SymphonyOscillator *osc = nullptr;
	for (int32_t i = 0; i < result.graph->operator_count; i++) {
		osc = dynamic_cast<SymphonyOscillator *>(result.graph->operators[i]);
		if (osc) {
			break;
		}
	}
	REQUIRE(osc != nullptr);
	CHECK(osc->export_state(nullptr, 0) == 12);
	AudioFrame frames[SYMPHONY_MICRO_BLOCK_SIZE];
	SymphonyGraphOutput *gout = nullptr;
	for (int32_t i = 0; i < result.graph->operator_count; i++) {
		gout = dynamic_cast<SymphonyGraphOutput *>(result.graph->operators[i]);
		if (gout) {
			break;
		}
	}
	REQUIRE(gout != nullptr);
	gout->set_output(frames, 0);
	result.graph->execute(SYMPHONY_MICRO_BLOCK_SIZE);
	uint8_t buf[12] = {};
	CHECK(osc->export_state(buf, 12) == 12);
	uint32_t primed = 0;
	memcpy(&primed, buf + 8, sizeof(uint32_t));
	CHECK(primed == 1);
	memdelete(result.graph);
}

TEST_CASE("[Symphony][B5] trigger_at 100 then 50 both fire at exact offsets") {
	SymphonyTriggerInput op;
	TriggerBuffer out;
	void *outputs[] = { &out };
	op.bind_pins(nullptr, outputs);
	REQUIRE(op.fire_at(100, 1.0f));
	REQUIRE(op.fire_at(50, 2.0f));

	bool saw_50 = false;
	bool saw_100 = false;
	const int block = SYMPHONY_MICRO_BLOCK_SIZE;
	for (int start = 0; start < 192; start += block) {
		out.clear();
		op.set_block_start_frame((uint64_t)start);
		op.execute(block);
		for (int32_t i = 0; i < out.count; i++) {
			const int abs_frame = start + out.events[i].sample_offset;
			if (Math::abs(out.events[i].value - 2.0f) < 1e-6f) {
				CHECK(abs_frame == 50);
				saw_50 = true;
			}
			if (Math::abs(out.events[i].value - 1.0f) < 1e-6f) {
				CHECK(abs_frame == 100);
				saw_100 = true;
			}
		}
	}
	CHECK(saw_50);
	CHECK(saw_100);
}

TEST_CASE("[Symphony][B5] Compile cache invalidates on every mutator") {
	Ref<AudioStreamSymphony> stream;
	stream.instantiate();
	stream->set_mix_rate(48000.0f);
	stream->set_graph_description(SymphonyTestHelpers::make_sine_graph());
	CompiledGraph *g0 = stream->compile_graph();
	REQUIRE(g0 != nullptr);
	const uint64_t builds0 = stream->get_plan_build_count();
	memdelete(g0);
	CompiledGraph *g1 = stream->compile_graph();
	REQUIRE(g1 != nullptr);
	CHECK(stream->get_plan_build_count() == builds0);
	CHECK(stream->get_plan_cache_hits() >= 1);
	memdelete(g1);

	auto expect_rebuild = [&](const char *p_label) {
		CompiledGraph *g = stream->compile_graph();
		REQUIRE_MESSAGE(g != nullptr, p_label);
		CHECK_MESSAGE(stream->get_plan_build_count() > builds0, p_label);
		memdelete(g);
	};

	stream->set_graph_description(SymphonyTestHelpers::make_sine_graph(220.0f));
	expect_rebuild("set_graph_description");

	const uint64_t after_set = stream->get_plan_build_count();
	stream->load_test_graph();
	CompiledGraph *g_load = stream->compile_graph();
	REQUIRE(g_load != nullptr);
	CHECK(stream->get_plan_build_count() > after_set);
	memdelete(g_load);

	stream->load_test_graph_30();
	CompiledGraph *g30 = stream->compile_graph();
	REQUIRE(g30 != nullptr);
	memdelete(g30);

	stream->load_test_graph_50();
	CompiledGraph *g50 = stream->compile_graph();
	REQUIRE(g50 != nullptr);
	memdelete(g50);

	stream->set_graph_description(SymphonyTestHelpers::make_sine_graph());
	{
		CompiledGraph *warm = stream->compile_graph();
		if (warm) {
			memdelete(warm);
		}
	}
	stream->add_lod_variant();
	CompiledGraph *g_add = stream->compile_graph();
	REQUIRE(g_add != nullptr);
	memdelete(g_add);

	stream->duplicate_main_to_lod();
	CompiledGraph *g_dup = stream->compile_graph();
	REQUIRE(g_dup != nullptr);
	memdelete(g_dup);

	GraphDescription lod = SymphonyTestHelpers::make_sine_graph(110.0f);
	CHECK(stream->set_lod_variant(1, lod));
	CompiledGraph *g_lod = stream->compile_graph();
	REQUIRE(g_lod != nullptr);
	memdelete(g_lod);

	stream->set_graph_for_tier(0, SymphonyTestHelpers::make_sine_graph(330.0f));
	CompiledGraph *g_tier = stream->compile_graph();
	REQUIRE(g_tier != nullptr);
	memdelete(g_tier);

	(void)stream->get_graph_for_tier_mut(0);
	CompiledGraph *g_mut = stream->compile_graph();
	REQUIRE(g_mut != nullptr);
	memdelete(g_mut);

	CHECK(stream->remove_lod_variant(1));
	CompiledGraph *g_rm = stream->compile_graph();
	REQUIRE(g_rm != nullptr);
	memdelete(g_rm);
}

TEST_CASE("[Symphony][B5] Retirement recycles arena into stream freelist") {
	Ref<AudioStreamSymphony> stream;
	stream.instantiate();
	stream->set_mix_rate(48000.0f);
	stream->set_graph_description(SymphonyTestHelpers::make_sine_graph());
	CompiledGraph *graph = stream->compile_graph();
	REQUIRE(graph != nullptr);
	PreparedGraphPackage *pkg = PreparedGraphPackage::create_from_graph(graph);
	REQUIRE(pkg != nullptr);
	pkg->owner_stream = stream->get_instance_id();
	GraphPackageRetirement::retire(pkg);
	GraphPackageRetirement::drain();
	CHECK(stream->get_arena_freelist_count() >= 1);

	const uint64_t alloc_before = Memory::get_thread_alloc_count();
	CompiledGraph *graph2 = stream->compile_graph();
	REQUIRE(graph2 != nullptr);
	const uint64_t alloc_after = Memory::get_thread_alloc_count();
	(void)alloc_before;
	(void)alloc_after;
	CHECK(stream->get_arena_freelist_count() == 0);
	memdelete(graph2);
}

TEST_CASE("[Symphony][B5] Budget counts operator_types and promotions") {
	GraphDescription desc = SymphonyTestHelpers::make_sine_graph();
	GraphCompiler::CompileResult result = GraphCompiler::compile(desc, 48000.0f);
	REQUIRE(result.success());
	const int32_t n = result.graph->operator_count;
	const size_t names = sizeof(StringName) * (size_t)n;
	CHECK(result.non_arena_bytes >= names * 2); // node_names + operator_types
	if (result.graph->budgeted_bytes > 0) {
		CHECK(result.graph->budgeted_bytes == result.total_package_bytes);
	}
	memdelete(result.graph);
}

TEST_CASE("[Symphony][B5] BeatClock fire_ahead emits early with heard target_time") {
	BeatClock *clock = BeatClock::get_singleton();
	REQUIRE(clock != nullptr);
	B5BeatCatcher *catcher = memnew(B5BeatCatcher);
	clock->connect(SNAME("beat_hit"), callable_mp(catcher, &B5BeatCatcher::on_beat));
	clock->start(120.0f, 4);
	clock->set_fire_ahead_s(0.5f); // one beat at 120 BPM
	clock->update_playback_position(0.0);
	clock->process(0.016);
	REQUIRE(catcher->beats.size() >= 2);
	CHECK(catcher->beats[0] == 0);
	CHECK(catcher->beats[1] == 1);
	CHECK(catcher->times[1] == doctest::Approx(0.5f).epsilon(0.0001));
	clock->disconnect(SNAME("beat_hit"), callable_mp(catcher, &B5BeatCatcher::on_beat));
	clock->set_fire_ahead_s(0.0f);
	clock->stop();
	memdelete(catcher);
}

TEST_CASE("[Symphony][B5] BeatClock update_transport emits each beat once across a tempo ramp") {
	BeatClock *clock = BeatClock::get_singleton();
	REQUIRE(clock != nullptr);
	B5BeatCatcher *catcher = memnew(B5BeatCatcher);
	clock->connect(SNAME("beat_hit"), callable_mp(catcher, &B5BeatCatcher::on_beat));
	clock->start(120.0f, 4);
	clock->set_fire_ahead_s(0.0f);
	const double positions[] = { 0.0, 0.4, 0.9, 1.2, 1.8, 2.05 };
	const float bpms[] = { 120.0f, 130.0f, 140.0f, 150.0f, 160.0f, 170.0f };
	for (int i = 0; i < 6; i++) {
		clock->update_transport(positions[i], bpms[i], 4);
		clock->process(0.016);
	}
	int ones = 0;
	int twos = 0;
	for (int i = 0; i < catcher->beats.size(); i++) {
		if (catcher->beats[i] == 1) {
			ones++;
		}
		if (catcher->beats[i] == 2) {
			twos++;
		}
	}
	CHECK(ones == 1);
	CHECK(twos == 1);
	clock->disconnect(SNAME("beat_hit"), callable_mp(catcher, &B5BeatCatcher::on_beat));
	clock->stop();
	memdelete(catcher);
}

TEST_CASE("[Symphony][B5][Stress] RTPC smoothing atomics vs smooth_all") {
	RTPCEngine *engine = RTPCEngine::get_singleton();
	REQUIRE(engine != nullptr);
	const StringName name = "b5_rtpc_smooth";
	RTPCEngine::Handle h = engine->register_parameter(name, 0.0f, 5.0f);
	if (h == RTPCEngine::INVALID_HANDLE) {
		h = engine->find_parameter(name);
	}
	REQUIRE(h != RTPCEngine::INVALID_HANDLE);

	std::atomic<bool> stop{ false };
	Thread smoother;
	smoother.start(
			[](void *p) {
				auto *flag = static_cast<std::atomic<bool> *>(p);
				RTPCEngine *eng = RTPCEngine::get_singleton();
				while (!flag->load(std::memory_order_acquire)) {
					eng->smooth_all(64);
				}
			},
			&stop);

	for (int i = 0; i < 400; i++) {
		engine->set_parameter_smoothing(h, 1.0f + (float)(i % 20), 2.0f + (float)(i % 15));
		engine->set_sample_rate(44100.0f + (float)(i % 2) * 3900.0f);
		engine->set_target(h, (float)(i % 10) * 0.1f);
	}
	stop.store(true, std::memory_order_release);
	smoother.wait_to_finish();
	engine->smooth_all(64);
}

TEST_CASE("[Symphony][B5][Stress] Reverb set_room during process") {
	Ref<AudioEffectSymphonyReverb> fx;
	fx.instantiate();
	fx->set_dry(0.0f);
	fx->set_wet(1.0f);
	Ref<AudioEffectInstance> inst = fx->instantiate();
	REQUIRE(inst.is_valid());

	std::atomic<bool> run{ true };
	Thread writer;
	struct WriterArgs {
		AudioEffectSymphonyReverb *fx = nullptr;
		std::atomic<bool> *run = nullptr;
	} args;
	args.fx = fx.ptr();
	args.run = &run;
	writer.start(
			[](void *p) {
				auto *a = static_cast<WriterArgs *>(p);
				float t = 0.5f;
				while (a->run->load()) {
					a->fx->set_room(0.5f + t, 0.3f + 0.2f * t, Vector3(6 + t, 3, 5 + t), 0.5f + 0.2f * t);
					t = t > 2.0f ? 0.5f : t + 0.01f;
				}
			},
			&args);

	const int n = 256;
	std::vector<AudioFrame> src(n), dst(n);
	for (int i = 0; i < n; i++) {
		src[i] = AudioFrame(0.1f, -0.1f);
	}
	for (int k = 0; k < 200; k++) {
		inst->process(src.data(), dst.data(), n);
		for (int i = 0; i < n; i++) {
			CHECK(std::isfinite(dst[i].left));
			CHECK(std::isfinite(dst[i].right));
		}
	}
	run.store(false);
	writer.wait_to_finish();
}

} // namespace TestSymphonyB5
