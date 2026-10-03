#include "tests/test_macros.h"
TEST_FORCE_LINK(test_agent_tools)
#include "modules/modules_enabled.gen.h"
#ifdef MODULE_AGENT_TOOLS_ENABLED
#include "modules/agent_tools/agent_tools.h"
#include "modules/agent_tools/agent_report.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/io/resource_loader.h"
#include "core/os/os.h"
#include "core/os/thread.h"
#include "tests/test_utils.h"
#include <string>

namespace TestAgentTools {
void worker_resource(void *p_path) {
	const String &path = *static_cast<String *>(p_path);
	Ref<Resource> resource = ResourceLoader::load(path, "", ResourceFormatLoader::CACHE_MODE_IGNORE);
}
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
TEST_CASE("[AgentTools] Fault policy distinguishes warnings and application errors") {
	const String path = TestUtils::get_temp_path("agent-fault-policy.jsonl");
	REQUIRE(AgentTools::begin_report(path) == OK);
	AgentTools::enable_script_fault_exit();
	ERR_PRINT_OFF;
	WARN_PRINT("nonfatal warning");
	ERR_PRINT("nonfatal handled application error");
	CHECK_FALSE(AgentTools::poll_failure_exit());
	_err_print_error("fixture", "res://fault.gd", 4, "Unhandled fixture fault", false, ERR_HANDLER_SCRIPT);
	CHECK(AgentTools::poll_failure_exit());
	ERR_PRINT_ON;
	AgentTools::finish_report();
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
	REQUIRE(file.is_valid());
	CHECK(file->get_as_text().contains("\"reason\":\"script_fault\""));
	OS::get_singleton()->set_exit_code(0);
	REQUIRE(AgentTools::begin_report(path) == OK);
	CHECK_FALSE(AgentTools::poll_failure_exit());
	AgentTools::finish_report();
}

TEST_CASE("[AgentTools] Strict resource assignments and missing-resource preservation") {
	const String source = TestUtils::get_temp_path("agent-strict-invalid.tres");
	const String report = TestUtils::get_temp_path("agent-strict.jsonl");
	{
		Ref<FileAccess> file = FileAccess::open(source, FileAccess::WRITE);
		REQUIRE(file.is_valid());
		file->store_string("[gd_resource type=\"Resource\" format=3]\n[resource]\nposiiton = Vector2(1, 2)\n");
	}
	Ref<Resource> baseline = ResourceLoader::load(source, "", ResourceFormatLoader::CACHE_MODE_IGNORE);
	REQUIRE(baseline.is_valid());
	REQUIRE(AgentTools::begin_report(report) == OK);
	AgentTools::enable_strict_resources();
	String worker_path = source;
	Thread worker;
	worker.start(worker_resource, &worker_path);
	worker.wait_to_finish();
	CHECK(AgentTools::poll_failure_exit());
	AgentTools::finish_report();
	Ref<FileAccess> evidence = FileAccess::open(report, FileAccess::READ);
	REQUIRE(evidence.is_valid());
	CHECK(evidence->get_as_text().contains("\"property\":\"posiiton\""));
	CHECK(evidence->get_as_text().contains("\"worker_thread\":true"));
	OS::get_singleton()->set_exit_code(0);
	{
		Ref<FileAccess> file = FileAccess::open(source, FileAccess::WRITE);
		file->store_string("[gd_resource type=\"GradientTexture2D\" load_steps=2 format=3]\n"
				"[sub_resource type=\"UnavailableAgentFixture\" id=\"Missing\"]\ncustom = 4\n"
				"[resource]\ngradient = SubResource(\"Missing\")\n");
	}
	const bool previous = ResourceLoader::is_creating_missing_resources_if_class_unavailable_enabled();
	ResourceLoader::set_create_missing_resources_if_class_unavailable(true);
	REQUIRE(AgentTools::begin_report(report) == OK);
	AgentTools::enable_strict_resources();
	Ref<Resource> missing = ResourceLoader::load(source, "", ResourceFormatLoader::CACHE_MODE_IGNORE);
	ResourceLoader::set_create_missing_resources_if_class_unavailable(previous);
	REQUIRE(missing.is_valid());
	CHECK_FALSE(AgentTools::poll_failure_exit());
	AgentTools::finish_report();
	CHECK(OS::get_singleton()->get_exit_code() == 0);
}

TEST_CASE("[AgentTools] Truncation invalidates evidence and exits nonzero") {
	const String path = TestUtils::get_temp_path("agent-truncation.jsonl");
	REQUIRE(AgentTools::begin_report(path) == OK);
	const std::string message(5000, 'x');
	ERR_PRINT_OFF;
	_err_print_error("fixture", "res://fixture.gd", 1, message.c_str());
	ERR_PRINT_ON;
	AgentTools::finish_report();
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
	REQUIRE(file.is_valid());
	const String text = file->get_as_text();
	CHECK(text.contains("\"truncated_events\":1"));
	CHECK(text.contains("\"evidence_complete\":false"));
	CHECK(OS::get_singleton()->get_exit_code() == 1);
	OS::get_singleton()->set_exit_code(0);
}

}
#endif
