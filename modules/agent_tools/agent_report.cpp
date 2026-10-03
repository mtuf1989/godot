#include "agent_report.h"
#include "core/error/error_macros.h"
#include "core/io/json.h"
#include "core/os/mutex.h"
#include "core/os/os.h"
#include "core/os/thread.h"
#include <atomic>
#include <cstdio>
#include <cstring>

namespace AgentTools {
namespace {
// Callbacks may run on workers while the engine's global error lock is held.
// Only bounded copies here: no engine error reporting, I/O, or scene mutation.
struct Diagnostic {
	char function[256];
	char file[1024];
	char message[4096];
	int line = 0;
	ErrorHandlerType type = ERR_HANDLER_ERROR;
	bool worker = false;
	bool truncated = false;
};
constexpr uint32_t QUEUE_LIMIT = 256;
Diagnostic pending[QUEUE_LIMIT];
uint32_t pending_count = 0;
uint64_t lost_events = 0;
std::atomic<uint64_t> suppressed_events{0};
std::atomic<bool> collecting{false};
uint64_t script_fault_count = 0;
uint64_t truncated_events = 0;
uint64_t diagnostic_count = 0;
BinaryMutex pending_mutex;
FILE *report = nullptr;
bool write_failed = false;
bool handler_installed = false;
std::atomic<bool> fail_on_script_error{false};
std::atomic<bool> script_fault_pending{false};
ErrorHandlerList handler;
uint64_t iterations = 0;
uint64_t requested = 0;
bool budget_reached = false;
bool project_exit = false;

bool copy_text(char *p_dest, size_t p_size, const char *p_text) {
	int count = snprintf(p_dest, p_size, "%s", p_text ? p_text : "");
	return count < 0 || size_t(count) >= p_size;
}

void on_error(void *, const char *p_function, const char *p_file, int p_line,
		const char *p_error, const char *p_message, bool, ErrorHandlerType p_type) {
	if (p_type == ERR_HANDLER_SCRIPT && fail_on_script_error.load()) {
		script_fault_pending = true;
	}
	if (!collecting.load()) {
		return;
	}
	MutexLock lock(pending_mutex);
	script_fault_count += p_type == ERR_HANDLER_SCRIPT;
	if (pending_count == QUEUE_LIMIT) {
		lost_events++;
		return;
	}
	Diagnostic &event = pending[pending_count++];
	event.truncated = copy_text(event.function, sizeof(event.function), p_function);
	event.truncated |= copy_text(event.file, sizeof(event.file), p_file);
	event.truncated |= copy_text(event.message, sizeof(event.message),
			p_message && *p_message ? p_message : p_error);
	event.line = p_line;
	event.type = p_type;
	event.worker = !Thread::is_main_thread();
	truncated_events += event.truncated;
}

void write_event(Dictionary p_event) {
	p_event["schema_version"] = 1;
	const CharString line = (JSON::stringify(p_event) + "\n").utf8();
	if (fwrite(line.get_data(), 1, line.length(), report) != size_t(line.length())) {
		write_failed = true;
	}
}
} // namespace

void enable_script_fault_exit() {
	fail_on_script_error = true;
	if (!handler_installed) {
		handler.errfunc = on_error;
		add_error_handler(&handler);
		handler_installed = true;
	}
}

bool poll_failure_exit() {
	if (!script_fault_pending.load()) {
		return false;
	}
	OS::get_singleton()->set_exit_code(EXIT_FAILURE);
	drain_report();
	return true;
}

Error begin_report(const String &p_path) {
	if (report) {
		return ERR_ALREADY_IN_USE;
	}
	report = fopen(p_path.utf8().get_data(), "wb");
	if (!report) {
		fprintf(stderr, "Cannot open agent report: %s\n", p_path.utf8().get_data());
		return ERR_CANT_OPEN;
	}
	pending_count = 0;
	suppressed_events = 0;
	script_fault_count = 0;
	collecting = true;
	lost_events = truncated_events = diagnostic_count = iterations = requested = 0;
	budget_reached = project_exit = write_failed = false;
	if (!handler_installed) {
		handler.errfunc = on_error;
		add_error_handler(&handler);
		handler_installed = true;
	}
	return OK;
}

void note_suppressed_diagnostic() {
	if (collecting.load()) {
		suppressed_events.fetch_add(1);
	}
}

void drain_report() {
	if (!report) {
		return;
	}
	// Release the callback mutex before using Variant/JSON or performing I/O.
	Diagnostic batch[QUEUE_LIMIT];
	uint32_t count;
	{
		MutexLock lock(pending_mutex);
		count = pending_count;
		memcpy(batch, pending, count * sizeof(Diagnostic));
		pending_count = 0;
	}
	for (uint32_t i = 0; i < count; i++) {
		const Diagnostic &event = batch[i];
		Dictionary output;
		output["event"] = "diagnostic";
		output["severity"] = event.type == ERR_HANDLER_WARNING ? "warning" : "error";
		output["kind"] = _error_handler_type_string(event.type);
		output["message"] = String::utf8(event.message);
		output["function"] = String::utf8(event.function);
		output["worker_thread"] = event.worker;
		output["truncated"] = event.truncated;
		const String file = String::utf8(event.file);
		Dictionary location;
		location["path"] = file.is_empty() ? Variant() : Variant(file);
		location["line"] = event.line > 0 ? Variant(event.line) : Variant();
		const bool project = file.begins_with("res://") || file.begins_with("user://");
		output["project_location"] = project ? Variant(location) : Variant();
		output["engine_location"] = !project && !file.is_empty() && !file.is_absolute_path() && file != "built-in" ? Variant(location) : Variant();
		// Absolute paths lack project identity here; retain evidence without guessing.
		output["source_location"] = location;
		write_event(output);
		diagnostic_count++;
	}
	if (fflush(report) != 0) {
		write_failed = true;
	}
}

void record_iteration(bool p_project_exit, bool p_budget_reached, uint64_t p_requested) {
	if (!report) {
		return;
	}
	iterations++;
	requested = p_requested;
	project_exit |= p_project_exit;
	budget_reached |= p_budget_reached;
	drain_report();
}

void finish_report(bool p_startup_failure, uint64_t p_requested) {
	poll_failure_exit();
	if (handler_installed) {
		remove_error_handler(&handler);
		handler_installed = false;
	}
	if (!report) {
		fail_on_script_error = false;
		script_fault_pending = false;
		return;
	}
	// Removal takes the engine's error lock and waits for in-flight callbacks.
	remove_error_handler(&handler);
	collecting = false;
	drain_report();
	lost_events += suppressed_events.load();
	requested = p_requested > 0 ? p_requested : requested;
	if (lost_events || truncated_events || write_failed) {
		OS::get_singleton()->set_exit_code(EXIT_FAILURE);
	}
	Dictionary summary;
	summary["event"] = "run_summary";
	summary["coverage"] = "setup_through_main_loop_finalize";
	summary["script_fault_count"] = script_fault_count;
	summary["reason"] = script_fault_pending.load() ? "script_fault" : p_startup_failure || (iterations == 0 && OS::get_singleton()->get_exit_code() != 0) ? "startup_failure" :
			project_exit ? "project_exit" : budget_reached ? "iteration_budget" : "no_main_loop";
	summary["requested_iterations"] = requested > 0 ? Variant(requested) : Variant();
	summary["observed_iterations"] = iterations;
	summary["budget_reached"] = budget_reached;
	summary["exit_code"] = OS::get_singleton()->get_exit_code();
	summary["diagnostic_count"] = diagnostic_count;
	summary["lost_events"] = lost_events;
	summary["truncated_events"] = truncated_events;
	summary["evidence_complete"] = lost_events == 0 && truncated_events == 0 && !write_failed;
	write_event(summary);
	if (fflush(report) != 0 || write_failed) {
		fprintf(stderr, "Agent report write failed; evidence is incomplete.\n");
		OS::get_singleton()->set_exit_code(EXIT_FAILURE);
	}
	if (fclose(report) != 0) {
		fprintf(stderr, "Agent report close failed; evidence is incomplete.\n");
		OS::get_singleton()->set_exit_code(EXIT_FAILURE);
	}
	report = nullptr;
	fail_on_script_error = false;
	script_fault_pending = false;
}
} // namespace AgentTools
