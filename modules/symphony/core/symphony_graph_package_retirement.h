/**************************************************************************/
/*  symphony_graph_package_retirement.h                                   */
/**************************************************************************/

#pragma once

#include "symphony_prepared_graph_package.h"
#include "symphony_runtime_metrics.h"

#include <atomic>
#include <cstdint>

// Lock-free retirement stack for PreparedGraphPackage.
// Audio thread only pushes; AudioServer update callback drains on the main thread.
class GraphPackageRetirement {
	static std::atomic<PreparedGraphPackage *> head;
	static std::atomic<uint32_t> pending_count;
	static bool update_callback_registered;

	static void _update_callback(void *p_userdata);

public:
	static void initialize();
	static void uninitialize();

	// Real-time safe: intrusive push onto the retirement stack.
	static void retire(PreparedGraphPackage *p_package);

	// Main thread only: destroy every retired package.
	static void drain();

	[[nodiscard]] static uint32_t get_pending_count();
	[[nodiscard]] static uint64_t get_destroyed_count();
	[[nodiscard]] static uint32_t get_peak_pending_count();
};
