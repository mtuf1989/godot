/**************************************************************************/
/*  test_symphony_profile.cpp                                             */
/*  Suite: [Symphony][Profile] — mixed-workload callback percentiles.     */
/*                                                                        */
/*  Default smoke is about one second per workload. Full soak, with each  */
/*  spectral workload using SYMPHONY_PROFILE_SECONDS and no shorter cap:  */
/*    SYMPHONY_PROFILE_SECONDS=1800 bin/godot… --test \                    */
/*      --test-case='*Symphony*Profile*30*'                                */
/*  Prefer template_release before treating P99 as a release gate.        */
/*  Browser underruns are not measured here.                               */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_profile)

#include "modules/symphony/core/symphony_graph_description.h"
#include "modules/symphony/core/symphony_voice_manager.h"
#include "modules/symphony/effects/audio_effect_symphony_reverb.h"
#include "modules/symphony/stream/audio_stream_playback_symphony.h"
#include "modules/symphony/stream/audio_stream_playback_symphony_music.h"
#include "modules/symphony/stream/audio_stream_symphony.h"
#include "modules/symphony/stream/audio_stream_symphony_music.h"

#include "core/os/os.h"
#include "servers/audio/audio_driver_dummy.h"
#include "servers/audio/audio_effect.h"

#include <cmath>

