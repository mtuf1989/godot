#pragma once

#include "core/string/string_name.h"
#include "core/templates/hash_map.h"
#include "core/variant/variant.h"

#include <cstdint>

// Authorable seeds are stored as floats in .tres. 2^24-1 is the largest integer
// a float32 can represent exactly, so the descriptor max stops there.
static constexpr int64_t SYMPHONY_SEED_PARAM_MAX = (int64_t(1) << 24) - 1;

// SplitMix64 (Steele / Vigna). Mixes a counter or a combined seed into a 64-bit value.
inline uint64_t splitmix64(uint64_t x) {
	x += 0x9E3779B97F4A7C15ULL;
	x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
	x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
	return x ^ (x >> 31);
}

// Set by GraphCompiler around each operator create_fn. create_fn has no node id
// of its own, so noisy operators read the voice and the node from here.
struct SymphonyCompileContext {
	uint64_t voice_seed = 0;
	StringName node_id;
};

SymphonyCompileContext &symphony_compile_context();

// One seed per playback, drawn from a global counter. Stable across LOD recompiles.
uint64_t symphony_draw_voice_seed();

// Mixes voice_seed ^ hash(node_id) with the authored seed.
// When deterministic, ignores the voice and returns splitmix(authored ^ hash(node_id)).
inline uint32_t symphony_node_seed(uint64_t p_voice_seed, const StringName &p_node_id, uint32_t p_authored, bool p_deterministic) {
	const uint64_t node_hash = (uint64_t)p_node_id.hash();
	uint64_t mixed;
	if (p_deterministic) {
		mixed = (uint64_t)p_authored ^ node_hash;
	} else {
		mixed = p_voice_seed ^ node_hash;
		mixed ^= (uint64_t)p_authored * 0x9E3779B97F4A7C15ULL;
	}
	return (uint32_t)splitmix64(mixed);
}

inline uint32_t symphony_authored_seed(const HashMap<StringName, Variant> &p_params, uint32_t p_default = 0) {
	if (!p_params.has("seed")) {
		return p_default;
	}
	int64_t raw = (int64_t)p_params["seed"];
	if (raw < 0) {
		raw = 0;
	}
	if (raw > SYMPHONY_SEED_PARAM_MAX) {
		raw = SYMPHONY_SEED_PARAM_MAX;
	}
	return (uint32_t)raw;
}

inline bool symphony_deterministic_seed(const HashMap<StringName, Variant> &p_params) {
	if (!p_params.has("deterministic_seed")) {
		return false;
	}
	const Variant &value = p_params["deterministic_seed"];
	if (value.get_type() == Variant::BOOL) {
		return (bool)value;
	}
	return (float)value >= 0.5f;
}

// Restores the previous compile context, including on a failed create.
class SymphonyCompileContextScope {
	SymphonyCompileContext previous;

public:
	SymphonyCompileContextScope(uint64_t p_voice_seed, const StringName &p_node_id) {
		SymphonyCompileContext &ctx = symphony_compile_context();
		previous = ctx;
		ctx.voice_seed = p_voice_seed;
		ctx.node_id = p_node_id;
	}
	~SymphonyCompileContextScope() {
		symphony_compile_context() = previous;
	}
};
