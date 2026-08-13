/**************************************************************************/
/*  test_symphony_serialization.cpp                                       */
/*  Suite: [Symphony][Serialization] — LOD/feedback .tres round trips.    */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_serialization)

namespace TestSymphonySerialization {

// Populated in M3 (LOD sections, is_feedback, editor round trips).
TEST_CASE("[Symphony][Serialization] suite scaffold present") {
	CHECK(true);
}

} // namespace TestSymphonySerialization
