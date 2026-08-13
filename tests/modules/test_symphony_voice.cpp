/**************************************************************************/
/*  test_symphony_voice.cpp                                               */
/*  Suite: [Symphony][Voice] — voice pool, stealing, RTPC, triggers.      */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_voice)

namespace TestSymphonyVoice {

// Populated in M2 (steal accounting, RTPC handles, trigger queues).
TEST_CASE("[Symphony][Voice] suite scaffold present") {
	CHECK(true);
}

} // namespace TestSymphonyVoice
