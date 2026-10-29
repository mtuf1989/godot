/**************************************************************************/
/*  test_symphony_profile.cpp                                             */
/*  Suite: [Symphony][Profile] — mixed-workload callback percentiles.     */
/*                                                                        */
/*  Default smoke is about one second per workload. Samples live in a      */
/*  fixed log2-microsecond histogram, not a duration-growing vector.      */
/*  Full soak, with each spectral workload using SYMPHONY_PROFILE_SECONDS */
/*  and no shorter cap:                                                   */
/*    SYMPHONY_PROFILE_SECONDS=1800 bin/godot… --test \                    */
/*      --test-case='*Symphony*Profile*30*'                                */
/*  Prefer template_release before treating P99 as a release gate.        */
/*  Browser underruns are not measured here.                               */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_profile)

#include "modules/symphony/core/symphony_graph_description.h"
#include "modules/symphony/core/symphony_qualification_meter.h"
#include "modules/symphony/core/symphony_voice_manager.h"
#include "servers/audio/audio_driver.h"
#include "modules/symphony/effects/audio_effect_symphony_reverb.h"
#include "modules/symphony/stream/audio_stream_playback_symphony.h"
#include "modules/symphony/stream/audio_stream_playback_symphony_music.h"
#include "modules/symphony/stream/audio_stream_symphony.h"
#include "modules/symphony/stream/audio_stream_symphony_music.h"

#include "core/os/os.h"
#include "servers/audio/audio_driver_dummy.h"
#include "servers/audio/audio_effect.h"

#include <cmath>
#include <cstdio>
#include <ctime>

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

// Log2 microsecond histogram. Rank is the bucket that contains the sample, so
// the printed low/high edges are the percentile resolution. A 43 µs sample
// lands in 32-64, which is a 32 µs bin, not an exact 43.
struct UsecHistogram {
	uint32_t counts[SYMPHONY_METER_BUCKETS] = {};
	uint64_t n = 0;

	void add(uint64_t p_us) {
		const int bucket = symphony_meter_bucket(p_us);
		if (counts[bucket] != UINT32_MAX) {
			counts[bucket]++;
		}
		n++;
	}

	bool percentile(double p_rank, uint64_t &r_lo, uint64_t &r_hi) const {
		if (n == 0 || n > UINT32_MAX) {
			return false;
		}
		return symphony_meter_percentile(counts, SYMPHONY_METER_BUCKETS, (uint32_t)n, p_rank, r_lo, r_hi);
	}
};

struct MemSnap {
	int64_t used = -1;
	int64_t reserved = -1;
	int64_t shared_pcm = -1;
	int64_t working = -1;
	int64_t peak = -1;
};

static MemSnap _mem_snap() {
	MemSnap snap;
	snap.used = _debug_i64("memory_global_used_bytes");
	snap.reserved = _debug_i64("memory_reserved_bytes");
	snap.shared_pcm = _debug_i64("memory_shared_pcm_bytes");
	snap.working = _debug_i64("memory_working_bytes");
	snap.peak = _debug_i64("memory_peak_used_bytes");
	return snap;
}

static String _bucket_text(uint64_t p_lo, uint64_t p_hi) {
	if (p_hi == UINT64_MAX) {
		return vformat("%d-inf", (int64_t)p_lo);
	}
	return vformat("%d-%d", (int64_t)p_lo, (int64_t)p_hi);
}

static uint64_t _bucket_width(uint64_t p_lo, uint64_t p_hi) {
	if (p_hi == UINT64_MAX || p_hi <= p_lo) {
		return UINT64_MAX;
	}
	return p_hi - p_lo;
}

// Thread CPU time. Sampled around render so a wall-clock spike can be split
// into on-CPU mix time and time the thread was not running. UINT64_MAX means
// this platform has no thread CPU clock.
static uint64_t _thread_cpu_usec() {
#if defined(CLOCK_THREAD_CPUTIME_ID)
	struct timespec ts;
	if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0) {
		return UINT64_MAX;
	}
	return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
#else
	return UINT64_MAX;
#endif
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

