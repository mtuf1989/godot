/**************************************************************************/
/*  test_symphony_compiler.cpp                                            */
/*  Suite: [Symphony][Compiler] — graph compile, arena sizing, packages.  */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_compiler)

#include "modules/symphony/core/symphony_operator_registry.h"

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
}

} // namespace TestSymphonyCompiler
