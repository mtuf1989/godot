/**************************************************************************/
/*  test_symphony_playback.cpp                                            */
/*  Suite: [Symphony][Playback] — stream playback, swaps, retirement.     */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_playback)

#include "modules/symphony/core/symphony_graph_compiler.h"
#include "modules/symphony/core/symphony_graph_description.h"
#include "modules/symphony/core/symphony_graph_package_retirement.h"
#include "modules/symphony/core/symphony_prepared_graph_package.h"

namespace TestSymphonyPlayback {

static GraphDescription _make_io_graph() {
	GraphDescription desc;

	NodeDesc gin;
	gin.id = 1;
	gin.type_name = "GraphInput";
	gin.params.insert("parameter_name", "freq");
	gin.params.insert("default_value", 440.0f);
	desc.nodes.push_back(gin);

	NodeDesc tin;
	tin.id = 2;
	tin.type_name = "TriggerInput";
	tin.params.insert("trigger_name", "gate");
	desc.nodes.push_back(tin);

	NodeDesc osc;
	osc.id = 3;
	osc.type_name = "Oscillator";
	osc.params.insert("frequency", 440.0f);
	osc.params.insert("waveform", 0.0f);
	desc.nodes.push_back(osc);

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
	c1.from_node = 3;
	c1.from_pin = 0;
	c1.to_node = 4;
	c1.to_pin = 0;
	desc.connections.push_back(c1);

	return desc;
}

TEST_CASE("[Symphony][Playback] PreparedGraphPackage builds sorted routes") {
	GraphCompiler::CompileResult result = GraphCompiler::compile(_make_io_graph(), 48000.0f);
	REQUIRE(result.success());
	REQUIRE(result.graph != nullptr);

	PreparedGraphPackage *pkg = PreparedGraphPackage::create_from_graph(result.graph, result.arena_bytes, result.total_package_bytes);
	REQUIRE(pkg != nullptr);
	CHECK(pkg->graph_output != nullptr);
	CHECK(pkg->param_routes.size() >= 1);
	CHECK(pkg->find_param(StringName("freq")) != nullptr);
	CHECK(pkg->find_param(StringName("missing")) == nullptr);
	CHECK(pkg->find_trigger(StringName("gate")) != nullptr);
	CHECK(pkg->find_trigger(StringName("missing")) == nullptr);

	PreparedGraphPackage::destroy(pkg);
}

TEST_CASE("[Symphony][Playback] GraphPackageRetirement drain destroys packages") {
	GraphCompiler::CompileResult result = GraphCompiler::compile(_make_io_graph(), 48000.0f);
	REQUIRE(result.success());

	PreparedGraphPackage *pkg = PreparedGraphPackage::create_from_graph(result.graph);
	REQUIRE(pkg != nullptr);

	const uint32_t before = GraphPackageRetirement::get_pending_count();
	GraphPackageRetirement::retire(pkg);
	CHECK(GraphPackageRetirement::get_pending_count() == before + 1);

	GraphPackageRetirement::drain();
	CHECK(GraphPackageRetirement::get_pending_count() == before);
}

} // namespace TestSymphonyPlayback
