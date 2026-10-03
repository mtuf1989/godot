#include "tests/test_macros.h"
TEST_FORCE_LINK(test_agent_tools)
#include "modules/modules_enabled.gen.h"
#ifdef MODULE_AGENT_TOOLS_ENABLED
#include "modules/agent_tools/agent_tools.h"
#include "modules/agent_tools/agent_report.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "core/os/thread.h"
#include "tests/test_utils.h"

namespace TestAgentTools {
void worker_error(void *) {
	ERR_PRINT("agent worker diagnostic");
}
void reentrant_error(void *, const char *, const char *, int, const char *, const char *, bool, ErrorHandlerType) {
	ERR_PRINT("nested diagnostic");
}

TEST_CASE("[AgentTools] Actual native registrations and report lifecycle") {
	const Dictionary caps = AgentTools::capabilities();
	const Dictionary classes = caps["classes"];
	CHECK(bool(classes["Node"]));
	const String path = TestUtils::get_temp_path("agent-tools.jsonl");
	REQUIRE(AgentTools::begin_report(path) == OK);
	CHECK(AgentTools::begin_report(path) == ERR_ALREADY_IN_USE);
	ERR_PRINT_OFF;
	WARN_PRINT("agent warning");
	Thread worker;
	worker.start(worker_error, nullptr);
	worker.wait_to_finish();
	ErrorHandlerList nested;
	nested.errfunc = reentrant_error;
	add_error_handler(&nested);
	ERR_PRINT("outer diagnostic");
	remove_error_handler(&nested);
	ERR_PRINT_ON;
	AgentTools::record_iteration(false, true, 1);
	AgentTools::finish_report();
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
	REQUIRE(file.is_valid());
	const String text = file->get_as_text();
	CHECK(text.contains("agent warning"));
	CHECK(text.contains("agent worker diagnostic"));
	CHECK(text.contains("outer diagnostic"));
	CHECK(text.contains("\"worker_thread\":true"));
	CHECK(text.contains("\"observed_iterations\":1"));
	CHECK(text.contains("\"lost_events\":1"));
	CHECK(text.contains("\"evidence_complete\":false"));
	OS::get_singleton()->set_exit_code(0);
	REQUIRE(AgentTools::begin_report(path) == OK);
	AgentTools::finish_report();
}

TEST_CASE("[AgentTools] Bounded queue reports evidence loss") {
	const String path = TestUtils::get_temp_path("agent-tools-overflow.jsonl");
	REQUIRE(AgentTools::begin_report(path) == OK);
	ERR_PRINT_OFF;
	for (int i = 0; i < 300; i++) {
		ERR_PRINT("overflow diagnostic");
	}
	ERR_PRINT_ON;
	AgentTools::finish_report();
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
	REQUIRE(file.is_valid());
	const String text = file->get_as_text();
	CHECK(text.contains("\"lost_events\":44"));
	CHECK(text.contains("\"evidence_complete\":false"));
	OS::get_singleton()->set_exit_code(0);
}
}
#endif
