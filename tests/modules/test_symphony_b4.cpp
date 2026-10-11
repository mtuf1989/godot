/**************************************************************************/
/*  test_symphony_b4.cpp                                                  */
/*  Suite: [Symphony][B4] — migration layout, pristine, trigger clamp.    */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_b4)

#include "modules/symphony/core/symphony_graph_compiler.h"
#include "modules/symphony/core/symphony_graph_description.h"
#include "modules/symphony/core/symphony_prepared_graph_package.h"
#include "modules/symphony/core/symphony_trigger.h"
#include "modules/symphony/nodes/filters/symphony_waveshaper.h"
#include "modules/symphony/nodes/io/symphony_graph_output.h"
#include "modules/symphony/runtime/symphony_note_pool.h"
#include "servers/audio/audio_frame.h"
#include "tests/modules/symphony_test_helpers.h"

#include "core/math/math_funcs.h"

namespace TestSymphonyB4 {

static GraphDescription _fdn_graph(float p_max_delay_ms, float p_num_lines) {
	GraphDescription desc;
	desc.smooth_parameters = false;
	NodeDesc osc;
	osc.id = 1;
	osc.type_name = "Oscillator";
	osc.params["frequency"] = 220.0f;
	desc.nodes.push_back(osc);
	NodeDesc fdn;
	fdn.id = 2;
	fdn.type_name = "FDNReverb";
	fdn.params["num_lines"] = p_num_lines;
	fdn.params["max_delay_ms"] = p_max_delay_ms;
	desc.nodes.push_back(fdn);
	NodeDesc out;
	out.id = 3;
	out.type_name = "GraphOutput";
	desc.nodes.push_back(out);
	ConnectionDesc c0{ 1, 0, 2, 0, false };
	ConnectionDesc c1{ 2, 0, 3, 0, false };
	desc.connections.push_back(c0);
	desc.connections.push_back(c1);
	return desc;
}

static GraphDescription _grain_graph(float p_capture_seconds) {
	GraphDescription desc;
	desc.smooth_parameters = false;
	NodeDesc osc;
	osc.id = 1;
	osc.type_name = "Oscillator";
	desc.nodes.push_back(osc);
	NodeDesc grain;
	grain.id = 2;
	grain.type_name = "GrainCloud";
	grain.params["capture_seconds"] = p_capture_seconds;
	desc.nodes.push_back(grain);
	NodeDesc out;
	out.id = 3;
	out.type_name = "GraphOutput";
	desc.nodes.push_back(out);
	ConnectionDesc c0{ 1, 0, 2, 0, false };
	ConnectionDesc c1{ 2, 0, 3, 0, false };
	desc.connections.push_back(c0);
	desc.connections.push_back(c1);
	return desc;
}

static void _run_finite(CompiledGraph *p_graph, int p_blocks) {
	REQUIRE(p_graph != nullptr);
	AudioFrame frames[SYMPHONY_MICRO_BLOCK_SIZE];
	SymphonyGraphOutput *gout = nullptr;
	for (int32_t i = 0; i < p_graph->operator_count; i++) {
		gout = dynamic_cast<SymphonyGraphOutput *>(p_graph->operators[i]);
		if (gout) {
			break;
		}
	}
	REQUIRE(gout != nullptr);
	for (int b = 0; b < p_blocks; b++) {
		for (int i = 0; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
			frames[i] = AudioFrame(0, 0);
		}
		gout->set_output(frames, 0);
		p_graph->execute(SYMPHONY_MICRO_BLOCK_SIZE);
		for (int i = 0; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
			CHECK(Math::is_finite(frames[i].left));
			CHECK(Math::is_finite(frames[i].right));
		}
	}
}

TEST_CASE("[Symphony][B4] FDN migration across room sizes and rates is finite") {
	const float rates[] = { 44100.0f, 96000.0f };
	const float delays[] = { 40.0f, 120.0f };
	for (float rate : rates) {
		for (float delay : delays) {
			GraphDescription desc = _fdn_graph(delay, 4.0f);
			GraphCompiler::CompileResult a = GraphCompiler::compile(desc, rate);
			GraphCompiler::CompileResult b = GraphCompiler::compile(desc, rate);
			REQUIRE(a.success());
			REQUIRE(b.success());
			PreparedGraphPackage *from = PreparedGraphPackage::create_from_graph(a.graph);
			PreparedGraphPackage *to = PreparedGraphPackage::create_from_graph(b.graph);
			_run_finite(a.graph, 8);
			PreparedGraphPackage::migrate_compatible_state(from, to);
			_run_finite(b.graph, 8);
			PreparedGraphPackage::destroy(from);
			PreparedGraphPackage::destroy(to);
		}
	}
	GraphCompiler::CompileResult small = GraphCompiler::compile(_fdn_graph(40.0f, 4.0f), 48000.0f);
	GraphCompiler::CompileResult large = GraphCompiler::compile(_fdn_graph(180.0f, 8.0f), 48000.0f);
	REQUIRE(small.success());
	REQUIRE(large.success());
	PreparedGraphPackage *from = PreparedGraphPackage::create_from_graph(small.graph);
	PreparedGraphPackage *to = PreparedGraphPackage::create_from_graph(large.graph);
	const auto *fp_a = from->find_fingerprint(2);
	const auto *fp_b = to->find_fingerprint(2);
	REQUIRE(fp_a != nullptr);
	REQUIRE(fp_b != nullptr);
	CHECK(fp_a->structural_hash != fp_b->structural_hash);
	PreparedGraphPackage::migrate_compatible_state(from, to);
	_run_finite(large.graph, 4);
	PreparedGraphPackage::destroy(from);
	PreparedGraphPackage::destroy(to);
}

TEST_CASE("[Symphony][B4] GrainCloud layout key follows capture_seconds") {
	GraphCompiler::CompileResult a = GraphCompiler::compile(_grain_graph(1.0f), 48000.0f);
	GraphCompiler::CompileResult b = GraphCompiler::compile(_grain_graph(4.0f), 48000.0f);
	REQUIRE(a.success());
	REQUIRE(b.success());
	PreparedGraphPackage *from = PreparedGraphPackage::create_from_graph(a.graph);
	PreparedGraphPackage *to = PreparedGraphPackage::create_from_graph(b.graph);
	CHECK(from->find_fingerprint(2)->structural_hash != to->find_fingerprint(2)->structural_hash);
	PreparedGraphPackage::migrate_compatible_state(from, to);
	_run_finite(b.graph, 4);
	PreparedGraphPackage::destroy(from);
	PreparedGraphPackage::destroy(to);
}

TEST_CASE("[Symphony][B4] PhaseVocoder fft_size 8192 compiles and runs") {
	GraphDescription desc;
	desc.smooth_parameters = false;
	NodeDesc osc;
	osc.id = 1;
	osc.type_name = "Oscillator";
	desc.nodes.push_back(osc);
	NodeDesc pv;
	pv.id = 2;
	pv.type_name = "PhaseVocoder";
	pv.params["fft_size"] = 8192.0f;
	pv.params["overlap"] = 8.0f;
	desc.nodes.push_back(pv);
	NodeDesc out;
	out.id = 3;
	out.type_name = "GraphOutput";
	desc.nodes.push_back(out);
	ConnectionDesc c0{ 1, 0, 2, 0, false };
	ConnectionDesc c1{ 2, 0, 3, 0, false };
	desc.connections.push_back(c0);
	desc.connections.push_back(c1);
	GraphCompiler::CompileResult result = GraphCompiler::compile(desc, 48000.0f);
	REQUIRE(result.success());
	_run_finite(result.graph, 4);
	memdelete(result.graph);
}

TEST_CASE("[Symphony][B4] Custom Waveshaper compiles in SFX and note pool") {
	GraphDescription desc;
	desc.smooth_parameters = false;
	NodeDesc osc;
	osc.id = 1;
	osc.type_name = "Oscillator";
	desc.nodes.push_back(osc);
	NodeDesc shaper;
	shaper.id = 2;
	shaper.type_name = "Waveshaper";
	PackedFloat32Array table;
	table.resize(128);
	for (int i = 0; i < 128; i++) {
		table.write[i] = Math::sin((float)i / 127.0f * Math::TAU) * 0.5f;
	}
	shaper.params["table_data"] = table;
	shaper.params["table_size"] = 128.0f;
	desc.nodes.push_back(shaper);
	NodeDesc out;
	out.id = 3;
	out.type_name = "GraphOutput";
	desc.nodes.push_back(out);
	ConnectionDesc c0{ 1, 0, 2, 0, false };
	ConnectionDesc c1{ 2, 0, 3, 0, false };
	desc.connections.push_back(c0);
	desc.connections.push_back(c1);

	GraphCompiler::CompileResult sfx = GraphCompiler::compile(desc, 48000.0f);
	REQUIRE(sfx.success());
	_run_finite(sfx.graph, 2);
	memdelete(sfx.graph);

	GraphDescription voice;
	voice.smooth_parameters = false;
	NodeDesc note;
	note.id = 1;
	note.type_name = "MusicNoteInput";
	voice.nodes.push_back(note);
	NodeDesc osc2;
	osc2.id = 2;
	osc2.type_name = "Oscillator";
	voice.nodes.push_back(osc2);
	voice.nodes.push_back(shaper);
	voice.nodes.write[voice.nodes.size() - 1].id = 3;
	NodeDesc vout;
	vout.id = 4;
	vout.type_name = "GraphOutput";
	voice.nodes.push_back(vout);
	ConnectionDesc n0{ 1, 0, 2, 0, false };
	ConnectionDesc n1{ 2, 0, 3, 0, false };
	ConnectionDesc n2{ 3, 0, 4, 0, false };
	ConnectionDesc n3{ 1, 6, 3, 1, false };
	voice.connections.push_back(n0);
	voice.connections.push_back(n1);
	voice.connections.push_back(n2);
	(void)n3;
	SymphonyNotePool pool;
	REQUIRE(pool.prepare(voice, 48000.0f, 1, 0xB4ULL));
	AudioFrame frames[SYMPHONY_MICRO_BLOCK_SIZE];
	pool.note_on(1, 60, 0.8f, 0, 0, 1);
	pool.render(frames, SYMPHONY_MICRO_BLOCK_SIZE, 0);
	for (int i = 0; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
		CHECK(Math::is_finite(frames[i].left));
	}
}

TEST_CASE("[Symphony][B4] SFX compile skips pristine and shrinks arena") {
	GraphDescription desc = SymphonyTestHelpers::make_sine_graph();
	GraphCompiler::CompileOptions note_opts;
	note_opts.capture_pristine = true;
	GraphCompiler::CompileResult sfx = GraphCompiler::compile(desc, 48000.0f);
	GraphCompiler::CompileResult note = GraphCompiler::compile(desc, 48000.0f, String(), 0, nullptr, nullptr, note_opts);
	REQUIRE(sfx.success());
	REQUIRE(note.success());
	CHECK(sfx.arena_bytes < note.arena_bytes);
	memdelete(sfx.graph);
	memdelete(note.graph);
}

TEST_CASE("[Symphony][B4] TriggerBuffer push clamps offsets") {
	TriggerBuffer buf;
	CHECK(buf.push(-4, 1.0f));
	CHECK(buf.events[0].sample_offset == 0);
	CHECK(buf.push(SYMPHONY_MAX_TRIGGERS_PER_BLOCK + 9, 0.5f));
	CHECK(buf.events[1].sample_offset == SYMPHONY_MAX_TRIGGERS_PER_BLOCK - 1);
	CHECK(buf.push(3, 0.25f));
	CHECK(buf.events[2].sample_offset == 3);
}

} // namespace TestSymphonyB4
