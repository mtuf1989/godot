/**************************************************************************/
/*  test_symphony_p4_rate.cpp                                             */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_p4_rate)

#include "modules/symphony/core/symphony_arena_allocator.h"
#include "modules/symphony/core/symphony_graph_compiler.h"
#include "modules/symphony/core/symphony_graph_description.h"
#include "modules/symphony/core/symphony_rate_policy.h"
#include "modules/symphony/nodes/filters/symphony_one_pole.h"
#include "modules/symphony/nodes/filters/symphony_sv_filter.h"
#include "modules/symphony/nodes/generators/symphony_oscillator.h"
#include "modules/symphony/stream/audio_stream_symphony.h"
#include "tests/modules/symphony_test_helpers.h"

#include <cmath>

using SymphonyTestHelpers::db_ratio;
using SymphonyTestHelpers::render_graph;
using SymphonyTestHelpers::rms;
using SymphonyTestHelpers::tone_level_db;

namespace TestSymphonyP4Rate {

static constexpr size_t ARENA_SIZE = 1 << 20;

TEST_CASE("[Symphony][P4][Rate] Music rate policy allow-list") {
	CHECK(symphony_offline_rate() == 48000.0f);
	CHECK(symphony_select_music_rate(44100.0f) == 44100.0f);
	CHECK(symphony_select_music_rate(48000.0f) == 48000.0f);
	CHECK(symphony_select_music_rate(96000.0f) == 48000.0f);
	CHECK(symphony_select_music_rate(22050.0f) == 48000.0f);
	CHECK(symphony_select_music_rate(192000.0f) == 48000.0f);
}

TEST_CASE("[Symphony][P4][Rate] SFX mix_rate defaults to 48k and clamps") {
	Ref<AudioStreamSymphony> stream;
	stream.instantiate();
	CHECK(stream->get_mix_rate() == doctest::Approx(48000.0f));
	stream->set_mix_rate(7000.0f);
	CHECK(stream->get_mix_rate() == doctest::Approx(8000.0f));
	stream->set_mix_rate(200000.0f);
	CHECK(stream->get_mix_rate() == doctest::Approx(192000.0f));
	stream->set_mix_rate(22050.0f);
	CHECK(stream->get_mix_rate() == doctest::Approx(22050.0f));
}

TEST_CASE("[Symphony][P4][Oscillator] Frequency 0 holds DC") {
	ArenaAllocator arena;
	arena.init(ARENA_SIZE);
	HashMap<StringName, Variant> params;
	params.insert("frequency", 0.0f);
	params.insert("waveform", 0.0f);
	SymphonyOperator *op = SymphonyOscillator::create(arena, params, 48000.0f);
	REQUIRE(op != nullptr);

	float out_buf[SYMPHONY_MICRO_BLOCK_SIZE];
	void *inputs[] = { nullptr, nullptr };
	void *outputs[] = { out_buf };
	op->bind_pins(inputs, outputs);
	op->execute(SYMPHONY_MICRO_BLOCK_SIZE);
	const float first = out_buf[0];
	for (int i = 1; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
		CHECK(out_buf[i] == doctest::Approx(first));
	}
	arena.free();
}

TEST_CASE("[Symphony][P4][Rate] Instrument graph pitch/RMS/envelope match at 44.1k and 48k") {
	auto make_note = []() {
		GraphDescription desc;
		desc.smooth_parameters = false;
		NodeDesc osc;
		osc.id = 1;
		osc.type_name = "Oscillator";
		osc.params.insert("frequency", 440.0f);
		osc.params.insert("waveform", 0.0f);
		desc.nodes.push_back(osc);
		NodeDesc trig;
		trig.id = 2;
		trig.type_name = "TriggerInput";
		trig.params.insert("auto_trigger_on_play", 1.0f);
		desc.nodes.push_back(trig);
		NodeDesc adsr;
		adsr.id = 3;
		adsr.type_name = "ADSR";
		adsr.params.insert("attack", 0.01f);
		adsr.params.insert("decay", 0.05f);
		adsr.params.insert("sustain", 0.7f);
		adsr.params.insert("release", 0.08f);
		desc.nodes.push_back(adsr);
		NodeDesc out;
		out.id = 4;
		out.type_name = "GraphOutput";
		desc.nodes.push_back(out);
		desc.connections.push_back({ 1, 0, 3, 0, false }); // osc → adsr audio
		desc.connections.push_back({ 2, 0, 3, 1, false }); // trigger → adsr
		desc.connections.push_back({ 3, 0, 4, 0, false });
		return desc;
	};

	auto peak_hz = [](const Vector<float> &buf, float sr) -> float {
		// Parabolic-interpolated DFT peak near 440.
		const int n = buf.size();
		const double bin_hz = (double)sr / (double)n;
		const int lo = MAX(1, (int)((400.0 / bin_hz)));
		const int hi = MIN(n / 2 - 2, (int)((480.0 / bin_hz)));
		double best = 0.0;
		int best_k = lo;
		for (int k = lo; k <= hi; k++) {
			double re = 0.0, im = 0.0;
			const double w = Math::TAU * (double)k / (double)n;
			for (int i = 0; i < n; i++) {
				re += (double)buf[i] * Math::cos(w * (double)i);
				im -= (double)buf[i] * Math::sin(w * (double)i);
			}
			const double p = re * re + im * im;
			if (p > best) {
				best = p;
				best_k = k;
			}
		}
		return (float)best_k * (float)bin_hz;
	};

	const int frames_48 = 48000 / 5; // 200 ms
	const int frames_44 = 44100 / 5;
	Vector<float> a = render_graph(make_note(), frames_48, 48000.0f);
	Vector<float> b = render_graph(make_note(), frames_44, 44100.0f);

	const float hz_a = peak_hz(a, 48000.0f);
	const float hz_b = peak_hz(b, 44100.0f);
	const float cents = 1200.0f * Math::log(hz_a / hz_b) / Math::log(2.0f);
	INFO("hz48=", hz_a, " hz44=", hz_b, " cents=", cents);
	CHECK(Math::abs(cents) <= 1.0f);

	const float rms_a = rms(a.ptr(), a.size());
	const float rms_b = rms(b.ptr(), b.size());
	CHECK(Math::abs(db_ratio(rms_a, rms_b)) <= 0.5f);

	// Envelope timing: find when |sample| first exceeds 0.1 after start.
	auto attack_ms = [](const Vector<float> &buf, float sr) -> float {
		for (int i = 0; i < buf.size(); i++) {
			if (Math::abs(buf[i]) > 0.1f) {
				return 1000.0f * (float)i / sr;
			}
		}
		return 1e9f;
	};
	CHECK(Math::abs(attack_ms(a, 48000.0f) - attack_ms(b, 44100.0f)) <= 1.0f);
}

TEST_CASE("[Symphony][P4][WavePlayer] Hermite Catmull-Rom is continuous on a looped buffer") {
	// White-box: the 4-point interpolator with forward wrap must not jump when
	// the read head crosses the loop end (period divides the loop length).
	const int loop_len = 48; // 1 kHz at 48 kHz
	const int loops = 4;
	float pcm[loop_len];
	for (int i = 0; i < loop_len; i++) {
		pcm[i] = sinf((float)Math::TAU * (float)i / (float)loop_len);
	}
	auto fetch = [&](int idx) -> float {
		int m = idx % loop_len;
		if (m < 0) {
			m += loop_len;
		}
		return pcm[m];
	};
	auto hermite = [&](double pos) -> float {
		int32_t idx = (int32_t)std::floor(pos);
		float t = (float)(pos - (double)idx);
		float ym1 = fetch(idx - 1);
		float y0 = fetch(idx);
		float y1 = fetch(idx + 1);
		float y2 = fetch(idx + 2);
		float t2 = t * t;
		float t3 = t2 * t;
		return 0.5f * ((2.0f * y0) + (-ym1 + y1) * t + (2.0f * ym1 - 5.0f * y0 + 4.0f * y1 - y2) * t2 + (-ym1 + 3.0f * y0 - 3.0f * y1 + y2) * t3);
	};
	// Periodic fetch ⇒ hermite(n·L + t) == hermite(t) within interpolation error.
	float seam_err = 0.0f;
	for (int k = 1; k <= loops; k++) {
		for (float t = 0.0f; t < (float)loop_len; t += 0.25f) {
			float a = hermite((double)k * loop_len + (double)t);
			float b = hermite((double)t);
			seam_err = MAX(seam_err, Math::abs(a - b));
		}
	}
	CHECK(seam_err < 1e-3f);
}

TEST_CASE("[Symphony][P4][Spatial] Open wrapper is flat at 10k/16k (48k)") {
	GraphDescription desc;
	desc.smooth_parameters = false;
	NodeDesc osc;
	osc.id = 1;
	osc.type_name = "Oscillator";
	osc.params.insert("frequency", 10000.0f);
	osc.params.insert("waveform", 0.0f);
	desc.nodes.push_back(osc);
	NodeDesc op;
	op.id = 2;
	op.type_name = "OnePole";
	op.params.insert("cutoff", 20000.0f);
	desc.nodes.push_back(op);
	NodeDesc sv;
	sv.id = 3;
	sv.type_name = "SVFilter";
	sv.params.insert("cutoff", 20000.0f);
	sv.params.insert("resonance", 0.0f);
	desc.nodes.push_back(sv);
	NodeDesc out;
	out.id = 4;
	out.type_name = "GraphOutput";
	desc.nodes.push_back(out);
	desc.connections.push_back({ 1, 0, 2, 0, false });
	desc.connections.push_back({ 2, 0, 3, 0, false });
	desc.connections.push_back({ 3, 0, 4, 0, false });

	for (float hz : { 10000.0f, 16000.0f }) {
		desc.nodes.write[0].params["frequency"] = hz;
		Vector<float> dry = render_graph(SymphonyTestHelpers::make_sine_graph(hz), 4800, 48000.0f);
		Vector<float> wet = render_graph(desc, 4800, 48000.0f);
		float d = tone_level_db(dry, 48000.0f, hz);
		float w = tone_level_db(wet, 48000.0f, hz);
		INFO("hz=", hz, " dry=", d, " wet=", w);
		CHECK(Math::abs(w - d) <= 0.1f);
	}
}

TEST_CASE("[Symphony][P4][Spatial] Open wrapper flat at 8 kHz for 22.05k graph") {
	GraphDescription desc;
	desc.smooth_parameters = false;
	NodeDesc osc;
	osc.id = 1;
	osc.type_name = "Oscillator";
	osc.params.insert("frequency", 8000.0f);
	osc.params.insert("waveform", 0.0f);
	desc.nodes.push_back(osc);
	NodeDesc op;
	op.id = 2;
	op.type_name = "OnePole";
	op.params.insert("cutoff", 20000.0f);
	desc.nodes.push_back(op);
	NodeDesc sv;
	sv.id = 3;
	sv.type_name = "SVFilter";
	sv.params.insert("cutoff", 20000.0f);
	desc.nodes.push_back(sv);
	NodeDesc out;
	out.id = 4;
	out.type_name = "GraphOutput";
	desc.nodes.push_back(out);
	desc.connections.push_back({ 1, 0, 2, 0, false });
	desc.connections.push_back({ 2, 0, 3, 0, false });
	desc.connections.push_back({ 3, 0, 4, 0, false });

	GraphDescription dry_desc = SymphonyTestHelpers::make_sine_graph(8000.0f);
	Vector<float> dry = render_graph(dry_desc, 2205, 22050.0f);
	Vector<float> wet = render_graph(desc, 2205, 22050.0f);
	float d = tone_level_db(dry, 22050.0f, 8000.0f);
	float w = tone_level_db(wet, 22050.0f, 8000.0f);
	CHECK(Math::abs(w - d) <= 0.1f);
}

} // namespace TestSymphonyP4Rate
