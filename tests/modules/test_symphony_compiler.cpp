/**************************************************************************/
/*  test_symphony_compiler.cpp                                            */
/*  Suite: [Symphony][Compiler] — graph compile, arena sizing, packages.  */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_compiler)

#include "modules/symphony/core/symphony_graph_compiler.h"
#include "modules/symphony/core/symphony_graph_description.h"
#include "modules/symphony/core/symphony_graph_flattener.h"
#include "modules/symphony/stream/audio_stream_symphony.h"
#include "modules/symphony/core/symphony_memory_budget.h"
#include "modules/symphony/core/shared_pcm_cache.h"
#include "modules/symphony/core/symphony_operator_registry.h"
#include "modules/symphony/core/symphony_pin_types.h"
#include "modules/symphony/nodes/delay/symphony_delay_line.h"
#include "modules/symphony/nodes/io/symphony_graph_output.h"
#include "scene/resources/audio/audio_stream.h"
#include "tests/modules/symphony_test_helpers.h"

#include <cmath>

namespace TestSymphonyCompiler {

using SymphonyTestHelpers::alias_ratio_db;
using SymphonyTestHelpers::make_sine_graph;
using SymphonyTestHelpers::render_graph;
using SymphonyTestHelpers::tone_level_db;

TEST_CASE("[Symphony][Compiler] Oscillator descriptor pin counts match bind_pins") {
	const OperatorDescriptor *desc = OperatorRegistry::get_singleton()->find("Oscillator");
	REQUIRE(desc != nullptr);
	// bind_pins reads frequency + pulse_width inputs and one audio output.
	CHECK(desc->inputs.size() == 2);
	CHECK(desc->outputs.size() == 1);
	CHECK(String(desc->inputs[0].name) == "frequency");
	CHECK(String(desc->inputs[1].name) == "pulse_width");
}

TEST_CASE("[Symphony][Compiler] DelayLine descriptor pin counts match bind_pins") {
	const OperatorDescriptor *desc = OperatorRegistry::get_singleton()->find("DelayLine");
	REQUIRE(desc != nullptr);
	CHECK(desc->inputs.size() == 2);
	CHECK(desc->outputs.size() == 1);
	REQUIRE(desc->extra_arena_bytes_fn != nullptr);
}

TEST_CASE("[Symphony][Compiler] DelayLine arena bytes scale with mix rate") {
	HashMap<StringName, Variant> params;
	params.insert("max_delay_ms", 2000.0f);
	params.insert("delay_ms", 10.0f);

	size_t bytes_48k = SymphonyDelayLine::calculate_arena_bytes(params, 48000.0f);
	size_t bytes_96k = SymphonyDelayLine::calculate_arena_bytes(params, 96000.0f);
	CHECK(bytes_48k == sizeof(float) * (2000 * 48000 / 1000 + 4));
	CHECK(bytes_96k == sizeof(float) * (2000 * 96000 / 1000 + 4));
	CHECK(bytes_96k > bytes_48k * 1.9); // ~2× aside from the fixed +4 interpolation pad
	CHECK(bytes_96k < bytes_48k * 2.1);
}

TEST_CASE("[Symphony][Compiler] CompileResult reports arena accounting") {
	GraphDescription desc;
	NodeDesc osc;
	osc.id = 1;
	osc.type_name = "Oscillator";
	osc.params.insert("frequency", 440.0f);
	osc.params.insert("waveform", 0.0f);
	desc.nodes.push_back(osc);

	NodeDesc out;
	out.id = 2;
	out.type_name = "GraphOutput";
	desc.nodes.push_back(out);

	ConnectionDesc conn;
	conn.from_node = 1;
	conn.from_pin = 0;
	conn.to_node = 2;
	conn.to_pin = 0;
	desc.connections.push_back(conn);

	GraphCompiler::CompileResult result = GraphCompiler::compile(desc, 48000.0f);
	REQUIRE(result.success());
	CHECK(result.arena_bytes > 0);
	CHECK(result.arena_used_bytes > 0);
	CHECK(result.arena_used_bytes <= result.arena_bytes);
	CHECK(result.total_package_bytes >= result.arena_bytes);
	// Planned capacity should match consumed bytes for this simple graph.
	CHECK(result.arena_used_bytes == result.arena_bytes);

	memdelete(result.graph);
}

TEST_CASE("[Symphony][Compiler] Memory budget rejects oversized graph") {
	SymphonyMemoryBudget *budget = SymphonyMemoryBudget::get_singleton();
	REQUIRE(budget != nullptr);

	const size_t old_per = budget->get_per_graph_limit_bytes();
	const size_t old_global = budget->get_global_limit_bytes();

	GraphDescription desc;
	NodeDesc osc;
	osc.id = 1;
	osc.type_name = "Oscillator";
	desc.nodes.push_back(osc);

	NodeDesc out;
	out.id = 2;
	out.type_name = "GraphOutput";
	desc.nodes.push_back(out);

	ConnectionDesc conn;
	conn.from_node = 1;
	conn.from_pin = 0;
	conn.to_node = 2;
	conn.to_pin = 0;
	desc.connections.push_back(conn);

	GraphCompiler::CompileResult ok = GraphCompiler::compile(desc, 48000.0f);
	REQUIRE(ok.success());
	CHECK(ok.total_package_bytes == ok.arena_bytes + ok.non_arena_bytes);
	CHECK(ok.non_arena_bytes > 0);
	const size_t needed = ok.total_package_bytes;
	memdelete(ok.graph);

	budget->set_per_graph_limit_bytes(needed > 0 ? needed - 1 : 0);

	GraphCompiler::CompileResult result = GraphCompiler::compile(desc, 48000.0f);
	CHECK_FALSE(result.success());
	REQUIRE(result.errors.size() > 0);
	CHECK(result.errors[0].findn("budget") != -1);

	budget->set_per_graph_limit_bytes(old_per);
	budget->set_global_limit_bytes(old_global);
}

static GraphDescription make_osc_to_output_graph() {
	GraphDescription desc;
	NodeDesc osc;
	osc.id = 1;
	osc.type_name = "Oscillator";
	osc.params.insert("frequency", 440.0f);
	osc.params.insert("waveform", 0.0f);
	desc.nodes.push_back(osc);

	NodeDesc out;
	out.id = 2;
	out.type_name = "GraphOutput";
	desc.nodes.push_back(out);

	ConnectionDesc conn;
	conn.from_node = 1;
	conn.from_pin = 0;
	conn.to_node = 2;
	conn.to_pin = 0;
	desc.connections.push_back(conn);
	return desc;
}

TEST_CASE("[Symphony][Compiler] Rate matrix compiles and executes") {
	const float rates[] = { 22050.0f, 44100.0f, 48000.0f, 96000.0f };
	const int32_t frames_list[] = { 32, SYMPHONY_MICRO_BLOCK_SIZE };

	for (float rate : rates) {
		for (int32_t frames : frames_list) {
			if (frames > SYMPHONY_MICRO_BLOCK_SIZE) {
				continue;
			}
			GraphCompiler::CompileResult result = GraphCompiler::compile(make_osc_to_output_graph(), rate);
			REQUIRE(result.success());
			CHECK(result.arena_used_bytes == result.arena_bytes);

			AudioFrame out_frames[SYMPHONY_MICRO_BLOCK_SIZE] = {};
			// Wire GraphOutput (execution order last for osc→out).
			for (int32_t i = 0; i < result.graph->operator_count; i++) {
				auto *gout = dynamic_cast<SymphonyGraphOutput *>(result.graph->operators[i]);
				if (gout) {
					gout->set_output(out_frames, 0);
				}
			}
			result.graph->execute(frames);
			bool any_nonzero = false;
			for (int32_t i = 0; i < frames; i++) {
				CHECK(!std::isnan(out_frames[i].left));
				CHECK(!std::isinf(out_frames[i].left));
				if (out_frames[i].left != 0.0f) {
					any_nonzero = true;
				}
			}
			CHECK(any_nonzero);
			memdelete(result.graph);
		}
	}
}

TEST_CASE("[Symphony][Compiler] DelayLine+FDN compile at 96 kHz within budget") {
	GraphDescription desc;
	NodeDesc osc;
	osc.id = 1;
	osc.type_name = "Oscillator";
	desc.nodes.push_back(osc);

	NodeDesc delay;
	delay.id = 2;
	delay.type_name = "DelayLine";
	delay.params.insert("max_delay_ms", 2000.0f);
	delay.params.insert("delay_ms", 100.0f);
	desc.nodes.push_back(delay);

	NodeDesc fdn;
	fdn.id = 3;
	fdn.type_name = "FDNReverb";
	fdn.params.insert("num_lines", 8.0f);
	fdn.params.insert("max_delay_ms", 200.0f);
	desc.nodes.push_back(fdn);

	NodeDesc out;
	out.id = 4;
	out.type_name = "GraphOutput";
	desc.nodes.push_back(out);

	ConnectionDesc c0{ 1, 0, 2, 0, false };
	ConnectionDesc c1{ 2, 0, 3, 0, false };
	ConnectionDesc c2{ 3, 0, 4, 0, false };
	desc.connections.push_back(c0);
	desc.connections.push_back(c1);
	desc.connections.push_back(c2);

	GraphCompiler::CompileResult result = GraphCompiler::compile(desc, 96000.0f);
	REQUIRE(result.success());
	CHECK(result.arena_bytes <= SymphonyMemoryBudget::DEFAULT_PER_GRAPH_BYTES);
	memdelete(result.graph);
}

// --- Fault injection ---

class SymphonyNullFactoryOp : public SymphonyOperator {
public:
	virtual void bind_pins(void **, void **) override {}
	virtual void execute(int32_t) override {}

