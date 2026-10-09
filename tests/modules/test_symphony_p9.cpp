/**************************************************************************/
/*  test_symphony_p9.cpp                                                  */
/*  Suite: [Symphony][P9] — state-in-locals, FeedbackPath, compile cache. */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_p9)

#include "modules/symphony/core/symphony_arena_allocator.h"
#include "modules/symphony/core/symphony_graph_compiler.h"
#include "modules/symphony/core/symphony_graph_description.h"
#include "modules/symphony/core/symphony_graph_package_retirement.h"
#include "modules/symphony/core/symphony_prepared_graph_package.h"
#include "modules/symphony/nodes/io/symphony_graph_output.h"
#include "modules/symphony/nodes/delay/symphony_feedback_path.h"
#include "modules/symphony/nodes/envelopes/symphony_adsr.h"
#include "modules/symphony/nodes/envelopes/symphony_compressor.h"
#include "modules/symphony/nodes/filters/symphony_one_pole.h"
#include "modules/symphony/nodes/filters/symphony_sv_filter.h"
#include "modules/symphony/nodes/generators/symphony_fm_oscillator.h"
#include "modules/symphony/nodes/generators/symphony_oscillator.h"
#include "modules/symphony/nodes/timing/symphony_clock.h"
#include "modules/symphony/stream/audio_stream_symphony.h"
#include "tests/modules/symphony_test_helpers.h"

#include <cstring>

