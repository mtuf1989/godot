/**************************************************************************/
/*  test_symphony_foundation.cpp                                          */
/*  Suite: [Symphony][Foundation] — P1b promotion, seeds, position, rate. */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_foundation)

#include "modules/symphony/core/symphony_graph_compiler.h"
#include "modules/symphony/core/symphony_graph_description.h"
#include "modules/symphony/core/symphony_prepared_graph_package.h"
#include "modules/symphony/core/symphony_render_seed.h"
#include "modules/symphony/core/symphony_seed.h"
#include "modules/symphony/core/symphony_seqlock.h"
#include "modules/symphony/nodes/io/symphony_graph_input.h"
#include "modules/symphony/nodes/io/symphony_graph_output.h"
#include "modules/symphony/nodes/io/symphony_trigger_input.h"
#include "modules/symphony/stream/audio_stream_playback_symphony.h"
#include "modules/symphony/stream/audio_stream_symphony.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "core/os/thread.h"
#include "servers/audio/audio_driver_dummy.h"
#include "servers/audio/audio_server.h"
#include "tests/modules/symphony_test_helpers.h"

#include <atomic>
#include <cmath>

namespace TestSymphonyFoundation {

using SymphonyTestHelpers::db_ratio;
using SymphonyTestHelpers::make_sine_graph;
using SymphonyTestHelpers::measure_hz;
using SymphonyTestHelpers::rms;

static GraphDescription make_promoted_gain_graph() {
	GraphDescription desc;
	desc.smooth_parameters = false;

	NodeDesc input;
	input.id = 1;
	input.type_name = "GraphInput";
	input.params.insert("parameter_name", "amount");
	input.params.insert("default_value", 0.0f);
	desc.nodes.push_back(input);

	NodeDesc map;
	map.id = 2;
	map.type_name = "MapRange";
	map.params.insert("in_min", 0.0f);
	map.params.insert("in_max", 1.0f);
	map.params.insert("out_min", 0.0f);
	map.params.insert("out_max", 1.0f);
	map.params.insert("clamp", 1.0f);
	desc.nodes.push_back(map);

	NodeDesc gain;
	gain.id = 3;
	gain.type_name = "Gain";
	gain.params.insert("gain", 1.0f);
	desc.nodes.push_back(gain);

	NodeDesc out;
	out.id = 4;
	out.type_name = "GraphOutput";
	desc.nodes.push_back(out);

	ConnectionDesc a;
	a.from_node = 1;
	a.from_pin = 0;
	a.to_node = 2;
	a.to_pin = 0;
	desc.connections.push_back(a);

	ConnectionDesc b;
	b.from_node = 2;
	b.from_pin = 0;
	b.to_node = 3;
	b.to_pin = 0;
	desc.connections.push_back(b);

	ConnectionDesc c;
	c.from_node = 3;
	c.from_pin = 0;
	c.to_node = 4;
	c.to_pin = 0;
	desc.connections.push_back(c);
	return desc;
}

static GraphDescription make_pitched_gate_graph() {
	GraphDescription desc;
	desc.smooth_parameters = false;

	NodeDesc pitch;
	pitch.id = 1;
	pitch.type_name = "GraphInput";
	pitch.params.insert("parameter_name", "pitch");
	pitch.params.insert("default_value", 220.0f);
	desc.nodes.push_back(pitch);

	NodeDesc osc;
	osc.id = 2;
	osc.type_name = "Oscillator";
	osc.params.insert("waveform", 0.0f);
	osc.params.insert("frequency", 220.0f);
	desc.nodes.push_back(osc);

	NodeDesc gate;
	gate.id = 3;
	gate.type_name = "TriggerInput";
	gate.params.insert("trigger_name", "gate");
	gate.params.insert("auto_trigger_on_play", 0.0f);
	desc.nodes.push_back(gate);

	NodeDesc adsr;
	adsr.id = 4;
	adsr.type_name = "ADSR";
	adsr.params.insert("attack", 0.0f);
	adsr.params.insert("decay", 0.0f);
	adsr.params.insert("sustain", 1.0f);
	adsr.params.insert("release", 0.2f);
	desc.nodes.push_back(adsr);

	NodeDesc out;
	out.id = 5;
	out.type_name = "GraphOutput";
	desc.nodes.push_back(out);

	ConnectionDesc pitch_edge;
	pitch_edge.from_node = 1;
	pitch_edge.from_pin = 0;
	pitch_edge.to_node = 2;
	pitch_edge.to_pin = 0;
	desc.connections.push_back(pitch_edge);

	ConnectionDesc osc_edge;
	osc_edge.from_node = 2;
	osc_edge.from_pin = 0;
	osc_edge.to_node = 4;
	osc_edge.to_pin = 0;
	desc.connections.push_back(osc_edge);

	ConnectionDesc gate_edge;
	gate_edge.from_node = 3;
	gate_edge.from_pin = 0;
	gate_edge.to_node = 4;
	gate_edge.to_pin = 1;
	desc.connections.push_back(gate_edge);

	ConnectionDesc out_edge;
	out_edge.from_node = 4;
	out_edge.from_pin = 0;
	out_edge.to_node = 5;
	out_edge.to_pin = 0;
	desc.connections.push_back(out_edge);
	return desc;
}

static GraphDescription make_noise_graph(bool p_deterministic) {
	GraphDescription desc;
	desc.smooth_parameters = false;
	NodeDesc noise;
	noise.id = 1;
	noise.type_name = "Noise";
	noise.params.insert("mode", 0.0f);
	noise.params.insert("seed", 7.0f);
	noise.params.insert("deterministic_seed", p_deterministic ? 1.0f : 0.0f);
	desc.nodes.push_back(noise);
	NodeDesc out;
	out.id = 2;
	out.type_name = "GraphOutput";
	desc.nodes.push_back(out);
	ConnectionDesc conn;
	conn.from_node = 1;
	conn.from_pin = 0;
	conn.to_node = 2;
	conn.to_pin = 0;
	desc.connections.push_back(conn);
	return desc;
}

static int count_zero_crossings(const AudioFrame *p_frames, int p_count) {
	int crossings = 0;
	for (int i = 1; i < p_count; i++) {
		const float previous = p_frames[i - 1].left;
		const float sample = p_frames[i].left;
		if ((previous <= 0.0f && sample > 0.0f) || (previous >= 0.0f && sample < 0.0f)) {
			crossings++;
		}
	}
	return crossings;
}

TEST_CASE("[Symphony][Foundation] Float to audio promotion is same-block") {
	GraphCompiler::CompileResult compiled = GraphCompiler::compile(make_promoted_gain_graph(), 48000.0f);
	REQUIRE(compiled.success());

	SymphonyGraphInput *amount = nullptr;
	SymphonyGraphOutput *output = nullptr;
	for (int32_t i = 0; i < compiled.graph->operator_count; i++) {
		if (!amount) {
			amount = dynamic_cast<SymphonyGraphInput *>(compiled.graph->operators[i]);
		}
		if (!output) {
			output = dynamic_cast<SymphonyGraphOutput *>(compiled.graph->operators[i]);
		}
	}
	REQUIRE(amount != nullptr);
	REQUIRE(output != nullptr);

	AudioFrame block[SYMPHONY_MICRO_BLOCK_SIZE];
	output->set_output(block, 0);
	compiled.graph->execute(SYMPHONY_MICRO_BLOCK_SIZE);
	CHECK(block[0].left == doctest::Approx(0.0f).epsilon(0.0001f));

	amount->set_value(0.75f);
	output->set_output(block, 0);
	compiled.graph->execute(SYMPHONY_MICRO_BLOCK_SIZE);
	CHECK(block[0].left == doctest::Approx(0.75f).epsilon(0.001f));
	CHECK(block[SYMPHONY_MICRO_BLOCK_SIZE - 1].left == doctest::Approx(0.75f).epsilon(0.001f));

	memdelete(compiled.graph);
}

TEST_CASE("[Symphony][Foundation] Pitch and gate in one block use the new pitch") {
	GraphCompiler::CompileResult compiled = GraphCompiler::compile(make_pitched_gate_graph(), 48000.0f);
	REQUIRE(compiled.success());

	SymphonyGraphInput *pitch = nullptr;
	SymphonyTriggerInput *gate = nullptr;
	SymphonyGraphOutput *output = nullptr;
	for (int32_t i = 0; i < compiled.graph->operator_count; i++) {
		SymphonyOperator *op = compiled.graph->operators[i];
		if (!pitch) {
			pitch = dynamic_cast<SymphonyGraphInput *>(op);
		}
		if (!gate) {
			gate = dynamic_cast<SymphonyTriggerInput *>(op);
		}
		if (!output) {
			output = dynamic_cast<SymphonyGraphOutput *>(op);
		}
	}
	REQUIRE(pitch != nullptr);
	REQUIRE(gate != nullptr);
	REQUIRE(output != nullptr);

	AudioFrame block[SYMPHONY_MICRO_BLOCK_SIZE];
	output->set_output(block, 0);
	compiled.graph->execute(SYMPHONY_MICRO_BLOCK_SIZE);

	pitch->set_value(2000.0f);
	REQUIRE(gate->fire(1.0f));
	output->set_output(block, 0);
	compiled.graph->execute(SYMPHONY_MICRO_BLOCK_SIZE);

	float samples[SYMPHONY_MICRO_BLOCK_SIZE];
	for (int i = 0; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
		samples[i] = block[i].left;
	}
	// 220 Hz barely moves in one 64-sample block. 2 kHz crosses zero several times.
	CHECK(rms(samples, SYMPHONY_MICRO_BLOCK_SIZE) > 0.2f);
	CHECK(count_zero_crossings(block, SYMPHONY_MICRO_BLOCK_SIZE) >= 4);

	memdelete(compiled.graph);
}

struct SeqPayload {
	uint32_t a = 0;
	uint32_t b = 0;
	uint32_t c = 0;
	uint32_t checksum = 0;
};
static_assert(sizeof(SeqPayload) % 4 == 0, "seqlock test payload");

TEST_CASE("[Symphony][Foundation] SeqLock two readers see no torn words") {
	SymphonySeqLock<SeqPayload> lock;
	std::atomic<bool> stop{ false };
	std::atomic<int> torn{ 0 };
	std::atomic<int> reads{ 0 };

	struct Ctx {
		SymphonySeqLock<SeqPayload> *lock;
		std::atomic<bool> *stop;
		std::atomic<int> *torn;
		std::atomic<int> *reads;
	} ctx{ &lock, &stop, &torn, &reads };

	SeqPayload initial;
	initial.a = 1;
	initial.b = 3;
	initial.c = 7;
	initial.checksum = initial.a ^ initial.b ^ initial.c;
	lock.store(initial);

	auto reader_fn = [](void *ud) {
		Ctx *c = (Ctx *)ud;
		while (!c->stop->load(std::memory_order_relaxed)) {
			SeqPayload payload;
			bool ok = true;
			if ((c->reads->load(std::memory_order_relaxed) & 1) != 0) {
				payload = c->lock->load();
			} else {
				ok = c->lock->try_load(payload);
			}
			if (!ok) {
				continue;
			}
			c->reads->fetch_add(1, std::memory_order_relaxed);
			const uint32_t expect = payload.a ^ payload.b ^ payload.c;
			if (payload.checksum != expect || payload.b != payload.a * 3u || payload.c != payload.a * 7u) {
				c->torn->fetch_add(1, std::memory_order_relaxed);
			}
		}
	};

	Thread readers[2];
	readers[0].start(reader_fn, &ctx);
	readers[1].start(reader_fn, &ctx);

	const uint64_t deadline = OS::get_singleton()->get_ticks_usec() + 2000000;
	uint32_t n = 1;
	while (OS::get_singleton()->get_ticks_usec() < deadline) {
		SeqPayload payload;
		payload.a = n;
		payload.b = n * 3u;
		payload.c = n * 7u;
		payload.checksum = payload.a ^ payload.b ^ payload.c;
		lock.store(payload);
		n++;
	}
	stop.store(true, std::memory_order_relaxed);
	readers[0].wait_to_finish();
	readers[1].wait_to_finish();

	CHECK(torn.load() == 0);
	CHECK(reads.load() > 1000);
}

static void render_playback(AudioStreamPlaybackSymphony *p_playback, Vector<float> &r_left, int p_frames) {
	r_left.resize(p_frames);
	AudioFrame chunk[256];
	int filled = 0;
	while (filled < p_frames) {
		const int ask = MIN(256, p_frames - filled);
		const int got = p_playback->mix_graph_rate(chunk, ask);
		if (got <= 0) {
			break;
		}
		for (int i = 0; i < got && filled < p_frames; i++) {
			r_left.write[filled++] = chunk[i].left;
		}
	}
	if (filled < p_frames) {
		r_left.resize(filled);
	}
}

TEST_CASE("[Symphony][Foundation] Noise voices differ and sum near +3 dB") {
	Ref<AudioStreamSymphony> stream;
	stream.instantiate();
	stream->set_mix_rate(48000.0f);
	stream->set_graph_description(make_noise_graph(false));

	SymphonyRenderSeed::set(0);
	Ref<AudioStreamPlayback> base_a = stream->instantiate_playback();
	Ref<AudioStreamPlayback> base_b = stream->instantiate_playback();
	Ref<AudioStreamPlaybackSymphony> a = base_a;
	Ref<AudioStreamPlaybackSymphony> b = base_b;
	REQUIRE(a.is_valid());
	REQUIRE(b.is_valid());
	a->start();
	b->start();

	const int frames = 32768;
	Vector<float> left_a;
	Vector<float> left_b;
	render_playback(a.ptr(), left_a, frames);
	render_playback(b.ptr(), left_b, frames);
	REQUIRE(left_a.size() == frames);
	REQUIRE(left_b.size() == frames);

	int different = 0;
	Vector<float> sum;
	sum.resize(frames);
	for (int i = 0; i < frames; i++) {
		if (left_a[i] != left_b[i]) {
			different++;
		}
		sum.write[i] = left_a[i] + left_b[i];
	}
	CHECK(different > frames / 2);

	const float gain_db = db_ratio(rms(sum.ptr(), frames), rms(left_a.ptr(), frames));
	CHECK(std::abs(gain_db - 3.0103f) <= 0.5f);
}

TEST_CASE("[Symphony][Foundation] Render seed repeats and deterministic seed ignores the voice") {
	Ref<AudioStreamSymphony> stream;
	stream.instantiate();
	stream->set_mix_rate(48000.0f);
	stream->set_graph_description(make_noise_graph(false));

	struct ClearRenderSeed {
		~ClearRenderSeed() { SymphonyRenderSeed::set(0); }
	} clear_render_seed;
	SymphonyRenderSeed::set(4242);
	Ref<AudioStreamPlayback> base_a = stream->instantiate_playback();
	Ref<AudioStreamPlayback> base_b = stream->instantiate_playback();
	SymphonyRenderSeed::set(0);
	Ref<AudioStreamPlaybackSymphony> a = base_a;
	Ref<AudioStreamPlaybackSymphony> b = base_b;
	REQUIRE(a.is_valid());
	REQUIRE(b.is_valid());
	a->start();
	b->start();

	const int frames = 4096;
	Vector<float> left_a;
	Vector<float> left_b;
	render_playback(a.ptr(), left_a, frames);
	render_playback(b.ptr(), left_b, frames);
	REQUIRE(left_a.size() == frames);
	REQUIRE(left_b.size() == frames);
	for (int i = 0; i < frames; i++) {
		CHECK(left_a[i] == left_b[i]);
	}

	Ref<AudioStreamSymphony> det_stream;
	det_stream.instantiate();
	det_stream->set_mix_rate(48000.0f);
	det_stream->set_graph_description(make_noise_graph(true));
	SymphonyRenderSeed::set(0);
	Ref<AudioStreamPlayback> det_base_a = det_stream->instantiate_playback();
	Ref<AudioStreamPlayback> det_base_b = det_stream->instantiate_playback();
	Ref<AudioStreamPlaybackSymphony> det_a = det_base_a;
	Ref<AudioStreamPlaybackSymphony> det_b = det_base_b;
	det_a->start();
	det_b->start();
	Vector<float> det_left_a;
	Vector<float> det_left_b;
	render_playback(det_a.ptr(), det_left_a, frames);
	render_playback(det_b.ptr(), det_left_b, frames);
	REQUIRE(det_left_a.size() == frames);
	REQUIRE(det_left_b.size() == frames);
	for (int i = 0; i < frames; i++) {
		CHECK(det_left_a[i] == det_left_b[i]);
	}
}

TEST_CASE("[Symphony][Foundation] LOD state migration keeps the noise sequence continuous") {
	const GraphDescription desc = make_noise_graph(false);
	const uint64_t voice_seed = 99;
	GraphCompiler::CompileResult first = GraphCompiler::compile(desc, 48000.0f, String(), voice_seed);
	GraphCompiler::CompileResult second = GraphCompiler::compile(desc, 48000.0f, String(), voice_seed);
	GraphCompiler::CompileResult fresh = GraphCompiler::compile(desc, 48000.0f, String(), voice_seed);
	REQUIRE(first.success());
	REQUIRE(second.success());
	REQUIRE(fresh.success());

	PreparedGraphPackage *pkg_a = PreparedGraphPackage::create_from_graph(first.graph);
	PreparedGraphPackage *pkg_b = PreparedGraphPackage::create_from_graph(second.graph);
	PreparedGraphPackage *pkg_c = PreparedGraphPackage::create_from_graph(fresh.graph);
	REQUIRE(pkg_a != nullptr);
	REQUIRE(pkg_b != nullptr);
	REQUIRE(pkg_c != nullptr);

	AudioFrame discard[SYMPHONY_MICRO_BLOCK_SIZE];
	for (int i = 0; i < 2; i++) {
		pkg_a->graph_output->set_output(discard, 0);
		pkg_a->graph->execute(SYMPHONY_MICRO_BLOCK_SIZE);
	}
	PreparedGraphPackage::migrate_compatible_state(pkg_a, pkg_b);

	AudioFrame continued[SYMPHONY_MICRO_BLOCK_SIZE];
	AudioFrame migrated[SYMPHONY_MICRO_BLOCK_SIZE];
	AudioFrame restarted[SYMPHONY_MICRO_BLOCK_SIZE];
	pkg_a->graph_output->set_output(continued, 0);
	pkg_a->graph->execute(SYMPHONY_MICRO_BLOCK_SIZE);
	pkg_b->graph_output->set_output(migrated, 0);
	pkg_b->graph->execute(SYMPHONY_MICRO_BLOCK_SIZE);
	pkg_c->graph_output->set_output(restarted, 0);
	pkg_c->graph->execute(SYMPHONY_MICRO_BLOCK_SIZE);

	bool migration_matches = true;
	bool restart_differs = false;
	for (int i = 0; i < SYMPHONY_MICRO_BLOCK_SIZE; i++) {
		if (continued[i].left != migrated[i].left) {
			migration_matches = false;
		}
		if (continued[i].left != restarted[i].left) {
			restart_differs = true;
		}
	}
	CHECK(migration_matches);
	CHECK(restart_differs);

	PreparedGraphPackage::destroy(pkg_a);
	PreparedGraphPackage::destroy(pkg_b);
	PreparedGraphPackage::destroy(pkg_c);
}

TEST_CASE("[Symphony][Foundation] Ten minutes of graph frames stay within one frame") {
	Ref<AudioStreamSymphony> stream;
	stream.instantiate();
	stream->set_mix_rate(48000.0f);
	stream->set_graph_description(make_sine_graph(440.0f));
	Ref<AudioStreamPlayback> base = stream->instantiate_playback();
	Ref<AudioStreamPlaybackSymphony> playback = base;
	REQUIRE(playback.is_valid());
	playback->start();

	const uint64_t total = 48000ull * 600ull;
	AudioFrame chunk[4096];
	uint64_t done = 0;
	while (done < total) {
		const int ask = (int)MIN((uint64_t)4096, total - done);
		const int got = playback->mix_graph_rate(chunk, ask);
		REQUIRE(got == ask);
		done += (uint64_t)got;
	}
	const double position = playback->get_playback_position();
	const double error_frames = std::abs(position * 48000.0 - (double)total);
	CHECK(error_frames < 1.0);
	CHECK(playback->get_frames_rendered() == total);
}

TEST_CASE("[Symphony][Foundation][Audio] render_offline ignores a 44.1k device") {
	AudioServer *server = AudioServer::get_singleton();
	REQUIRE(server != nullptr);
	AudioDriverDummy *dummy = AudioDriverDummy::get_dummy_singleton();
	REQUIRE(dummy != nullptr);
	struct RestoreRate {
		AudioDriverDummy *driver;
		int previous;
		~RestoreRate() {
			if (driver) {
				driver->set_mix_rate(previous);
			}
		}
	} restore_rate{ dummy, dummy->get_mix_rate() };
	dummy->set_mix_rate(44100);
	REQUIRE(server->get_mix_rate() == doctest::Approx(44100.0f));

	Ref<AudioStreamSymphony> stream;
	stream.instantiate();
	stream->set_mix_rate(48000.0f);
	stream->set_graph_description(make_sine_graph(440.0f));

	const String path = String("/tmp/symphony_p1b_sine.wav");
	const double duration = 0.5;
	Dictionary rendered = stream->render_offline(duration, 1, Dictionary(), Array(), path);
	CHECK(bool(rendered["ok"]) == true);
	const int expected_frames = (int)Math::round(duration * 48000.0);
	CHECK((int)rendered["frames"] == expected_frames);
	CHECK((int)rendered["sample_rate"] == 48000);

	Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
	REQUIRE(file.is_valid());
	file->seek(24);
	const uint32_t wav_rate = file->get_32();
	CHECK(wav_rate == 48000);
	file->seek(44);
	Vector<AudioFrame> frames;
	frames.resize(expected_frames);
	for (int i = 0; i < expected_frames; i++) {
		const float left = (float)(int16_t)file->get_16() / 32767.0f;
		const float right = (float)(int16_t)file->get_16() / 32767.0f;
		frames.write[i] = AudioFrame(left, right);
	}
	const float hz = measure_hz(frames, expected_frames / 10, 48000.0f);
	CHECK(std::abs(hz - 440.0f) <= 1.0f);

	DirAccess::remove_absolute(path);
}

} // namespace TestSymphonyFoundation