	static size_t calculate_arena_bytes(const HashMap<StringName, Variant> &, float) { return 0; }

	static SymphonyOperator *create(ArenaAllocator &, const HashMap<StringName, Variant> &, float) {
		return nullptr;
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "NullFactoryOp";
		desc.category = "Generators";
		desc.outputs.push_back({ "output", SymphonyPinType::AUDIO, false });
		desc.state_size = sizeof(SymphonyNullFactoryOp);
		desc.state_align = alignof(SymphonyNullFactoryOp);
		desc.create_fn = &SymphonyNullFactoryOp::create;
		desc.extra_arena_bytes_fn = &SymphonyNullFactoryOp::calculate_arena_bytes;
		desc.silence_behavior = SilenceBehavior::ALWAYS_PROCESS;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}
};

class SymphonyUnderReportOp : public SymphonyOperator {
public:
	float *extra = nullptr;
	virtual void bind_pins(void **, void **) override {}
	virtual void execute(int32_t) override {}

	// Lies: claims no extra bytes, but create allocates a large buffer.
	static size_t calculate_arena_bytes(const HashMap<StringName, Variant> &, float) { return 0; }

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &, float) {
		void *mem = p_arena.alloc(sizeof(SymphonyUnderReportOp), alignof(SymphonyUnderReportOp));
		if (!mem) {
			return nullptr;
		}
		float *extra = (float *)p_arena.alloc(sizeof(float) * 4096, 32);
		if (!extra) {
			return nullptr; // Compiler rewinds mark; no constructed object yet.
		}
		auto *op = new (mem) SymphonyUnderReportOp();
		op->extra = extra;
		return op;
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "UnderReportOp";
		desc.category = "Generators";
		desc.outputs.push_back({ "output", SymphonyPinType::AUDIO, false });
		desc.state_size = sizeof(SymphonyUnderReportOp);
		desc.state_align = alignof(SymphonyUnderReportOp);
		desc.create_fn = &SymphonyUnderReportOp::create;
		desc.extra_arena_bytes_fn = &SymphonyUnderReportOp::calculate_arena_bytes;
		desc.silence_behavior = SilenceBehavior::ALWAYS_PROCESS;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}
};

