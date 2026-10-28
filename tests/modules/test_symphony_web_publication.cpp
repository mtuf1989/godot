/**************************************************************************/
/*  test_symphony_web_publication.cpp                                     */
/*  Suite: [Symphony][Web] — lock-free publication and PFFFT SIMD select. */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_web_publication)

#include "modules/symphony/core/symphony_published_u64.h"
#include "modules/symphony/core/symphony_seqlock.h"
#include "modules/symphony/runtime/voice_manager.h"
#include "modules/symphony/thirdparty/pffft/pffft.h"

#include <atomic>

namespace TestSymphonyWebPublication {

TEST_CASE("[Symphony][Web] callback publication uses lock-free 32-bit atomics") {
	static_assert(std::atomic<uint32_t>::is_always_lock_free);
	static_assert(std::atomic<float>::is_always_lock_free);
	static_assert(!std::atomic<SymphonyVoicePool::VoiceMetrics>::is_always_lock_free);

	SymphonyPublishedU64 published;
	CHECK(published.load() == 0);
	CHECK(published.fetch_add(5) == 0);
	CHECK(published.load() == 5);
	published.publish(uint64_t(0x100000001ULL));
	CHECK(published.load() == uint64_t(0x100000001ULL));
	CHECK(published.writer_value() == uint64_t(0x100000001ULL));

	SymphonySeqLock<SymphonyVoicePool::VoiceMetrics> metrics;
	SymphonyVoicePool::VoiceMetrics written;
	written.active = 3;
	written.virtual_count = 1;
	written.stolen_this_frame = 2;
	written.budget_percent = 12.5f;
	written.occupancy_percent = 40.0f;
	metrics.store(written);
	const SymphonyVoicePool::VoiceMetrics read = metrics.load();
	CHECK(read.active == 3);
	CHECK(read.virtual_count == 1);
	CHECK(read.stolen_this_frame == 2);
	CHECK(read.budget_percent == 12.5f);
	CHECK(read.occupancy_percent == 40.0f);
}

TEST_CASE("[Symphony][Web] PFFFT scalar or SIMD kernel matches the build flags") {
	const char *arch = pffft_simd_arch();
	const int simd = pffft_simd_size();
	REQUIRE(arch != nullptr);
	MESSAGE("pffft arch=", String(arch), " simd_size=", simd);
	CHECK((simd == 1 || simd == 4));

#if defined(PFFFT_SIMD_DISABLE)
	CHECK(simd == 1);
	CHECK(String(arch) == "Scalar");
#elif defined(__wasm_simd128__)
	CHECK(simd == 4);
	CHECK(String(arch) == "SSE1");
#else
	if (String(arch) == "Scalar") {
		CHECK(simd == 1);
	} else {
		CHECK(simd == 4);
	}
#endif

	constexpr int N = 256;
	PFFFT_Setup *setup = pffft_new_setup(N, PFFFT_REAL);
	REQUIRE(setup != nullptr);
	float *in = (float *)pffft_aligned_malloc(sizeof(float) * N);
	float *spec = (float *)pffft_aligned_malloc(sizeof(float) * N);
	float *out = (float *)pffft_aligned_malloc(sizeof(float) * N);
	float *work = (float *)pffft_aligned_malloc(sizeof(float) * N);
	REQUIRE(in != nullptr);
	REQUIRE(spec != nullptr);
	REQUIRE(out != nullptr);
	REQUIRE(work != nullptr);
	for (int i = 0; i < N; i++) {
		in[i] = (i == 0) ? 1.0f : 0.0f;
		spec[i] = 0.0f;
		out[i] = 0.0f;
	}
	pffft_transform_ordered(setup, in, spec, work, PFFFT_FORWARD);
	pffft_transform_ordered(setup, spec, out, work, PFFFT_BACKWARD);
	const float scale = 1.0f / (float)N;
	CHECK(Math::abs(out[0] * scale - 1.0f) < 1.0e-4f);
	CHECK(Math::abs(out[1] * scale) < 1.0e-4f);
	pffft_aligned_free(in);
	pffft_aligned_free(spec);
	pffft_aligned_free(out);
	pffft_aligned_free(work);
	pffft_destroy_setup(setup);
}

} // namespace TestSymphonyWebPublication