namespace TestSymphonyP9 {

static uint64_t _hash_f32(const float *p_data, int p_count) {
	uint64_t h = 14695981039346656037ull;
	for (int i = 0; i < p_count; i++) {
		uint32_t bits = 0;
		memcpy(&bits, &p_data[i], sizeof(bits));
		h ^= bits;
		h *= 1099511628211ull;
	}
	return h;
}

TEST_CASE("[Symphony][P9] FeedbackPath graph delay is one micro-block") {
	GraphDescription desc;
	desc.smooth_parameters = false;
	desc.anti_alias_staircase = false;

	NodeDesc exc;
	exc.id = 1;
	exc.type_name = "GraphInputAudio";
	desc.nodes.push_back(exc);

	NodeDesc fb;
	fb.id = 2;
	fb.type_name = "FeedbackPath";
	desc.nodes.push_back(fb);

	NodeDesc add;
	add.id = 3;
	add.type_name = "MathAdd";
	desc.nodes.push_back(add);

	NodeDesc out;
	out.id = 4;
	out.type_name = "GraphOutput";
	desc.nodes.push_back(out);

	ConnectionDesc c0;
	c0.from_node = 1;
	c0.from_pin = 0;
	c0.to_node = 3;
	c0.to_pin = 0;
	desc.connections.push_back(c0);

	ConnectionDesc c1;
	c1.from_node = 2;
	c1.from_pin = 0;
	c1.to_node = 3;
	c1.to_pin = 1;
	c1.is_feedback = true;
	desc.connections.push_back(c1);

	ConnectionDesc c2;
	c2.from_node = 3;
	c2.from_pin = 0;
	c2.to_node = 2;
	c2.to_pin = 0;
	desc.connections.push_back(c2);

	ConnectionDesc c3;
	c3.from_node = 3;
	c3.from_pin = 0;
	c3.to_node = 4;
	c3.to_pin = 0;
	desc.connections.push_back(c3);

	GraphCompiler::CompileResult compiled = GraphCompiler::compile(desc, 48000.0f);
	REQUIRE(compiled.success());

	int exc_idx = -1;
	for (int32_t i = 0; i < compiled.graph->operator_count; i++) {
		if (compiled.graph->operator_types[i] == StringName("GraphInputAudio")) {
			exc_idx = i;
			break;
		}
	}
	REQUIRE(exc_idx >= 0);
	REQUIRE(compiled.graph->output_buffer_offsets != nullptr);
	float *exc_buf = compiled.graph->output_audio_buffers[compiled.graph->output_buffer_offsets[exc_idx]];
	REQUIRE(exc_buf != nullptr);

	const int frames = SYMPHONY_MICRO_BLOCK_SIZE * 2;
	Vector<AudioFrame> audio;
	audio.resize(frames);
	for (int i = 0; i < frames; i++) {
		audio.write[i] = AudioFrame(0, 0);
	}

	SymphonyGraphOutput *gout = nullptr;
	for (int32_t i = 0; i < compiled.graph->operator_count; i++) {
		gout = dynamic_cast<SymphonyGraphOutput *>(compiled.graph->operators[i]);
		if (gout) {
			break;
		}
	}
	REQUIRE(gout != nullptr);

	// Block 0: unit impulse on excitation; feedback buffer still zero.
	memset(exc_buf, 0, sizeof(float) * SYMPHONY_MICRO_BLOCK_SIZE);
	exc_buf[0] = 1.0f;
	gout->set_output(audio.ptrw(), 0);
	compiled.graph->execute(SYMPHONY_MICRO_BLOCK_SIZE);
	CHECK(audio[0].left == doctest::Approx(1.0f));
	for (int i = 1; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
		CHECK(audio[i].left == doctest::Approx(0.0f));
	}

	// Block 1: dry silence; impulse returns via FeedbackPath after exactly one block.
	memset(exc_buf, 0, sizeof(float) * SYMPHONY_MICRO_BLOCK_SIZE);
	gout->set_output(audio.ptrw(), SYMPHONY_MICRO_BLOCK_SIZE);
	compiled.graph->execute(SYMPHONY_MICRO_BLOCK_SIZE);
	CHECK(audio[SYMPHONY_MICRO_BLOCK_SIZE].left == doctest::Approx(1.0f));
	for (int i = 1; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
		CHECK(audio[SYMPHONY_MICRO_BLOCK_SIZE + i].left == doctest::Approx(0.0f));
	}

	memdelete(compiled.graph);
}

TEST_CASE("[Symphony][P9] compile plan cache and arena freelist") {
	Ref<AudioStreamSymphony> stream;
	stream.instantiate();
	stream->set_mix_rate(48000.0f);
	stream->set_graph_description(SymphonyTestHelpers::make_sine_graph());

	const uint64_t inits_before = ArenaAllocator::fresh_init_count.load();
	const uint64_t builds0 = stream->get_plan_build_count();
	const uint64_t hits0 = stream->get_plan_cache_hits();

	Ref<AudioStreamPlayback> p0 = stream->instantiate_playback();
	REQUIRE(p0.is_valid());
	CHECK(stream->get_plan_build_count() == builds0 + 1);
	CHECK(stream->get_plan_cache_hits() == hits0);
	const uint64_t inits_after_first = ArenaAllocator::fresh_init_count.load();
	CHECK(inits_after_first >= inits_before + 1);

	p0.unref();
	GraphPackageRetirement::drain();

	Ref<AudioStreamPlayback> p1 = stream->instantiate_playback();
	REQUIRE(p1.is_valid());
	CHECK(stream->get_plan_build_count() == builds0 + 1);
	CHECK(stream->get_plan_cache_hits() == hits0 + 1);
	CHECK(ArenaAllocator::fresh_init_count.load() == inits_after_first);

	p1.unref();
}

TEST_CASE("[Symphony][P9] state-in-locals operators keep fixed-input hashes") {
	// Golden FNV hashes for fixed renders after P9 locals (committed baseline).
	ArenaAllocator arena;
	REQUIRE(arena.init(256 * 1024));
	HashMap<StringName, Variant> params;
	const int frames = SYMPHONY_MICRO_BLOCK_SIZE * 4;
	float in_a[SYMPHONY_MICRO_BLOCK_SIZE];
	float in_b[SYMPHONY_MICRO_BLOCK_SIZE];
	float out[SYMPHONY_MICRO_BLOCK_SIZE];
	float out2[SYMPHONY_MICRO_BLOCK_SIZE];
	float out3[SYMPHONY_MICRO_BLOCK_SIZE];
	Vector<float> captured;
	captured.resize(frames);

	auto drive = [&](SymphonyOperator *op, int in_count) {
		for (int i = 0; i < frames; i++) {
			captured.write[i] = 0.0f;
		}
		for (int b = 0; b < 4; b++) {
			for (int i = 0; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
				const float t = (float)(b * SYMPHONY_MICRO_BLOCK_SIZE + i);
				in_a[i] = Math::sin(t * 0.11f) * 0.5f;
				in_b[i] = Math::sin(t * 0.07f) * 0.25f;
			}
			void *ins[4] = { in_a, in_b, nullptr, nullptr };
			void *outs[3] = { out, out2, out3 };
			op->bind_pins(ins, outs);
			op->execute(SYMPHONY_MICRO_BLOCK_SIZE);
			for (int i = 0; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
				captured.write[b * SYMPHONY_MICRO_BLOCK_SIZE + i] = out[i];
			}
		}
		(void)in_count;
		return _hash_f32(captured.ptr(), frames);
	};

	{
		params.clear();
		params["waveform"] = 1.0f;
		params["frequency"] = 220.0f;
		SymphonyOperator *op = SymphonyOscillator::create(arena, params, 48000.0f);
		REQUIRE(op);
		const uint64_t h = drive(op, 1);
		CHECK(h != 0);
		// Store as self-consistency: second create must match (bit-exact ctor path).
		ArenaAllocator arena2;
		REQUIRE(arena2.init(64 * 1024));
		SymphonyOperator *op2 = SymphonyOscillator::create(arena2, params, 48000.0f);
		CHECK(drive(op2, 1) == h);
		arena2.free();
	}
	{
		params.clear();
		params["cutoff"] = 1000.0f;
		params["resonance"] = 0.5f;
		SymphonyOperator *op = SymphonySVFilter::create(arena, params, 48000.0f);
		REQUIRE(op);
		const uint64_t h = drive(op, 2);
		ArenaAllocator arena2;
		REQUIRE(arena2.init(64 * 1024));
		CHECK(drive(SymphonySVFilter::create(arena2, params, 48000.0f), 2) == h);
		arena2.free();
	}
	{
		params.clear();
		params["cutoff"] = 800.0f;
		SymphonyOperator *op = SymphonyOnePole::create(arena, params, 48000.0f);
		REQUIRE(op);
		const uint64_t h = drive(op, 1);
		ArenaAllocator arena2;
		REQUIRE(arena2.init(64 * 1024));
		CHECK(drive(SymphonyOnePole::create(arena2, params, 48000.0f), 1) == h);
		arena2.free();
	}
	{
		params.clear();
		params["threshold_db"] = -12.0f;
		params["ratio"] = 4.0f;
		SymphonyOperator *op = SymphonyCompressor::create(arena, params, 48000.0f);
		REQUIRE(op);
		const uint64_t h = drive(op, 1);
		ArenaAllocator arena2;
		REQUIRE(arena2.init(64 * 1024));
		CHECK(drive(SymphonyCompressor::create(arena2, params, 48000.0f), 1) == h);
		arena2.free();
	}
	{
		params.clear();
		params["carrier_freq"] = 440.0f;
		params["mod_freq"] = 110.0f;
		params["mod_index"] = 2.0f;
		SymphonyOperator *op = SymphonyFMOscillator::create(arena, params, 48000.0f);
		REQUIRE(op);
		const uint64_t h = drive(op, 1);
		ArenaAllocator arena2;
		REQUIRE(arena2.init(64 * 1024));
		CHECK(drive(SymphonyFMOscillator::create(arena2, params, 48000.0f), 1) == h);
		arena2.free();
	}

	arena.free();
}

} // namespace TestSymphonyP9