TEST_CASE("[Symphony][Compiler] Null factory returns structured error without crash") {
	SymphonyNullFactoryOp::register_operator();

	GraphDescription desc;
	NodeDesc n;
	n.id = 1;
	n.type_name = "NullFactoryOp";
	desc.nodes.push_back(n);
	NodeDesc out;
	out.id = 2;
	out.type_name = "GraphOutput";
	desc.nodes.push_back(out);
	ConnectionDesc conn{ 1, 0, 2, 0, false };
	desc.connections.push_back(conn);

	SymphonyMemoryBudget *budget = SymphonyMemoryBudget::get_singleton();
	const size_t before = budget ? budget->get_snapshot().reserved_bytes : 0;

	GraphCompiler::CompileResult result = GraphCompiler::compile(desc, 48000.0f);
	CHECK_FALSE(result.success());
	REQUIRE(result.errors.size() > 0);
	CHECK(result.errors[0].findn("Failed to create") != -1);
	if (budget) {
		CHECK(budget->get_snapshot().reserved_bytes == before);
	}
}

TEST_CASE("[Symphony][Compiler] Under-reported extra bytes fail cleanly") {
	SymphonyUnderReportOp::register_operator();

	GraphDescription desc;
	NodeDesc n;
	n.id = 1;
	n.type_name = "UnderReportOp";
	desc.nodes.push_back(n);
	NodeDesc out;
	out.id = 2;
	out.type_name = "GraphOutput";
	desc.nodes.push_back(out);
	ConnectionDesc conn{ 1, 0, 2, 0, false };
	desc.connections.push_back(conn);

	SymphonyMemoryBudget *budget = SymphonyMemoryBudget::get_singleton();
	const size_t before = budget ? budget->get_snapshot().reserved_bytes : 0;

	GraphCompiler::CompileResult result = GraphCompiler::compile(desc, 48000.0f);
	// Exact planner under-sizes arena → create returns null → structured error.
	CHECK_FALSE(result.success());
	REQUIRE(result.errors.size() > 0);
	if (budget) {
		CHECK(budget->get_snapshot().reserved_bytes == before);
	}
}

