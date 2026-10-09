/**************************************************************************/
/*  test_symphony_reverb_effect.cpp                                       */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_reverb_effect)

#include "modules/symphony/effects/audio_effect_symphony_reverb.h"
#include "servers/audio/audio_server.h"

#include <atomic>
#include <cmath>
#include <thread>
#include <vector>

namespace TestSymphonyReverbEffect {

TEST_CASE("[Symphony][ReverbEffect] First reflection for 10×5×8 ≈ 9.2 ms") {
	const float ms = AudioEffectSymphonyReverb::compute_first_reflection_ms(Vector3(10, 5, 8));
	CHECK(ms == doctest::Approx(9.2f).epsilon(0.5));
}

TEST_CASE("[Symphony][ReverbEffect] Wet-only processes without NaN; auto predelay") {
	Ref<AudioEffectSymphonyReverb> fx;
	fx.instantiate();
	fx->set_dry(0.0f);
	fx->set_wet(1.0f);
	fx->set_auto_predelay(true);
	fx->set_room(1.2f, 0.5f, Vector3(10, 5, 8), 0.7f);

	Ref<AudioEffectInstance> inst = fx->instantiate();
	REQUIRE(inst.is_valid());

	const int n = 512;
	std::vector<AudioFrame> src(n), dst(n);
	for (int i = 0; i < n; i++) {
		src[i] = AudioFrame(i == 0 ? 0.5f : 0.0f, i == 0 ? 0.5f : 0.0f);
		dst[i] = AudioFrame(0, 0);
	}
	inst->process(src.data(), dst.data(), n);
	bool any = false;
	for (int i = 0; i < n; i++) {
		CHECK(std::isfinite(dst[i].left));
		CHECK(std::isfinite(dst[i].right));
		if (Math::abs(dst[i].left) + Math::abs(dst[i].right) > 1e-8f) {
			any = true;
		}
	}
	CHECK(any);
	CHECK(inst->process_silence() == false);
}

TEST_CASE("[Symphony][ReverbEffect] Concurrent set_room during process is safe") {
	Ref<AudioEffectSymphonyReverb> fx;
	fx.instantiate();
	fx->set_dry(0.0f);
	fx->set_wet(1.0f);
	Ref<AudioEffectInstance> inst = fx->instantiate();
	REQUIRE(inst.is_valid());

	std::atomic<bool> run{ true };
	std::thread writer([&]() {
		float t = 0.5f;
		while (run.load()) {
			fx->set_room(0.5f + t, 0.3f + 0.2f * t, Vector3(6 + t, 3, 5 + t), 0.5f + 0.2f * t);
			t = t > 2.0f ? 0.5f : t + 0.01f;
		}
	});

	const int n = 256;
	std::vector<AudioFrame> src(n), dst(n);
	for (int i = 0; i < n; i++) {
		src[i] = AudioFrame(0.1f, -0.1f);
	}
	for (int k = 0; k < 200; k++) {
		inst->process(src.data(), dst.data(), n);
		for (int i = 0; i < n; i++) {
			CHECK(std::isfinite(dst[i].left));
			CHECK(std::isfinite(dst[i].right));
		}
	}
	run.store(false);
	writer.join();
}

} // namespace TestSymphonyReverbEffect
