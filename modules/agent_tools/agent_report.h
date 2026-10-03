#pragma once
#include "core/error/error_list.h"
#include "core/string/ustring.h"

namespace AgentTools {
void enable_script_fault_exit();
bool poll_failure_exit();
Error begin_report(const String &p_path);
void drain_report();
void note_suppressed_diagnostic();
void record_iteration(bool p_project_exit, bool p_budget_reached, uint64_t p_requested);
void finish_report(bool p_startup_failure = false, uint64_t p_requested = 0);
}