namespace TestSymphonyProfile {

struct Workload {
	const char *name = "";
	int sfx = 0;
	int spatial = 0;
	int phase_vocoders = 0;
	int spectral_gates = 0;
	int synth_notes = 0;
	int stretches = 0;
	int reverbs = 0;
	int fft_size = 1024;
};

static int _profile_seconds() {
	const String env = OS::get_singleton()->get_environment("SYMPHONY_PROFILE_SECONDS");
	if (env.is_empty()) {
		return 1;
	}
	return MAX(1, env.to_int());
}

static int64_t _debug_i64(const char *p_key) {
	SymphonyVoiceManager *vm = SymphonyVoiceManager::get_singleton();
	if (vm == nullptr) {
		return -1;
	}
	const Dictionary metrics = vm->get_debug_metrics();
	const Variant value = metrics.get(String(p_key), Variant());
	if (value.get_type() == Variant::NIL) {
		return -1;
	}
	return (int64_t)value;
}

static void _percentile(Vector<uint64_t> &p_samples, uint64_t &r_p50, uint64_t &r_p95, uint64_t &r_p99) {
	REQUIRE(p_samples.size() > 0);
	p_samples.sort();
	const int n = p_samples.size();
	r_p50 = p_samples[(n - 1) / 2];
	r_p95 = p_samples[CLAMP((int)((n - 1) * 0.95), 0, n - 1)];
	r_p99 = p_samples[CLAMP((int)((n - 1) * 0.99), 0, n - 1)];
}

static void _connect(GraphDescription &r_desc, int p_from, int p_from_pin, int p_to, int p_to_pin) {
	ConnectionDesc conn;
	conn.from_node = p_from;
	conn.from_pin = p_from_pin;
	conn.to_node = p_to;
	conn.to_pin = p_to_pin;
	r_desc.connections.push_back(conn);
}

static GraphDescription _tone_noise(int p_seed) {
	GraphDescription desc;
	desc.smooth_parameters = false;
	NodeDesc osc;
	osc.id = 1;
	osc.type_name = "Oscillator";
	osc.params.insert("frequency", 220.0f);
	osc.params.insert("waveform", 0.0f);
	desc.nodes.push_back(osc);
	NodeDesc noise;
	noise.id = 2;
	noise.type_name = "Noise";
	noise.params.insert("mode", 0.0f);
	noise.params.insert("seed", (float)p_seed);
	noise.params.insert("deterministic_seed", 1.0f);
	desc.nodes.push_back(noise);
	NodeDesc mix;
	mix.id = 3;
	mix.type_name = "Mix";
	mix.params.insert("mix", 0.35f);
	desc.nodes.push_back(mix);
	_connect(desc, 1, 0, 3, 0);
	_connect(desc, 2, 0, 3, 1);
	return desc;
}

static GraphDescription _ordinary_sfx(float p_hz) {
	GraphDescription desc;
	desc.smooth_parameters = false;
	NodeDesc osc;
	osc.id = 1;
	osc.type_name = "Oscillator";
	osc.params.insert("frequency", p_hz);
	osc.params.insert("waveform", 0.0f);
	desc.nodes.push_back(osc);
	NodeDesc filter;
	filter.id = 2;
	filter.type_name = "SVFilter";
	filter.params.insert("cutoff", 1800.0f);
	filter.params.insert("resonance", 0.2f);
	desc.nodes.push_back(filter);
	NodeDesc out;
	out.id = 3;
	out.type_name = "GraphOutput";
	desc.nodes.push_back(out);
	_connect(desc, 1, 0, 2, 0);
	_connect(desc, 2, 0, 3, 0);
	return desc;
}

static GraphDescription _spatial_sfx(float p_hz) {
	GraphDescription desc;
	desc.smooth_parameters = false;
	NodeDesc osc;
	osc.id = 1;
	osc.type_name = "Oscillator";
	osc.params.insert("frequency", p_hz);
	osc.params.insert("waveform", 0.0f);
	desc.nodes.push_back(osc);
	NodeDesc er;
	er.id = 2;
	er.type_name = "EarlyReflections";
	er.params.insert("room_width", 8.0f);
	er.params.insert("room_height", 3.0f);
	er.params.insert("room_depth", 6.0f);
	er.params.insert("reflection", 0.65f);
	desc.nodes.push_back(er);
	NodeDesc out;
	out.id = 3;
	out.type_name = "GraphOutput";
	desc.nodes.push_back(out);
	_connect(desc, 1, 0, 2, 0);
	_connect(desc, 2, 0, 3, 0);
	return desc;
}

static GraphDescription _phase_vocoder(int p_fft) {
	GraphDescription desc = _tone_noise(11);
	NodeDesc pv;
	pv.id = 4;
	pv.type_name = "PhaseVocoder";
	pv.params.insert("fft_size", (float)p_fft);
	pv.params.insert("overlap", 4.0f);
	pv.params.insert("pitch_shift", 3.0f);
	desc.nodes.push_back(pv);
	NodeDesc out;
	out.id = 5;
	out.type_name = "GraphOutput";
	desc.nodes.push_back(out);
	_connect(desc, 3, 0, 4, 0);
	_connect(desc, 4, 0, 5, 0);
	return desc;
}

static GraphDescription _spectral_gate(int p_fft) {
	GraphDescription desc = _tone_noise(29);
	NodeDesc gate;
	gate.id = 4;
	gate.type_name = "SpectralGate";
	gate.params.insert("fft_size", (float)p_fft);
	gate.params.insert("overlap", 4.0f);
	gate.params.insert("threshold_db", -18.0f);
	gate.params.insert("reduction_db", -24.0f);
	desc.nodes.push_back(gate);
	NodeDesc out;
	out.id = 5;
	out.type_name = "GraphOutput";
	desc.nodes.push_back(out);
	_connect(desc, 3, 0, 4, 0);
	_connect(desc, 4, 0, 5, 0);
	return desc;
}

static GraphDescription _instrument() {
	GraphDescription desc;
	desc.smooth_parameters = false;
	NodeDesc note;
	note.id = 1;
	note.type_name = "MusicNoteInput";
	note.params.insert("root_hz", 440.0f);
	desc.nodes.push_back(note);
	NodeDesc osc;
	osc.id = 2;
	osc.type_name = "Oscillator";
	osc.params.insert("waveform", 0.0f);
	desc.nodes.push_back(osc);
	NodeDesc adsr;
	adsr.id = 3;
	adsr.type_name = "ADSR";
	adsr.params.insert("attack", 0.001f);
	adsr.params.insert("decay", 0.02f);
	adsr.params.insert("sustain", 0.6f);
	adsr.params.insert("release", 0.05f);
	desc.nodes.push_back(adsr);
	NodeDesc out;
	out.id = 4;
	out.type_name = "GraphOutput";
	desc.nodes.push_back(out);
	_connect(desc, 1, 0, 2, 0);
	_connect(desc, 2, 0, 3, 0);
	_connect(desc, 1, 6, 3, 1);
	_connect(desc, 3, 0, 4, 0);
	return desc;
}

static void _run_workload(const Workload &p_workload, float p_mix_rate, int p_callback_frames, int p_seconds, bool p_require_gate) {
	REQUIRE(p_workload.spatial + p_workload.phase_vocoders + p_workload.spectral_gates <= p_workload.sfx);
	REQUIRE(p_callback_frames > 0);

	AudioDriverDummy *dummy = AudioDriverDummy::get_dummy_singleton();
	REQUIRE(dummy != nullptr);
	struct Restore {
		AudioDriverDummy *d;
		int prev;
		~Restore() {
			if (d) {
				d->set_mix_rate(prev);
			}
		}
	} restore{ dummy, dummy->get_mix_rate() };
	dummy->set_mix_rate((int)p_mix_rate);

	const uint64_t prepare_start = OS::get_singleton()->get_ticks_usec();
	Vector<Ref<AudioStreamSymphony>> streams;
	Vector<Ref<AudioStreamPlayback>> playbacks;
	Vector<Ref<AudioStreamPlaybackSymphony>> voices;
	const int ordinary = p_workload.sfx - p_workload.spatial - p_workload.phase_vocoders - p_workload.spectral_gates;
	auto add_voice = [&](const GraphDescription &p_graph) {
		Ref<AudioStreamSymphony> stream;
		stream.instantiate();
		stream->set_mix_rate(p_mix_rate);
		stream->set_graph_description(p_graph);
		Ref<AudioStreamPlayback> base = stream->instantiate_playback();
		Ref<AudioStreamPlaybackSymphony> voice = base;
		REQUIRE(voice.is_valid());
		voice->start();
		streams.push_back(stream);
		playbacks.push_back(base);
		voices.push_back(voice);
	};
	for (int i = 0; i < ordinary; i++) {
		add_voice(_ordinary_sfx(110.0f * float(i + 1)));
	}
	for (int i = 0; i < p_workload.spatial; i++) {
		add_voice(_spatial_sfx(80.0f + 15.0f * float(i)));
	}
	for (int i = 0; i < p_workload.phase_vocoders; i++) {
		add_voice(_phase_vocoder(p_workload.fft_size));
	}
	for (int i = 0; i < p_workload.spectral_gates; i++) {
		add_voice(_spectral_gate(p_workload.fft_size));
	}

	Ref<AudioStreamSymphonyMusic> music;
	Ref<AudioStreamPlayback> music_base;
	Ref<AudioStreamPlaybackSymphonyMusic> music_pb;
	if (p_workload.synth_notes > 0 || p_workload.stretches > 0) {
		music.instantiate();
		music->set_mix_rate(p_mix_rate);
		music->set_initial_bpm(120.0f);
		Ref<AudioStreamSymphony> instrument;
		instrument.instantiate();
		instrument->set_mix_rate(p_mix_rate);
		instrument->set_graph_description(_instrument());
		const int polyphony = MAX(1, p_workload.synth_notes);
		REQUIRE(music->set_track_instrument(0, instrument, polyphony, true) == OK);
		for (int i = 0; i < p_workload.stretches; i++) {
			const int clip_frames = (int)(p_mix_rate * 0.25f);
			PackedFloat32Array pcm;
			pcm.resize(clip_frames * 2);
			float *pcm_w = pcm.ptrw();
			for (int s = 0; s < clip_frames; s++) {
				const float sample = Math::sin(Math::TAU * (330.0f + 40.0f * float(i)) * (float)s / p_mix_rate) * 0.3f;
				pcm_w[s * 2] = sample;
				pcm_w[s * 2 + 1] = sample;
			}
			const StringName key = StringName(vformat("profile_clip_%d", i));
			REQUIRE(music->register_clip_pcm(key, pcm, p_mix_rate, 120.0f) == OK);
		}
		music_base = music->instantiate_playback();
		music_pb = music_base;
		REQUIRE(music_pb.is_valid());
		music_pb->start();
		Dictionary phrase;
		phrase["start_tick"] = 0;
		phrase["generation"] = 1;
		Array events;
		for (int i = 0; i < p_workload.synth_notes; i++) {
			Dictionary on;
			on["kind"] = "note_on";
			on["tick"] = 0;
			on["track"] = 0;
			on["note_id"] = i + 1;
			on["midi"] = 48 + (i % 24);
			on["velocity"] = 0.7;
			on["duration_ticks"] = 19200;
			events.push_back(on);
		}
		for (int i = 0; i < p_workload.stretches; i++) {
			Dictionary clip;
			clip["kind"] = "clip_on";
			clip["tick"] = 0;
			clip["track"] = 0;
			clip["note_id"] = 1000 + i;
			clip["clip_key"] = StringName(vformat("profile_clip_%d", i));
			clip["transpose"] = 0;
			clip["tempo_ratio"] = 1.0;
			events.push_back(clip);
		}
		phrase["events"] = events;
		REQUIRE(music_pb->submit_phrase(phrase) == OK);
	}

	Vector<Ref<AudioEffectSymphonyReverb>> reverbs;
	Vector<Ref<AudioEffectInstance>> reverb_instances;
	for (int i = 0; i < p_workload.reverbs; i++) {
		Ref<AudioEffectSymphonyReverb> reverb;
		reverb.instantiate();
		Ref<AudioEffectInstance> instance = reverb->instantiate();
		REQUIRE(instance.is_valid());
		reverbs.push_back(reverb);
		reverb_instances.push_back(instance);
	}
	const uint64_t prepare_us = OS::get_singleton()->get_ticks_usec() - prepare_start;

	const uint64_t deadline_us = (uint64_t)((double)p_callback_frames / (double)p_mix_rate * 1.0e6);
	const uint64_t end_us = OS::get_singleton()->get_ticks_usec() + uint64_t(p_seconds) * 1'000'000ull;
	Vector<uint64_t> samples;
	uint64_t misses = 0;
	double energy = 0.0;
	AudioFrame *chunk = (AudioFrame *)memalloc(sizeof(AudioFrame) * p_callback_frames);
	AudioFrame *wet = (AudioFrame *)memalloc(sizeof(AudioFrame) * p_callback_frames);
	REQUIRE(chunk != nullptr);
	REQUIRE(wet != nullptr);

	auto render_once = [&]() {
		for (int i = 0; i < p_callback_frames; i++) {
			chunk[i] = AudioFrame(0, 0);
		}
		for (int i = 0; i < voices.size(); i++) {
			if (voices[i].is_valid() && voices[i]->is_playing()) {
				voices[i]->mix(chunk, 1.0f, p_callback_frames);
			}
		}
		if (music_pb.is_valid() && music_pb->is_playing()) {
			music_pb->mix(chunk, 1.0f, p_callback_frames);
		}
		for (int i = 0; i < reverb_instances.size(); i++) {
			reverb_instances[i]->process(chunk, wet, p_callback_frames);
			for (int s = 0; s < p_callback_frames; s++) {
				chunk[s].left += wet[s].left;
				chunk[s].right += wet[s].right;
			}
		}
	};

	for (int i = 0; i < 4; i++) {
		render_once();
	}
	const int64_t memory_used_after_warmup = _debug_i64("memory_global_used_bytes");
	while (OS::get_singleton()->get_ticks_usec() < end_us) {
		const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
		render_once();
		const uint64_t dt = OS::get_singleton()->get_ticks_usec() - t0;
		samples.push_back(dt);
		if (dt > deadline_us) {
			misses++;
		}
		energy += (double)chunk[0].left * (double)chunk[0].left;
	}
	memfree(chunk);
	memfree(wet);

	const int64_t memory_used_before_teardown = _debug_i64("memory_global_used_bytes");
	const int64_t memory_peak_before_teardown = _debug_i64("memory_peak_used_bytes");

	uint64_t p50 = 0, p95 = 0, p99 = 0;
	_percentile(samples, p50, p95, p99);

	for (int i = 0; i < voices.size(); i++) {
		if (voices[i].is_valid()) {
			voices[i]->stop();
		}
	}
	if (music_pb.is_valid()) {
		music_pb->stop();
	}
	voices.clear();
	playbacks.clear();
	streams.clear();
	music_pb.unref();
	music_base.unref();
	music.unref();
	reverb_instances.clear();
	reverbs.clear();
	if (SymphonyVoiceManager::get_singleton()) {
		SymphonyVoiceManager::get_singleton()->process_deferred_lod();
	}
	const int64_t memory_used_after_teardown = _debug_i64("memory_global_used_bytes");
	MESSAGE("profile workload=", String(p_workload.name),
			" rate=", p_mix_rate, " cb=", p_callback_frames,
			" s=", p_seconds, " n=", samples.size(),
			" prepare_us=", prepare_us,
			" p50_us=", p50, " p95_us=", p95, " p99_us=", p99,
			" deadline_us=", deadline_us, " deadline_misses=", misses,
			" p99_fraction=", (deadline_us > 0 ? (double)p99 / (double)deadline_us : 0.0),
			" memory_used_after_warmup=", memory_used_after_warmup,
			" memory_used_before_teardown=", memory_used_before_teardown,
			" memory_peak_before_teardown=", memory_peak_before_teardown,
			" memory_used_after_teardown=", memory_used_after_teardown);

	CHECK(samples.size() > 10);
	CHECK(energy > 0.0);
	if (p_require_gate) {
		CHECK(p99 * 2 < deadline_us);
		CHECK(misses == 0);
	}
}

static Workload _web(bool p_spectral) {
	Workload workload;
	workload.name = p_spectral ? "web-spectral" : "web";
	workload.sfx = 8;
	workload.spatial = 4;
	workload.phase_vocoders = p_spectral ? 1 : 0;
	workload.spectral_gates = p_spectral ? 1 : 0;
	workload.synth_notes = 8;
	workload.stretches = 1;
	workload.reverbs = 1;
	workload.fft_size = 1024;
	return workload;
}

static Workload _desktop(bool p_spectral) {
	Workload workload;
	workload.name = p_spectral ? "desktop-spectral" : "desktop";
	workload.sfx = 32;
	workload.spatial = 8;
	workload.phase_vocoders = p_spectral ? 2 : 0;
	workload.spectral_gates = p_spectral ? 2 : 0;
	workload.synth_notes = 32;
	workload.stretches = 2;
	workload.reverbs = 2;
	workload.fft_size = 2048;
	return workload;
}

TEST_CASE("[Symphony][Profile] web baseline accepts non-128 callback sizes") {
	const int seconds = MIN(1, _profile_seconds());
	const int sizes[] = { 96, 128, 160, 256, 480 };
	for (int size : sizes) {
		_run_workload(_web(false), 48000.0f, size, seconds, false);
	}
}

TEST_CASE("[Symphony][Profile] web spectral baseline at 48 kHz / 128") {
	_run_workload(_web(true), 48000.0f, 128, MIN(1, _profile_seconds()), false);
}

TEST_CASE("[Symphony][Profile] desktop baseline at 48 kHz / 128") {
	_run_workload(_desktop(false), 48000.0f, 128, MIN(1, _profile_seconds()), false);
}

TEST_CASE("[Symphony][Profile] desktop spectral baseline at 48 kHz / 128") {
	_run_workload(_desktop(true), 48000.0f, 128, MIN(1, _profile_seconds()), false);
}

TEST_CASE("[Symphony][Profile] web baseline at 44.1 kHz / 192") {
	_run_workload(_web(false), 44100.0f, 192, MIN(1, _profile_seconds()), false);
}

static bool _begin_release_soak(int &r_seconds, bool &r_gate) {
	r_seconds = _profile_seconds();
	if (r_seconds < 60) {
		MESSAGE("30-minute soak is an external release gate. Set SYMPHONY_PROFILE_SECONDS=1800 on a template_release binary.");
		return false;
	}
	r_gate = OS::get_singleton()->has_feature("template_release");
	if (!r_gate) {
		MESSAGE("Running the long profile without the P99 gate because this binary is not template_release.");
	}
	return true;
}

TEST_CASE("[Symphony][Profile][30min] web spectral release soak") {
	int seconds = 0;
	bool gate = false;
	if (!_begin_release_soak(seconds, gate)) {
		return;
	}
	_run_workload(_web(true), 48000.0f, 128, seconds, gate);
}

TEST_CASE("[Symphony][Profile][30min] desktop spectral release soak") {
	int seconds = 0;
	bool gate = false;
	if (!_begin_release_soak(seconds, gate)) {
		return;
	}
	_run_workload(_desktop(true), 48000.0f, 128, seconds, gate);
}

} // namespace TestSymphonyProfile