TEST_CASE("[Symphony][Compiler] SharedPCM charges unique entries once") {
	SharedPCMCache *cache = SharedPCMCache::get_singleton();
	SymphonyMemoryBudget *budget = SymphonyMemoryBudget::get_singleton();
	REQUIRE(cache != nullptr);
	REQUIRE(budget != nullptr);

	const size_t shared_before = budget->get_shared_pcm_bytes();
	Vector<float> samples;
	samples.resize(128);
	for (int i = 0; i < 128; i++) {
		samples.write[i] = (float)i;
	}

	const StringName key = StringName("test_shared_pcm_unique");
	const float *p0 = cache->acquire(key, samples.ptr(), samples.size());
	REQUIRE(p0 != nullptr);
	const size_t expected = sizeof(float) * (size_t)samples.size();
	CHECK(budget->get_shared_pcm_bytes() == shared_before + expected);

	const float *p1 = cache->acquire(key, samples.ptr(), samples.size());
	REQUIRE(p1 == p0);
	CHECK(budget->get_shared_pcm_bytes() == shared_before + expected);

	cache->release(key);
	CHECK(budget->get_shared_pcm_bytes() == shared_before + expected);
	cache->release(key);
	CHECK(budget->get_shared_pcm_bytes() == shared_before);
}

static GraphDescription _make_constant_output_graph() {
	GraphDescription desc;
	desc.smooth_parameters = false;
	NodeDesc constant;
	constant.id = 1;
	constant.type_name = "Constant";
	constant.params.insert("value", 0.25f);
	desc.nodes.push_back(constant);
	NodeDesc out;
	out.id = 2;
	out.type_name = "GraphOutput";
	desc.nodes.push_back(out);
	ConnectionDesc conn;
	conn.from_node = 1;
	conn.from_pin = 0;
	conn.to_node = 2;
	conn.to_pin = 0;
	desc.connections.push_back(conn);
	return desc;
}

TEST_CASE("[Symphony][Compiler] Negative pin is rejected before indexing") {
	GraphDescription desc = _make_constant_output_graph();
	desc.connections.write[0].from_pin = -1;

	GraphCompiler::CompileResult result = GraphCompiler::compile(desc, 44100.0f, "res://bad_pin.tres");
	CHECK_FALSE(result.success());
	REQUIRE(result.diagnostics.size() >= 1);
	CHECK(result.diagnostics[0].connection_index == 0);
	CHECK(result.diagnostics[0].resource_path == "res://bad_pin.tres");

	Ref<AudioStreamSymphony> stream;
	stream.instantiate();
	stream->set_graph_description(desc);
	Dictionary validated = stream->validate_tier_compile(0);
	CHECK_FALSE(bool(validated["ok"]));
	Array errors = validated["errors"];
	CHECK(errors.size() >= 1);
	Dictionary entry = errors[0];
	CHECK(int(entry["connection_index"]) == 0);
	CHECK(validated.has("warnings"));
	CHECK(validated.has("arena_bytes"));
	CHECK(validated.has("cost_units"));
}

TEST_CASE("[Symphony][Compiler] Duplicate input connections are rejected") {
	GraphDescription desc = _make_constant_output_graph();
	desc.connections.push_back(desc.connections[0]);
	GraphCompiler::CompileResult result = GraphCompiler::compile(desc, 44100.0f, "res://duplicate.tres");
	CHECK_FALSE(result.success());
	bool saw_duplicate = false;
	for (const GraphCompiler::CompileDiagnostic &diagnostic : result.diagnostics) {
		if (diagnostic.connection_index == 1) {
			saw_duplicate = true;
		}
	}
	CHECK(saw_duplicate);
}

