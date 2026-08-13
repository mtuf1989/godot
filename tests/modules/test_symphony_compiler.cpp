/**************************************************************************/
/*  test_symphony_compiler.cpp                                            */
/*  Suite: [Symphony][Compiler] — graph compile, arena sizing, packages.  */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_compiler)

#include "modules/symphony/core/symphony_graph_compiler.h"
#include "modules/symphony/core/symphony_graph_description.h"
#include "modules/symphony/core/symphony_memory_budget.h"
#include "modules/symphony/core/symphony_operator_registry.h"
#include "modules/symphony/nodes/delay/symphony_delay_line.h"

namespace TestSymphonyCompiler {

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
	const size_t needed = ok.arena_bytes;
	memdelete(ok.graph);

	budget->set_per_graph_limit_bytes(needed > 0 ? needed - 1 : 0);

	GraphCompiler::CompileResult result = GraphCompiler::compile(desc, 48000.0f);
	CHECK_FALSE(result.success());
	REQUIRE(result.errors.size() > 0);
	CHECK(result.errors[0].findn("budget") != -1);

	budget->set_per_graph_limit_bytes(old_per);
	budget->set_global_limit_bytes(old_global);
}

} // namespace TestSymphonyCompiler
