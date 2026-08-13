/**************************************************************************/
/*  symphony_test_helpers.h                                               */
/**************************************************************************/

#pragma once

#include "modules/symphony/core/symphony_operator_registry.h"
#include "modules/symphony/core/symphony_pin_types.h"
#include "tests/test_macros.h"

#include <cstddef>

namespace SymphonyTestHelpers {

// Bind helpers that size input/output pointer arrays from the operator descriptor.
// Prevents out-of-bounds bind_pins access when fixtures omit optional pins.
inline void expect_descriptor_pins(const StringName &p_type_name, int p_expected_inputs, int p_expected_outputs) {
	const OperatorDescriptor *desc = OperatorRegistry::get_singleton()->find(p_type_name);
	REQUIRE(desc != nullptr);
	CHECK(desc->inputs.size() == p_expected_inputs);
	CHECK(desc->outputs.size() == p_expected_outputs);
}

} // namespace SymphonyTestHelpers
