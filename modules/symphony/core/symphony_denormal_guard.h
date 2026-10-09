/**************************************************************************/
/*  symphony_denormal_guard.h                                             */
/**************************************************************************/

#pragma once

#include "core/typedefs.h"

#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <xmmintrin.h>
#define SYMPHONY_DENORMAL_X86 1
#elif defined(_MSC_VER) && defined(_M_ARM64)
#include <float.h>
#define SYMPHONY_DENORMAL_MSVC_ARM64 1
#elif defined(__aarch64__) || defined(_M_ARM64)
#define SYMPHONY_DENORMAL_ARM64 1
#elif defined(__arm__) || defined(_M_ARM)
#define SYMPHONY_DENORMAL_ARMV7 1
#else
#define SYMPHONY_DENORMAL_NONE 1
#endif

// RAII FTZ/DAZ (or platform equivalent). Skips the CSR write when the flag is
// already set. No-op on WASM and other targets without a flush mode.
class SymphonyDenormalGuard {
	bool engaged = false;
#if defined(SYMPHONY_DENORMAL_X86)
	unsigned int saved_mxcsr = 0;
#elif defined(SYMPHONY_DENORMAL_MSVC_ARM64)
	unsigned int saved_cw = 0;
#elif defined(SYMPHONY_DENORMAL_ARM64)
	uint64_t saved_fpcr = 0;
#elif defined(SYMPHONY_DENORMAL_ARMV7)
	uint32_t saved_fpscr = 0;
#endif

public:
	static bool is_supported() {
#if defined(SYMPHONY_DENORMAL_NONE)
		return false;
#else
		return true;
#endif
	}

	static bool is_flush_active() {
#if defined(SYMPHONY_DENORMAL_X86)
		return (_mm_getcsr() & 0x8040u) == 0x8040u;
#elif defined(SYMPHONY_DENORMAL_MSVC_ARM64)
		unsigned int cw = 0;
		_controlfp_s(&cw, 0, 0);
		return (cw & _MCW_DN) == _DN_FLUSH;
#elif defined(SYMPHONY_DENORMAL_ARM64)
		uint64_t fpcr = 0;
		__asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
		return (fpcr & (1ull << 24)) != 0;
#elif defined(SYMPHONY_DENORMAL_ARMV7)
		uint32_t fpscr = 0;
		__asm__ __volatile__("vmrs %0, fpscr" : "=r"(fpscr));
		return (fpscr & (1u << 24)) != 0;
#else
		return false;
#endif
	}

	SymphonyDenormalGuard() {
#if defined(SYMPHONY_DENORMAL_X86)
		saved_mxcsr = _mm_getcsr();
		if ((saved_mxcsr & 0x8040u) != 0x8040u) {
			_mm_setcsr(saved_mxcsr | 0x8040u);
			engaged = true;
		}
#elif defined(SYMPHONY_DENORMAL_MSVC_ARM64)
		_controlfp_s(&saved_cw, 0, 0);
		if ((saved_cw & _MCW_DN) != _DN_FLUSH) {
			unsigned int unused = 0;
			_controlfp_s(&unused, _DN_FLUSH, _MCW_DN);
			engaged = true;
		}
#elif defined(SYMPHONY_DENORMAL_ARM64)
		__asm__ __volatile__("mrs %0, fpcr" : "=r"(saved_fpcr));
		if ((saved_fpcr & (1ull << 24)) == 0) {
			const uint64_t next = saved_fpcr | (1ull << 24);
			__asm__ __volatile__("msr fpcr, %0" : : "r"(next));
			engaged = true;
		}
#elif defined(SYMPHONY_DENORMAL_ARMV7)
		__asm__ __volatile__("vmrs %0, fpscr" : "=r"(saved_fpscr));
		if ((saved_fpscr & (1u << 24)) == 0) {
			const uint32_t next = saved_fpscr | (1u << 24);
			__asm__ __volatile__("vmsr fpscr, %0" : : "r"(next));
			engaged = true;
		}
#else
		(void)engaged;
#endif
	}

	~SymphonyDenormalGuard() {
		if (!engaged) {
			return;
		}
#if defined(SYMPHONY_DENORMAL_X86)
		_mm_setcsr(saved_mxcsr);
#elif defined(SYMPHONY_DENORMAL_MSVC_ARM64)
		unsigned int unused = 0;
		_controlfp_s(&unused, saved_cw, _MCW_DN);
#elif defined(SYMPHONY_DENORMAL_ARM64)
		__asm__ __volatile__("msr fpcr, %0" : : "r"(saved_fpcr));
#elif defined(SYMPHONY_DENORMAL_ARMV7)
		__asm__ __volatile__("vmsr fpscr, %0" : : "r"(saved_fpscr));
#endif
	}

	SymphonyDenormalGuard(const SymphonyDenormalGuard &) = delete;
	SymphonyDenormalGuard &operator=(const SymphonyDenormalGuard &) = delete;
};

// Zero IEEE subnormals / tiny values (also useful on WASM where FTZ is unavailable).
_FORCE_INLINE_ void symphony_flush_denormal(float &p_value) {
	union {
		uint32_t i;
		float f;
	} v;
	v.f = p_value;
	if ((v.i & 0x7f800000u) < 0x08000000u) {
		p_value = 0.0f;
	}
}
