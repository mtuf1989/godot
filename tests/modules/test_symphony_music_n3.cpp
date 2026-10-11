/**************************************************************************/
/*  test_symphony_music_n3.cpp                                            */
/*  Suite: [Symphony][N3] — recorded phrases, stretch, stereo PCM.        */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_music_n3)

#include "modules/symphony/core/symphony_rate_policy.h"
#include "modules/symphony/runtime/symphony_music_stereo_pcm.h"
#include "modules/symphony/runtime/symphony_music_stretch.h"
#include "modules/symphony/runtime/symphony_note_pool.h"
#include "modules/symphony/core/symphony_graph_description.h"
#include "modules/symphony/stream/audio_stream_playback_symphony_music.h"
#include "modules/symphony/stream/audio_stream_symphony.h"
#include "modules/symphony/stream/audio_stream_symphony_music.h"
#include "tests/modules/symphony_test_helpers.h"

#include "core/math/math_funcs.h"
#include "core/string/ustring.h"
#include "servers/audio/audio_frame.h"
#include "servers/audio/audio_server.h"

#include <cmath>

namespace TestSymphonyMusicN3 {

static void _make_stereo_tone(Vector<int16_t> &r_pcm, int p_frames, float p_rate, float p_hz) {
	r_pcm.resize(p_frames * 2);
	for (int i = 0; i < p_frames; i++) {
		const float t = (float)i / p_rate;
		const float s = Math::sin(Math::TAU * p_hz * t) * 0.5f;
		r_pcm.write[i * 2] = (int16_t)CLAMP(s * 32767.0f, -32768.0f, 32767.0f);
		r_pcm.write[i * 2 + 1] = r_pcm[i * 2];
	}
}

static PackedFloat32Array _make_click_pcm(int p_frames = 8192) {
	PackedFloat32Array pcm;
	pcm.resize(p_frames * 2);
	pcm.fill(0.0f);
	const int burst = MIN(p_frames, 4096);
	for (int i = 0; i < burst; i++) {
		pcm.write[i * 2] = 0.9f;
		pcm.write[i * 2 + 1] = 0.9f;
	}
	return pcm;
}

static GraphDescription _make_synth() {
	GraphDescription desc;
	desc.smooth_parameters = false;
	NodeDesc note;
	note.id = 1;
	note.type_name = "MusicNoteInput";
	note.params["root_hz"] = 440.0f;
	desc.nodes.push_back(note);
	NodeDesc osc;
	osc.id = 2;
	osc.type_name = "Oscillator";
	osc.params["waveform"] = 0.0f;
	desc.nodes.push_back(osc);
	NodeDesc adsr;
	adsr.id = 3;
	adsr.type_name = "ADSR";
	adsr.params["attack"] = 0.001f;
	adsr.params["decay"] = 0.02f;
	adsr.params["sustain"] = 0.8f;
	adsr.params["release"] = 0.05f;
	desc.nodes.push_back(adsr);
	NodeDesc gain;
	gain.id = 4;
	gain.type_name = "Gain";
	gain.params["gain"] = 0.2f;
	desc.nodes.push_back(gain);
	NodeDesc out;
	out.id = 5;
	out.type_name = "GraphOutput";
	desc.nodes.push_back(out);
	auto c = [&](int a, int ap, int b, int bp) {
		ConnectionDesc x;
		x.from_node = a;
		x.from_pin = ap;
		x.to_node = b;
		x.to_pin = bp;
		desc.connections.push_back(x);
	};
	c(1, 0, 2, 0);
	c(2, 0, 3, 0);
	c(1, 6, 3, 1);
	c(3, 0, 4, 0);
	c(4, 0, 5, 0);
	return desc;
}

TEST_CASE("[Symphony][N3] Stereo cache acquire/release and budget") {
	SymphonyMusicStereoCache *cache = SymphonyMusicStereoCache::get_singleton();
	REQUIRE(cache != nullptr);
	const int before = cache->get_entry_count();

	Vector<float> pcm;
	pcm.resize(256);
	for (int i = 0; i < 256; i++) {
		pcm.write[i] = (float)(i & 1);
	}
	const StringName key("n3_test_tone");
	const float *p = cache->acquire(key, pcm.ptr(), 128, 48000.0f);
	REQUIRE(p != nullptr);
	CHECK(cache->get_frames(key) == 128);
	CHECK(cache->get_sample_rate(key) == doctest::Approx(48000.0f));
	CHECK(cache->get_entry_count() == before + 1);

	const float *p2 = cache->acquire(key, pcm.ptr(), 128, 48000.0f);
	CHECK(p2 == p);
	cache->release(key);
	CHECK(cache->get_entry_count() == before + 1);
	cache->release(key);
	CHECK(cache->get_entry_count() == before);
}

TEST_CASE("[Symphony][N3] Resample helper preserves tone energy") {
	Vector<int16_t> src;
	_make_stereo_tone(src, 4800, 48000.0f, 440.0f);
	Vector<float> dst;
	REQUIRE(symphony_music_resample_to_stereo_float(src.ptr(), 4800, true, 48000.0f, 44100.0f, dst));
	CHECK(dst.size() > 1000);
	Vector<float> mono;
	mono.resize(dst.size() / 2);
	for (int i = 0; i < mono.size(); i++) {
		mono.write[i] = dst[i * 2];
	}
	const float level = SymphonyTestHelpers::tone_level_db(mono, 44100.0f, 440.0f);
	CHECK(level > -20.0f);
}

TEST_CASE("[Symphony][N3] Stretch context tempo/transpose envelope") {
	Vector<int16_t> src;
	const float rate = 48000.0f;
	_make_stereo_tone(src, (int)(rate * 1.0f), rate, 440.0f);
	Vector<float> stereo;
	REQUIRE(symphony_music_resample_to_stereo_float(src.ptr(), src.size() / 2, true, rate, rate, stereo));

	SymphonyMusicStereoCache *cache = SymphonyMusicStereoCache::get_singleton();
	const StringName key("n3_stretch_clip");
	const float *pcm = cache->acquire(key, stereo.ptr(), stereo.size() / 2, rate);
	REQUIRE(pcm != nullptr);

	SymphonyMusicClipDesc clip;
	clip.pcm_key = key;
	clip.pcm = pcm;
	clip.frames = stereo.size() / 2;
	clip.sample_rate = rate;
	clip.source_bpm = 120.0f;
	clip.body_start = 0;
	clip.body_end = clip.frames;
	clip.tempo_ratio_min = 0.8f;
	clip.tempo_ratio_max = 1.25f;
	clip.transpose_min_st = -6;
	clip.transpose_max_st = 6;

	SymphonyStretchPool pool;
	// Outside envelope → clamped.
	SymphonyStretchContext *ctx = pool.acquire_prepared(clip, 2.0f, 12.0f);
	REQUIRE(ctx != nullptr);
	CHECK(ctx->tempo_ratio == doctest::Approx(1.25f));
	CHECK(ctx->transpose_st == doctest::Approx(6.0f));
	CHECK(ctx->common_latency > 0);
	REQUIRE(ctx->activate());

	// Output latency means the first blocks are pre-roll silence — render past it.
	const int need = ctx->common_latency + 2048;
	Vector<AudioFrame> out;
	out.resize(need);
	for (int i = 0; i < need; i++) {
		out.write[i] = AudioFrame(0, 0);
	}
	for (int off = 0; off < need; off += 512) {
		const int n = MIN(512, need - off);
		ctx->process_add(out.ptrw() + off, n);
	}
	float peak = 0.0f;
	for (int i = ctx->common_latency; i < need; i++) {
		peak = MAX(peak, Math::abs(out[i].left));
	}
	CHECK(peak > 1e-6f);

	ctx->request_return();
	pool.reclaim_returned();
	CHECK(ctx->is_free());
	cache->release(key);
}

TEST_CASE("[Symphony][N3] Hybrid score: synth + stretched clip with common latency") {
	const float rate = symphony_offline_rate();
	Vector<int16_t> src;
	_make_stereo_tone(src, (int)(rate * 0.5f), rate, 330.0f);
	Vector<float> stereo;
	REQUIRE(symphony_music_resample_to_stereo_float(src.ptr(), src.size() / 2, true, rate, rate, stereo));

	SymphonyMusicStereoCache *cache = SymphonyMusicStereoCache::get_singleton();
	const StringName key("n3_hybrid");
	const float *pcm = cache->acquire(key, stereo.ptr(), stereo.size() / 2, rate);
	REQUIRE(pcm != nullptr);

	SymphonyMusicClipDesc clip;
	clip.pcm = pcm;
	clip.frames = stereo.size() / 2;
	clip.sample_rate = rate;
	clip.body_end = clip.frames;

	SymphonyStretchPool stretch_pool;
	SymphonyStretchContext *ctx = stretch_pool.acquire_prepared(clip, 1.0f, 0.0f);
	REQUIRE(ctx != nullptr);
	REQUIRE(ctx->activate());
	const int latency = stretch_pool.common_latency();
	CHECK(latency > 0);

	SymphonyNotePool notes;
	REQUIRE(notes.prepare(_make_synth(), rate, 2, 0x3EULL));
	// Delay note-on by common latency so synth aligns with stretch pre-roll centre.
	notes.note_on(1, 69, 0.8f, 0, 0);

	const int total = (int)(rate * 0.35f);
	Vector<float> mono;
	mono.resize(total);
	Vector<AudioFrame> block;
	block.resize(SYMPHONY_MICRO_BLOCK_SIZE);
	Vector<AudioFrame> delay;
	delay.resize(latency + SYMPHONY_MICRO_BLOCK_SIZE);
	for (int i = 0; i < delay.size(); i++) {
		delay.write[i] = AudioFrame(0, 0);
	}
	int delay_w = 0;
	int written = 0;
	while (written < total) {
		const int n = MIN(SYMPHONY_MICRO_BLOCK_SIZE, total - written);
		for (int i = 0; i < n; i++) {
			block.write[i] = AudioFrame(0, 0);
		}
		ctx->process_add(block.ptrw(), n);

		Vector<AudioFrame> synth;
		synth.resize(n);
		notes.render(synth.ptrw(), n, (uint64_t)written);
		// Write synth into delay line, read delayed mix.
		for (int i = 0; i < n; i++) {
			delay.write[(delay_w + i) % delay.size()] = synth[i];
		}
		for (int i = 0; i < n; i++) {
			const int ri = (delay_w + i - latency + delay.size() * 8) % delay.size();
			AudioFrame s = (written + i >= latency) ? delay[ri] : AudioFrame(0, 0);
			mono.write[written + i] = block[i].left + s.left;
		}
		delay_w = (delay_w + n) % delay.size();
		written += n;
	}

	float peak = 0.0f;
	for (int i = 0; i < mono.size(); i++) {
		peak = MAX(peak, Math::abs(mono[i]));
		CHECK(Math::is_finite(mono[i]));
	}
	CHECK(peak > 0.01f);

	ctx->request_return();
	stretch_pool.reclaim_returned();
	cache->release(key);
}

TEST_CASE("[Symphony][B3] Tempo ratio is playback speed with bounded duration") {
	const float rate = 48000.0f;
	Vector<int16_t> src;
	_make_stereo_tone(src, (int)(rate * 0.25f), rate, 440.0f);
	Vector<float> stereo;
	REQUIRE(symphony_music_resample_to_stereo_float(src.ptr(), src.size() / 2, true, rate, rate, stereo));
	SymphonyMusicStereoCache *cache = SymphonyMusicStereoCache::get_singleton();
	const StringName key("b3_tempo_speed");
	const float *pcm = cache->acquire(key, stereo.ptr(), stereo.size() / 2, rate);
	REQUIRE(pcm != nullptr);
	const float ratios[] = { 0.8f, 1.0f, 1.25f };
	for (float ratio : ratios) {
		SymphonyMusicClipDesc clip;
		clip.pcm = pcm;
		clip.frames = stereo.size() / 2;
		clip.sample_rate = rate;
		clip.body_end = clip.frames;
		clip.tempo_ratio_min = 0.8f;
		clip.tempo_ratio_max = 1.25f;
		SymphonyStretchPool pool;
		SymphonyStretchContext *ctx = pool.acquire_prepared(clip, ratio, 0.0f);
		REQUIRE(ctx != nullptr);
		REQUIRE(ctx->activate());
		AudioFrame block[64];
		int produced = 0;
		while (!ctx->finished && produced < (int)(rate * 2.0f)) {
			for (int i = 0; i < 64; i++) {
				block[i] = AudioFrame(0, 0);
			}
			ctx->process_add(block, 64);
			produced += 64;
		}
		const float expect = (float)clip.frames / ratio;
		CHECK(Math::abs((float)produced - expect) < 64.0f * 4.0f);
		pool.reclaim_returned();
	}
	cache->release(key);
}

TEST_CASE("[Symphony][B3] 44.1k source at 48k music rate keeps pitch") {
	Vector<int16_t> src;
	_make_stereo_tone(src, 4410, 44100.0f, 440.0f);
	PackedFloat32Array pcm;
	pcm.resize(src.size());
	for (int i = 0; i < src.size(); i++) {
		pcm.write[i] = (float)src[i] / 32768.0f;
	}
	Ref<AudioStreamSymphonyMusic> music;
	music.instantiate();
	music->set_mix_rate(48000.0f);
	REQUIRE(music->register_clip_pcm("b3_441", pcm, 44100.0f, 120.0f) == OK);
	REQUIRE(music->find_clip_index("b3_441") >= 0);
	float device = 48000.0f;
	if (AudioServer::get_singleton()) {
		device = AudioServer::get_singleton()->get_mix_rate();
	}
	const float music_rate = symphony_select_music_rate(device);
	CHECK(music->get_clip_sample_rate("b3_441") == doctest::Approx(music_rate));
	const int32_t expect_frames = MAX(1, (int32_t)Math::floor(4410.0 * (double)music_rate / 44100.0));
	CHECK(Math::abs(music->get_clip_frames("b3_441") - expect_frames) <= 1);
}

TEST_CASE("[Symphony][B3] Cancelled clip events return stretch contexts") {
	PackedFloat32Array pcm = _make_click_pcm();
	Ref<AudioStreamSymphony> instrument;
	instrument.instantiate();
	instrument->set_graph_description(SymphonyTestHelpers::make_click_instrument());
	Ref<AudioStreamSymphonyMusic> music;
	music.instantiate();
	REQUIRE(music->set_track_instrument(0, instrument, 2, false) == OK);
	REQUIRE(music->register_clip_pcm("b3_cancel", pcm, 48000.0f) == OK);
	Ref<AudioStreamPlaybackSymphonyMusic> pb = music->instantiate_playback();
	pb->start();
	for (int i = 0; i < 20; i++) {
		Dictionary phrase;
		phrase["start_tick"] = 960 * (i + 4);
		phrase["generation"] = i + 1;
		Array events;
		Dictionary clip;
		clip["kind"] = "clip_on";
		clip["tick"] = 960 * (i + 4);
		clip["track"] = 0;
		clip["note_id"] = 100 + i;
		clip["clip_key"] = StringName("b3_cancel");
		events.push_back(clip);
		phrase["events"] = events;
		CHECK(pb->submit_phrase(phrase) == OK);
		CHECK(pb->replace_future(960 * (i + 4), i + 1) == OK);
		AudioFrame chunk[64];
		pb->mix_graph_rate(chunk, 64);
		pb->drain_status();
	}
	CHECK(pb->_test_free_stretch_contexts() == SymphonyStretchPool::MAX_CONTEXTS);

	const Dictionary snap = pb->get_transport_snapshot();
	const int64_t start_tick = (int64_t)snap["tick"] + 960;
	Dictionary live;
	live["start_tick"] = start_tick;
	live["generation"] = 99;
	Array live_events;
	Dictionary live_clip;
	live_clip["kind"] = "clip_on";
	live_clip["tick"] = start_tick;
	live_clip["track"] = 0;
	live_clip["note_id"] = 200;
	live_clip["clip_key"] = StringName("b3_cancel");
	live_events.push_back(live_clip);
	live["events"] = live_events;
	REQUIRE(pb->submit_phrase(live) == OK);
	Vector<AudioFrame> audio = SymphonyTestHelpers::render_music(pb.ptr(), 48000, 128);
	float peak = 0.0f;
	for (int i = 0; i < audio.size(); i++) {
		peak = MAX(peak, Math::abs(audio[i].left));
	}
	CHECK(peak > 1e-4f);
	pb->drain_status();
	pb->stop();
}

TEST_CASE("[Symphony][B3] Re-register longer PCM and MAX_CLIPS do not leak") {
	SymphonyMusicStereoCache *cache = SymphonyMusicStereoCache::get_singleton();
	const int before = cache->get_entry_count();
	Ref<AudioStreamSymphonyMusic> music;
	music.instantiate();
	PackedFloat32Array short_pcm;
	short_pcm.resize(8);
	short_pcm.fill(0.1f);
	REQUIRE(music->register_clip_pcm("b3_rereg", short_pcm, 48000.0f) == OK);
	PackedFloat32Array long_pcm;
	long_pcm.resize(64);
	long_pcm.fill(0.2f);
	REQUIRE(music->register_clip_pcm("b3_rereg", long_pcm, 48000.0f) == OK);
	CHECK(music->get_clip_frames("b3_rereg") > 8);
	const int after_one = cache->get_entry_count();
	int registered = 1;
	for (int i = 0; i < AudioStreamSymphonyMusic::MAX_CLIPS + 4; i++) {
		PackedFloat32Array p;
		p.resize(8);
		p.fill(0.01f * (float)i);
		const Error err = music->register_clip_pcm(StringName(vformat("b3_cap_%d", i)), p, 48000.0f);
		if (err == OK) {
			registered++;
		} else {
			CHECK(err == ERR_OUT_OF_MEMORY);
		}
	}
	CHECK(registered <= AudioStreamSymphonyMusic::MAX_CLIPS);
	CHECK(cache->get_entry_count() <= before + AudioStreamSymphonyMusic::MAX_CLIPS);
	(void)after_one;
}

TEST_CASE("[Symphony][B3] Click clip and click note onsets align within 48 samples") {
	PackedFloat32Array pcm = _make_click_pcm();
	Ref<AudioStreamSymphony> instrument;
	instrument.instantiate();
	instrument->set_graph_description(SymphonyTestHelpers::make_click_instrument());
	Ref<AudioStreamSymphonyMusic> music;
	music.instantiate();
	REQUIRE(music->set_track_instrument(0, instrument, 2, false) == OK);
	REQUIRE(music->register_clip_pcm("b3_click", pcm, 48000.0f) == OK);

	auto make_pb = [&]() -> Ref<AudioStreamPlaybackSymphonyMusic> {
		Ref<AudioStreamPlaybackSymphonyMusic> pb = music->instantiate_playback();
		pb->start();
		return pb;
	};
	// Arm latency compensation with a far-future clip so the note path matches hybrid playback.
	auto note_phrase = []() {
		Dictionary phrase;
		phrase["start_tick"] = 0;
		phrase["generation"] = 1;
		Array events;
		Dictionary on;
		on["kind"] = "note_on";
		on["tick"] = 0;
		on["track"] = 0;
		on["note_id"] = 1;
		on["midi"] = 60;
		on["duration_ticks"] = 240;
		events.push_back(on);
		Dictionary dummy;
		dummy["kind"] = "clip_on";
		dummy["tick"] = 960 * 32;
		dummy["track"] = 0;
		dummy["note_id"] = 99;
		dummy["clip_key"] = StringName("b3_click");
		events.push_back(dummy);
		phrase["events"] = events;
		return phrase;
	};
	auto clip_phrase = []() {
		Dictionary phrase;
		phrase["start_tick"] = 0;
		phrase["generation"] = 1;
		Array events;
		Dictionary clip;
		clip["kind"] = "clip_on";
		clip["tick"] = 0;
		clip["track"] = 0;
		clip["note_id"] = 2;
		clip["clip_key"] = StringName("b3_click");
		events.push_back(clip);
		phrase["events"] = events;
		return phrase;
	};

	Ref<AudioStreamPlaybackSymphonyMusic> note_pb = make_pb();
	REQUIRE(note_pb->submit_phrase(note_phrase()) == OK);
	Vector<AudioFrame> note_audio = SymphonyTestHelpers::render_music(note_pb.ptr(), 16384, 64);
	const int note_onset = SymphonyTestHelpers::first_onset_frame(note_audio, 0.05f, 0);
	note_pb->stop();

	Ref<AudioStreamPlaybackSymphonyMusic> clip_pb = make_pb();
	REQUIRE(clip_pb->submit_phrase(clip_phrase()) == OK);
	Vector<AudioFrame> clip_audio = SymphonyTestHelpers::render_music(clip_pb.ptr(), 16384, 64);
	const int clip_onset = SymphonyTestHelpers::first_onset_frame(clip_audio, 0.05f, 0);
	clip_pb->stop();

	REQUIRE(note_onset >= 0);
	REQUIRE(clip_onset >= 0);
	CHECK(Math::abs(note_onset - clip_onset) <= 48);
}

TEST_CASE("[Symphony][N3] Signalsmith Stretch version pin is 1.3.x") {
	CHECK(signalsmith::stretch::SignalsmithStretch<float>::version[0] == 1);
	CHECK(signalsmith::stretch::SignalsmithStretch<float>::version[1] == 3);
}

} // namespace TestSymphonyMusicN3
