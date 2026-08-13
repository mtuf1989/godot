/**************************************************************************/
/*  symphony_memory_budget.h                                              */
/**************************************************************************/

#pragma once

#include "core/object/object.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"

#include <cstddef>
#include <cstdint>

// Main-thread memory budget for compiled Symphony graph packages.
// Reservations happen before compilation; release happens only when a package
// is destroyed on the main thread (never on the audio thread).
class SymphonyMemoryBudget {
public:
	static constexpr size_t DEFAULT_PER_GRAPH_BYTES = size_t(8) * 1024 * 1024;

#if defined(__EMSCRIPTEN__)
	static constexpr size_t DEFAULT_GLOBAL_BYTES = size_t(32) * 1024 * 1024;
#elif defined(ANDROID_ENABLED) || defined(IOS_ENABLED)
	static constexpr size_t DEFAULT_GLOBAL_BYTES = size_t(64) * 1024 * 1024;
#else
	static constexpr size_t DEFAULT_GLOBAL_BYTES = size_t(128) * 1024 * 1024;
#endif

	struct Snapshot {
		size_t per_graph_limit_bytes = DEFAULT_PER_GRAPH_BYTES;
		size_t global_limit_bytes = DEFAULT_GLOBAL_BYTES;
		size_t reserved_bytes = 0;
		size_t peak_reserved_bytes = 0;
		size_t shared_pcm_bytes = 0;
		uint32_t active_packages = 0;
		uint32_t pending_packages = 0;
		uint32_t outgoing_packages = 0;
		uint32_t retired_packages = 0;
	};

	static SymphonyMemoryBudget *get_singleton();
	static void create_singleton();
	static void destroy_singleton();

	void set_per_graph_limit_bytes(size_t p_bytes);
	size_t get_per_graph_limit_bytes() const { return per_graph_limit_bytes; }

	void set_global_limit_bytes(size_t p_bytes);
	size_t get_global_limit_bytes() const { return global_limit_bytes; }

	// Reserve bytes for a package about to be compiled. Fails cleanly without
	// mutating state when per-graph or global limits would be exceeded.
	bool try_reserve(size_t p_bytes, String *r_error = nullptr);

	// Release previously reserved bytes when a package is destroyed/discarded.
	void release(size_t p_bytes);

	void set_shared_pcm_bytes(size_t p_bytes);
	size_t get_shared_pcm_bytes() const { return shared_pcm_bytes; }

	void set_package_counts(uint32_t p_active, uint32_t p_pending, uint32_t p_outgoing, uint32_t p_retired);
	Snapshot get_snapshot() const;

private:
	static SymphonyMemoryBudget *singleton;

	size_t per_graph_limit_bytes = DEFAULT_PER_GRAPH_BYTES;
	size_t global_limit_bytes = DEFAULT_GLOBAL_BYTES;
	size_t reserved_bytes = 0;
	size_t peak_reserved_bytes = 0;
	size_t shared_pcm_bytes = 0;
	uint32_t active_packages = 0;
	uint32_t pending_packages = 0;
	uint32_t outgoing_packages = 0;
	uint32_t retired_packages = 0;
};