TEST_CASE("[Symphony][Compiler] Flattening keeps top-level quality settings") {
	GraphDescription desc = _make_constant_output_graph();
	desc.smooth_parameters = false;
	desc.smooth_time_ms = 12.5f;
	desc.anti_alias_staircase = true;
	GraphFlattener::FlattenResult flat = GraphFlattener::flatten(desc, "res://quality.tres");
	CHECK(flat.success());
	CHECK_FALSE(flat.graph.smooth_parameters);
	CHECK(flat.graph.smooth_time_ms == doctest::Approx(12.5f));
	CHECK(flat.graph.anti_alias_staircase);

	Ref<AudioStreamSymphony> stream;
	stream.instantiate();
	stream->set_graph_description(desc);
	Dictionary validated = stream->validate_tier_compile(0);
	CHECK(bool(validated["ok"]));
	CHECK((float)validated["cost_units"] > 0.0f);
}

// A stateless node (Mix) skipped while its input was silent must wake up
// when that input becomes active again, or everything downstream stays muted.
TEST_CASE("[Symphony][Compiler] Silence skip does not latch stateless nodes") {
	GraphDescription desc;
	auto add_node = [&](int p_id, const char *p_type) -> NodeDesc & {
		NodeDesc node;
		node.id = p_id;
		node.type_name = p_type;
		desc.nodes.push_back(node);
		return desc.nodes.write[desc.nodes.size() - 1];
	};
	auto connect = [&](int p_from, int p_from_pin, int p_to, int p_to_pin) {
		ConnectionDesc conn;
		conn.from_node = p_from;
		conn.from_pin = p_from_pin;
		conn.to_node = p_to;
		conn.to_pin = p_to_pin;
		desc.connections.push_back(conn);
	};

	NodeDesc &trigger = add_node(0, "TriggerInput");
	trigger.params.insert("trigger_name", "hit");
	trigger.params.insert("auto_trigger_on_play", 0.0f);
	NodeDesc &osc = add_node(1, "Oscillator");
	osc.params.insert("frequency", 440.0f);
	osc.params.insert("waveform", 0.0f);
	NodeDesc &adsr = add_node(2, "ADSR");
	adsr.params.insert("attack", 0.001f);
	adsr.params.insert("decay", 0.2f);
	adsr.params.insert("sustain", 0.0f);
	adsr.params.insert("release", 0.05f);
	NodeDesc &mix = add_node(3, "Mix");
	mix.params.insert("mix", 0.0f);
	NodeDesc &gain = add_node(4, "Gain");
	gain.params.insert("gain", 1.0f);
	add_node(5, "GraphOutput");

	connect(1, 0, 2, 0);
	connect(0, 0, 2, 1);
	connect(2, 0, 3, 0);
	connect(3, 0, 4, 0);
	connect(4, 0, 5, 0);

	Ref<AudioStreamSymphony> stream;
	stream.instantiate();
	stream->set_graph_description(desc);

	// Silent for the first 0.1 s (no trigger yet), then struck.
	Array triggers;
	Dictionary hit;
	hit["name"] = StringName("hit");
	hit["time"] = 0.1;
	triggers.push_back(hit);
	Dictionary rendered = stream->render_offline(0.4, 1, Dictionary(), triggers, String());
	REQUIRE(bool(rendered["ok"]));
	CHECK(float(rendered["peak"]) > 0.5f);
}

TEST_CASE("[Symphony][Compiler] Shared helpers render_graph / tone_level / alias_ratio") {
	const float sr = 48000.0f;
	const int frames = 4096;
	// Exact DFT bin: 32 cycles → 375 Hz, so the fundamental lands on-grid.
	const float f0 = 32.0f * sr / (float)frames;
	Vector<float> buf = render_graph(make_sine_graph(f0), frames, sr);
	REQUIRE(buf.size() == frames);

	const float tone_db = tone_level_db(buf, sr, f0);
	const float noise_db = tone_level_db(buf, sr, f0 * 2.5f);
	CHECK(tone_db > -6.0f); // near 0 dBFS for unit sine
	CHECK(noise_db < tone_db - 20.0f);

	// Pure sine: off-grid energy should be well below the fundamental.
	const float alias_db = alias_ratio_db(buf, sr, f0, 1);
	CHECK(alias_db < -20.0f);
}

} // namespace TestSymphonyCompiler
