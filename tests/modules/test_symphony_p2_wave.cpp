/**************************************************************************/
/*  test_symphony_p2_wave.cpp                                             */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_p2_wave)

#include "core/io/resource_saver.h"
#include "core/math/math_funcs.h"
#include "modules/symphony/core/symphony_graph_description.h"
#include "modules/symphony/nodes/generators/symphony_wave_player.h"
#include "modules/symphony/spatial/spatial_graph_wrapper.h"
#include "scene/resources/audio/audio_stream_wav.h"
#include "servers/audio/audio_server.h"
#include "tests/modules/symphony_test_helpers.h"
#include "tests/test_utils.h"

using SymphonyTestHelpers::db_ratio;
using SymphonyTestHelpers::render_graph;
using SymphonyTestHelpers::rms;

namespace TestSymphonyP2Wave {

static constexpr float SR = 48000.0f;
static constexpr float HZ = 440.0f;
static constexpr int FRAMES = 4800; // 100 ms

static Vector<float> make_sine(int p_n, float p_amp = 0.5f) {
	Vector<float> out;
	out.resize(p_n);
	for (int i = 0; i < p_n; i++) {
		out.write[i] = p_amp * Math::sin((float)Math::TAU * HZ * (float)i / SR);
	}
	return out;
}

static Vector<uint8_t> to_pcm16_bytes(const Vector<float> &p_f) {
	Vector<uint8_t> bytes;
	bytes.resize(p_f.size() * 2);
	int16_t *dst = reinterpret_cast<int16_t *>(bytes.ptrw());
	for (int i = 0; i < p_f.size(); i++) {
		dst[i] = (int16_t)CLAMP(p_f[i] * 32767.0f, -32768.0f, 32767.0f);
	}
	return bytes;
}

static Vector<uint8_t> to_pcm8_bytes(const Vector<float> &p_f) {
	Vector<uint8_t> bytes;
	bytes.resize(p_f.size());
	for (int i = 0; i < p_f.size(); i++) {
		const int v = (int)Math::round((double)p_f[i] * 127.0);
		const int8_t s = (int8_t)CLAMP(v, -128, 127);
		bytes.write[i] = (uint8_t)s;
	}
	return bytes;
}

static GraphDescription make_wave_graph(const String &p_path) {
	GraphDescription desc;
	desc.smooth_parameters = false;
	NodeDesc wp;
	wp.id = 1;
	wp.type_name = "WavePlayer";
	wp.params["resource_path"] = p_path;
	wp.params["bake_audio"] = 1.0f;
	wp.params["auto_play"] = 1.0f;
	wp.params["loop_mode"] = 0.0f;
	desc.nodes.push_back(wp);
	NodeDesc out;
	out.id = 2;
	out.type_name = "GraphOutput";
	desc.nodes.push_back(out);
	desc.connections.push_back({ 1, 0, 2, 0, false });
	return desc;
}

static float pcm16_rms(const Vector<uint8_t> &p_pcm16) {
	const int n = p_pcm16.size() / 2;
	REQUIRE(n > 0);
	Vector<float> f;
	f.resize(n);
	const int16_t *src = reinterpret_cast<const int16_t *>(p_pcm16.ptr());
	for (int i = 0; i < n; i++) {
		f.write[i] = (float)src[i] / 32768.0f;
	}
	return rms(f.ptr(), n);
}

// [Audio] required: AudioStreamWAV::set_data locks AudioServer.
TEST_CASE("[Symphony][P2][Audio][WavePlayer] 8-bit decode widens to PCM16 within 1 dB") {
	REQUIRE(AudioServer::get_singleton() != nullptr);

	const Vector<float> sine = make_sine(FRAMES);
	Ref<AudioStreamWAV> eight;
	eight.instantiate();
	eight->set_format(AudioStreamWAV::FORMAT_8_BITS);
	eight->set_mix_rate((int)SR);
	eight->set_stereo(false);
	eight->set_data(to_pcm8_bytes(sine));

	Ref<AudioStreamWAV> sixteen;
	sixteen.instantiate();
	sixteen->set_format(AudioStreamWAV::FORMAT_16_BITS);
	sixteen->set_mix_rate((int)SR);
	sixteen->set_stereo(false);
	sixteen->set_data(to_pcm16_bytes(sine));

	Vector<uint8_t> decoded;
	bool stereo = true;
	int32_t len = 0;
	REQUIRE(SymphonyWavePlayer::decode_to_pcm16(eight, decoded, stereo, len));
	CHECK_FALSE(stereo);
	CHECK(len == FRAMES);
	CHECK_FALSE(decoded.is_empty());

	Vector<uint8_t> ref_pcm;
	bool stereo16 = false;
	int32_t len16 = 0;
	REQUIRE(SymphonyWavePlayer::decode_to_pcm16(sixteen, ref_pcm, stereo16, len16));

	const float d_rms = pcm16_rms(decoded);
	const float r_rms = pcm16_rms(ref_pcm);
	CHECK(d_rms > 1e-3f);
	CHECK(Math::abs(db_ratio(d_rms, r_rms)) <= 1.0f);
}

TEST_CASE("[Symphony][P2][Audio][WavePlayer] QOA decode RMS within 1 dB of PCM16") {
	REQUIRE(AudioServer::get_singleton() != nullptr);

	const Vector<float> sine = make_sine(FRAMES);
	Ref<AudioStreamWAV> sixteen;
	sixteen.instantiate();
	sixteen->set_format(AudioStreamWAV::FORMAT_16_BITS);
	sixteen->set_mix_rate((int)SR);
	sixteen->set_stereo(false);
	sixteen->set_data(to_pcm16_bytes(sine));

	qoa_desc desc = {};
	desc.channels = 1;
	desc.samplerate = (uint32_t)SR;
	desc.samples = (uint32_t)FRAMES;
	Vector<uint8_t> qoa_bytes;
	AudioStreamWAV::_compress_qoa(sine, qoa_bytes, &desc);
	REQUIRE_FALSE(qoa_bytes.is_empty());

	Ref<AudioStreamWAV> qoa;
	qoa.instantiate();
	qoa->set_format(AudioStreamWAV::FORMAT_QOA);
	qoa->set_mix_rate((int)SR);
	qoa->set_stereo(false);
	qoa->set_data(qoa_bytes);

	Vector<uint8_t> decoded;
	bool stereo = true;
	int32_t len = 0;
	REQUIRE(SymphonyWavePlayer::decode_to_pcm16(qoa, decoded, stereo, len));
	CHECK_FALSE(stereo);
	CHECK(len >= FRAMES - 8); // QOA frame padding tolerance
	CHECK_FALSE(decoded.is_empty());

	Vector<uint8_t> ref_pcm;
	bool stereo16 = false;
	int32_t len16 = 0;
	REQUIRE(SymphonyWavePlayer::decode_to_pcm16(sixteen, ref_pcm, stereo16, len16));

	const float d_rms = pcm16_rms(decoded);
	const float r_rms = pcm16_rms(ref_pcm);
	CHECK(d_rms > 1e-3f);
	CHECK(Math::abs(db_ratio(d_rms, r_rms)) <= 1.0f);
}

TEST_CASE("[Symphony][P2][Audio][WavePlayer] 8-bit and QOA graphs are non-silent vs PCM16") {
	REQUIRE(AudioServer::get_singleton() != nullptr);

	const Vector<float> sine = make_sine(FRAMES);

	const String path16 = TestUtils::get_temp_path("symphony_p2_pcm16.wav");
	Ref<AudioStreamWAV> sixteen;
	sixteen.instantiate();
	sixteen->set_format(AudioStreamWAV::FORMAT_16_BITS);
	sixteen->set_mix_rate((int)SR);
	sixteen->set_stereo(false);
	sixteen->set_data(to_pcm16_bytes(sine));
	REQUIRE(sixteen->save_to_wav(path16) == OK);

	const String path8 = TestUtils::get_temp_path("symphony_p2_pcm8.wav");
	Ref<AudioStreamWAV> eight;
	eight.instantiate();
	eight->set_format(AudioStreamWAV::FORMAT_8_BITS);
	eight->set_mix_rate((int)SR);
	eight->set_stereo(false);
	eight->set_data(to_pcm8_bytes(sine));
	REQUIRE(eight->save_to_wav(path8) == OK);

	qoa_desc qdesc = {};
	qdesc.channels = 1;
	qdesc.samplerate = (uint32_t)SR;
	qdesc.samples = (uint32_t)FRAMES;
	Vector<uint8_t> qoa_bytes;
	AudioStreamWAV::_compress_qoa(sine, qoa_bytes, &qdesc);
	Ref<AudioStreamWAV> qoa;
	qoa.instantiate();
	qoa->set_format(AudioStreamWAV::FORMAT_QOA);
	qoa->set_mix_rate((int)SR);
	qoa->set_stereo(false);
	qoa->set_data(qoa_bytes);
	const String path_qoa = TestUtils::get_temp_path("symphony_p2_qoa.res");
	REQUIRE(ResourceSaver::save(qoa, path_qoa) == OK);

	Vector<float> out16 = render_graph(make_wave_graph(path16), FRAMES, SR);
	Vector<float> out8 = render_graph(make_wave_graph(path8), FRAMES, SR);
	Vector<float> out_qoa = render_graph(make_wave_graph(path_qoa), FRAMES, SR);

	const float r16 = rms(out16.ptr(), FRAMES);
	const float r8 = rms(out8.ptr(), FRAMES);
	const float rq = rms(out_qoa.ptr(), FRAMES);
	INFO("rms16=", r16, " rms8=", r8, " rms_qoa=", rq);
	CHECK(r16 > 1e-3f);
	CHECK(r8 > 1e-3f);
	CHECK(rq > 1e-3f);
	CHECK(Math::abs(db_ratio(r8, r16)) <= 1.0f);
	CHECK(Math::abs(db_ratio(rq, r16)) <= 1.0f);
}

TEST_CASE("[Symphony][P2][Spatial] 8-bit WAV with path is wrappable") {
	Ref<AudioStreamWAV> eight_bit;
	eight_bit.instantiate();
	eight_bit->set_format(AudioStreamWAV::FORMAT_8_BITS);
	eight_bit->set_path("user://symphony_p2_wrap_8bit.wav");
	CHECK(SpatialGraphWrapper::is_wrappable_wav(eight_bit));
	CHECK(SpatialGraphWrapper::needs_wrapping(eight_bit));
}

} // namespace TestSymphonyP2Wave