static void _run_workload(const Workload &p_workload, float p_mix_rate, int p_callback_frames, int p_seconds, bool p_require_gate, bool p_repeat_memory = false) {
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

	// Stretch scratch stays with a live playback and is released when that playback
	// is destroyed. Graph arenas on the freelist stay with the stream and are freed
	// with it. Stereo PCM and budgeted graph bytes release with their last owner.
	// peak_used_bytes is a high-water mark and is not reclaimed.
	const MemSnap baseline = _mem_snap();
	REQUIRE(baseline.used >= 0);
	const int pass_count = (p_repeat_memory || p_require_gate) ? 2 : 1;
	int64_t first_warmup_used = -1;
	int64_t peak_after_first = baseline.peak;

	for (int pass = 0; pass < pass_count; pass++) {
	const int pass_seconds = pass == 0 ? p_seconds : 1;
	const bool pass_gate = p_require_gate && pass == 0;
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
	const uint64_t end_us = OS::get_singleton()->get_ticks_usec() + uint64_t(pass_seconds) * 1'000'000ull;
	// time() is independent of the engine tick clock. The loop stops when either
	// clock says the pass is over, so a stuck usec clock cannot run past the soak.
	const time_t wall_start = time(nullptr);
	const time_t wall_deadline = wall_start + pass_seconds + 2;
	time_t last_beat = wall_start;
	bool stopped_on_wall = false;
	UsecHistogram wall_hist;
	UsecHistogram cpu_hist;
	uint64_t wall_misses = 0;
	uint64_t scheduler_stalls = 0;
	uint64_t mix_deadline_misses = 0;
	uint64_t unclassified_wall_misses = 0;
	uint64_t max_wall_us = 0;
	uint64_t max_cpu_us = 0;
	bool cpu_known = true;
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
	const MemSnap warmed = _mem_snap();
	uint64_t loop_iters = 0;
	while (OS::get_singleton()->get_ticks_usec() < end_us) {
		if ((++loop_iters & 0xffff) == 0) {
			const time_t now = time(nullptr);
			if (now >= wall_deadline) {
				stopped_on_wall = true;
				break;
			}
			if (now - last_beat >= 60) {
				fprintf(stderr, "profile beat workload=%s pass=%d elapsed_s=%ld\n", p_workload.name, pass, (long)(now - wall_start));
				last_beat = now;
			}
		}
		const uint64_t cpu0 = _thread_cpu_usec();
		const uint64_t wall0 = OS::get_singleton()->get_ticks_usec();
		render_once();
		const uint64_t wall1 = OS::get_singleton()->get_ticks_usec();
		const uint64_t cpu1 = _thread_cpu_usec();
		const uint64_t wall_us = wall1 - wall0;
		wall_hist.add(wall_us);
		if (wall_us > max_wall_us) {
			max_wall_us = wall_us;
		}
		const bool this_cpu_known = cpu0 != UINT64_MAX && cpu1 != UINT64_MAX && cpu1 >= cpu0;
		uint64_t cpu_us = 0;
		if (!this_cpu_known) {
			cpu_known = false;
		} else {
			cpu_us = cpu1 - cpu0;
			if (cpu_us > wall_us) {
				cpu_us = wall_us;
			}
			cpu_hist.add(cpu_us);
			if (cpu_us > max_cpu_us) {
				max_cpu_us = cpu_us;
			}
		}
		if (wall_us > deadline_us) {
			wall_misses++;
			if (!this_cpu_known) {
				unclassified_wall_misses++;
			} else if (cpu_us > deadline_us) {
				mix_deadline_misses++;
			} else {
				scheduler_stalls++;
			}
		}
		energy += (double)chunk[0].left * (double)chunk[0].left;
	}
	memfree(chunk);
	memfree(wet);

	const MemSnap before_teardown = _mem_snap();

	uint64_t p50_lo = 0, p50_hi = 0, p95_lo = 0, p95_hi = 0, p99_lo = 0, p99_hi = 0;
	uint64_t cpu_p99_lo = 0, cpu_p99_hi = 0;
	const bool have_wall = wall_hist.percentile(0.50, p50_lo, p50_hi) && wall_hist.percentile(0.95, p95_lo, p95_hi) && wall_hist.percentile(0.99, p99_lo, p99_hi);
	const bool have_cpu_p99 = cpu_hist.percentile(0.99, cpu_p99_lo, cpu_p99_hi);
	REQUIRE(have_wall);

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
	const MemSnap after = _mem_snap();
	const int device_underruns = AudioDriver::get_singleton() ? AudioDriver::get_singleton()->get_output_underrun_count() : -1;
	MESSAGE("profile workload=", String(p_workload.name),
			" pass=", pass, " rate=", p_mix_rate, " cb=", p_callback_frames,
			" s=", pass_seconds, " n=", wall_hist.n,
			" histogram=log2_microseconds percentile=nearest_rank_bucket_low",
			" prepare_us=", prepare_us,
			" p50_us=", p50_lo, " p50_bucket=", _bucket_text(p50_lo, p50_hi), " p50_width_us=", _bucket_width(p50_lo, p50_hi),
			" p95_us=", p95_lo, " p95_bucket=", _bucket_text(p95_lo, p95_hi), " p95_width_us=", _bucket_width(p95_lo, p95_hi),
			" p99_us=", p99_lo, " p99_bucket=", _bucket_text(p99_lo, p99_hi), " p99_width_us=", _bucket_width(p99_lo, p99_hi),
			" cpu_p99_us=", (have_cpu_p99 ? String(vformat("%d", (int64_t)cpu_p99_lo)) : String("unavailable")),
			" cpu_p99_bucket=", (have_cpu_p99 ? _bucket_text(cpu_p99_lo, cpu_p99_hi) : String("unavailable")),
			" deadline_us=", deadline_us,
			" stopped_on=", String(stopped_on_wall ? "wall" : "ticks"),
			" tight_loop_wall_misses=", wall_misses,
			" scheduler_stalls=", scheduler_stalls,
			" mix_deadline_misses=", mix_deadline_misses,
			" max_wall_us=", max_wall_us, " max_cpu_us=", (cpu_known ? String(vformat("%d", (int64_t)max_cpu_us)) : String("unavailable")),
			" device_underruns=", (device_underruns < 0 ? String("unavailable") : String(vformat("%d", device_underruns))),
			" p99_fraction=", (deadline_us > 0 ? (double)p99_hi / (double)deadline_us : 0.0),
			" memory_baseline_used=", baseline.used,
			" memory_baseline_reserved=", baseline.reserved,
			" memory_baseline_shared_pcm=", baseline.shared_pcm,
			" memory_baseline_working=", baseline.working,
			" memory_baseline_peak=", baseline.peak,
			" memory_used_after_warmup=", warmed.used,
			" memory_used_before_teardown=", before_teardown.used,
			" memory_peak_before_teardown=", before_teardown.peak,
			" memory_used_after_teardown=", after.used,
			" memory_after_reserved=", after.reserved,
			" memory_after_shared_pcm=", after.shared_pcm,
			" memory_after_working=", after.working,
			" retained_caches=none_after_playback_and_stream_destruction");
	fflush(stdout);

	CHECK(wall_hist.n > 10);
	CHECK(energy > 0.0);
	CHECK(after.used == baseline.used);
	CHECK(after.reserved == baseline.reserved);
	CHECK(after.shared_pcm == baseline.shared_pcm);
	CHECK(after.working == baseline.working);
	if (pass == 0) {
		first_warmup_used = warmed.used;
		peak_after_first = after.peak;
	} else {
		CHECK(warmed.used == first_warmup_used);
		CHECK(after.peak == peak_after_first);
	}
	if (pass_gate) {
		REQUIRE(p99_hi != UINT64_MAX);
		CHECK(p99_hi * 2 < deadline_us);
		if (have_cpu_p99) {
			REQUIRE(cpu_p99_hi != UINT64_MAX);
			CHECK(cpu_p99_hi * 2 < deadline_us);
		}
		// Wall-clock spikes whose on-CPU time stayed under the deadline are
		// scheduler stalls. A mix miss is on-CPU time over the deadline.
		// Device underruns are the driver counter above, not this tight loop.
		CHECK(mix_deadline_misses == 0);
		if (!cpu_known) {
			CHECK(wall_misses == 0);
		}
		CHECK(unclassified_wall_misses == 0);
	}
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

TEST_CASE("[Symphony][Profile] repeated workload returns to the byte baseline") {
	_run_workload(_web(true), 48000.0f, 128, MIN(1, _profile_seconds()), false, true);
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

TEST_CASE("[Symphony][Profile] qualification histogram reports bucket width") {
	CHECK(symphony_meter_bucket(0) == 0);
	CHECK(symphony_meter_bucket(1) == 1);
	CHECK(symphony_meter_bucket(43) == 6);
	uint32_t counts[SYMPHONY_METER_BUCKETS] = {};
	counts[6] = 100;
	uint64_t lo = 0;
	uint64_t hi = 0;
	CHECK(symphony_meter_percentile(counts, SYMPHONY_METER_BUCKETS, 100, 0.99, lo, hi));
	CHECK(lo == 32);
	CHECK(hi == 64);

	SymphonyVoiceManager *vm = SymphonyVoiceManager::get_singleton();
	REQUIRE(vm != nullptr);
	vm->set_qualification_generation_map_size(-1);
	Dictionary hidden = vm->get_qualification_report();
	CHECK(String(hidden["generation_map_size"]) == "unavailable");
	CHECK(String(hidden["driver_underruns"]) == "unavailable");
	if (AudioDriver::get_singleton() == nullptr || AudioDriver::get_singleton()->get_output_underrun_count() < 0) {
		CHECK(String(hidden["worklet_underruns"]) == "unavailable");
	}
	vm->set_qualification_generation_map_size(3);
	Dictionary shown = vm->get_qualification_report();
	CHECK((int64_t)shown["generation_map_size"] == 3);
	vm->set_qualification_generation_map_size(-1);
}

} // namespace TestSymphonyProfile
