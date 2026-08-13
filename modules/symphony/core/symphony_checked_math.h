/**************************************************************************/
/*  symphony_checked_math.h                                               */
/**************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

// Checked size arithmetic for arena planning. Reject overflow before allocation.
namespace SymphonyCheckedMath {

inline bool add(size_t p_a, size_t p_b, size_t &r_out) {
	if (p_a > std::numeric_limits<size_t>::max() - p_b) {
		return false;
	}
	r_out = p_a + p_b;
	return true;
}

inline bool mul(size_t p_a, size_t p_b, size_t &r_out) {
	if (p_a != 0 && p_b > std::numeric_limits<size_t>::max() / p_a) {
		return false;
	}
	r_out = p_a * p_b;
	return true;
}

// Align value upward to a power-of-two alignment.
inline bool align_up(size_t p_value, size_t p_align, size_t &r_out) {
	if (p_align == 0 || (p_align & (p_align - 1)) != 0) {
		return false;
	}
	size_t mask = p_align - 1;
	if (p_value > std::numeric_limits<size_t>::max() - mask) {
		return false;
	}
	r_out = (p_value + mask) & ~mask;
	return true;
}

// Simulate ArenaAllocator::alloc bump: align current offset, then add size.
inline bool bump(size_t &r_offset, size_t p_size, size_t p_align) {
	size_t aligned = 0;
	if (!align_up(r_offset, p_align, aligned)) {
		return false;
	}
	size_t next = 0;
	if (!add(aligned, p_size, next)) {
		return false;
	}
	r_offset = next;
	return true;
}

} // namespace SymphonyCheckedMath
