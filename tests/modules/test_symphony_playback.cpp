/**************************************************************************/
/*  test_symphony_playback.cpp                                            */
/*  Suite: [Symphony][Playback] — stream playback, swaps, retirement.     */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_playback)

#include "modules/symphony/core/shared_pcm_cache.h"
#include "modules/symphony/core/symphony_arena_allocator.h"
#include "modules/symphony/core/symphony_graph_compiler.h"
#include "modules/symphony/core/symphony_graph_description.h"
#include "modules/symphony/core/symphony_graph_package_retirement.h"
#include "modules/symphony/core/symphony_prepared_graph_package.h"
#include "modules/symphony/core/symphony_operator.h"
#include "modules/symphony/core/symphony_realtime_scope.h"
#include "modules/symphony/core/symphony_voice_manager.h"
#include "modules/symphony/stream/audio_stream_symphony.h"
#include "modules/symphony/stream/audio_stream_playback_symphony.h"
#include "modules/symphony/spatial/spatial_graph_wrapper.h"

#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/object/class_db.h"
#include "core/os/memory.h"
#include "scene/resources/audio/audio_stream_wav.h"
#include "servers/audio/audio_server.h"
#include "tests/test_utils.h"

#include <cmath>
#include <cstring>

namespace TestSymphonyPlayback {

static GraphDescription _make_io_graph() {
	GraphDescription desc;

	NodeDesc gin;
	gin.id = 1;
	gin.type_name = "GraphInput";
	gin.params.insert("parameter_name", "freq");
	gin.params.insert("default_value", 440.0f);
	desc.nodes.push_back(gin);

	NodeDesc tin;
	tin.id = 2;
	tin.type_name = "TriggerInput";
	tin.params.insert("trigger_name", "gate");
	desc.nodes.push_back(tin);

	NodeDesc osc;
	osc.id = 3;
	osc.type_name = "Oscillator";
	osc.params.insert("frequency", 440.0f);
	osc.params.insert("waveform", 0.0f);
	desc.nodes.push_back(osc);

	NodeDesc out;
	out.id = 4;
	out.type_name = "GraphOutput";
	desc.nodes.push_back(out);

	ConnectionDesc c0;
	c0.from_node = 1;
	c0.from_pin = 0;
	c0.to_node = 3;
	c0.to_pin = 0;
	desc.connections.push_back(c0);

	ConnectionDesc c1;
	c1.from_node = 3;
	c1.from_pin = 0;
	c1.to_node = 4;
	c1.to_pin = 0;
	desc.connections.push_back(c1);

	return desc;
}

TEST_CASE("[Symphony][Playback] PreparedGraphPackage builds sorted routes") {
	GraphCompiler::CompileResult result = GraphCompiler::compile(_make_io_graph(), 48000.0f);
	REQUIRE(result.success());
	REQUIRE(result.graph != nullptr);

	PreparedGraphPackage *pkg = PreparedGraphPackage::create_from_graph(result.graph, result.arena_bytes, result.total_package_bytes, 0, result.estimated_cost_units);
	REQUIRE(pkg != nullptr);
	CHECK(pkg->graph_output != nullptr);
	CHECK(pkg->param_routes.size() >= 1);
	CHECK(pkg->find_param(StringName("freq")) != nullptr);
	CHECK(pkg->find_param(StringName("missing")) == nullptr);
	CHECK(pkg->find_trigger(StringName("gate")) != nullptr);
	CHECK(pkg->find_trigger(StringName("missing")) == nullptr);
	CHECK(result.estimated_cost_units > 0.0f);
	CHECK(pkg->estimated_cost_units == doctest::Approx(result.estimated_cost_units));

	PreparedGraphPackage::destroy(pkg);
}

TEST_CASE("[Symphony][Playback] GraphPackageRetirement drain destroys packages") {
	GraphCompiler::CompileResult result = GraphCompiler::compile(_make_io_graph(), 48000.0f);
	REQUIRE(result.success());

	PreparedGraphPackage *pkg = PreparedGraphPackage::create_from_graph(result.graph);
	REQUIRE(pkg != nullptr);

	const uint32_t before = GraphPackageRetirement::get_pending_count();
	GraphPackageRetirement::retire(pkg);
	CHECK(GraphPackageRetirement::get_pending_count() == before + 1);

	GraphPackageRetirement::drain();
	CHECK(GraphPackageRetirement::get_pending_count() == before);
}

TEST_CASE("[Symphony][Playback] Package fingerprints match node id + type") {
	GraphCompiler::CompileResult result = GraphCompiler::compile(_make_io_graph(), 48000.0f);
	REQUIRE(result.success());

	PreparedGraphPackage *pkg = PreparedGraphPackage::create_from_graph(result.graph, result.arena_bytes, result.total_package_bytes);
	REQUIRE(pkg != nullptr);
	REQUIRE(pkg->fingerprints.size() == result.graph->operator_count);

	for (int i = 1; i < pkg->fingerprints.size(); i++) {
		CHECK(pkg->fingerprints[i - 1].node_id <= pkg->fingerprints[i].node_id);
	}

	const PreparedGraphPackage::OperatorFingerprint *osc_fp = pkg->find_fingerprint(3);
	REQUIRE(osc_fp != nullptr);
	CHECK(osc_fp->type_hash == StringName("Oscillator").hash());
	CHECK(osc_fp->structural_hash != 0);
	CHECK(pkg->find_fingerprint(999) == nullptr);

	PreparedGraphPackage::destroy(pkg);
}

TEST_CASE("[Symphony][Playback] migrate_compatible_state copies bounded operator state") {
	GraphCompiler::CompileResult a = GraphCompiler::compile(_make_io_graph(), 48000.0f);
	GraphCompiler::CompileResult b = GraphCompiler::compile(_make_io_graph(), 48000.0f);
	REQUIRE(a.success());
	REQUIRE(b.success());

	PreparedGraphPackage *from = PreparedGraphPackage::create_from_graph(a.graph, a.arena_bytes, a.total_package_bytes);
	PreparedGraphPackage *to = PreparedGraphPackage::create_from_graph(b.graph, b.arena_bytes, b.total_package_bytes);
	REQUIRE(from != nullptr);
	REQUIRE(to != nullptr);

	const PreparedGraphPackage::OperatorFingerprint *osc_fp = from->find_fingerprint(3);
	REQUIRE(osc_fp != nullptr);
	SymphonyOperator *src_osc = from->graph->operators[osc_fp->exec_index];
	SymphonyOperator *dst_osc = to->graph->operators[to->find_fingerprint(3)->exec_index];
	REQUIRE(src_osc != nullptr);
	REQUIRE(dst_osc != nullptr);

	uint8_t seed[16];
	const size_t seed_size = src_osc->export_state(nullptr, 0);
	REQUIRE(seed_size > 0);
	REQUIRE(seed_size <= sizeof(seed));
	// Drive distinct phase via import of a known buffer, then migrate.
	for (size_t i = 0; i < seed_size; i++) {
		seed[i] = (uint8_t)(0xA5 ^ (uint8_t)i);
	}
	src_osc->import_state(seed, seed_size);

	uint8_t before_dst[16] = {};
	dst_osc->export_state(before_dst, sizeof(before_dst));

	PreparedGraphPackage::migrate_compatible_state(from, to);

	uint8_t after_src[16] = {};
	uint8_t after_dst[16] = {};
	CHECK(src_osc->export_state(after_src, sizeof(after_src)) == seed_size);
	CHECK(dst_osc->export_state(after_dst, sizeof(after_dst)) == seed_size);
	CHECK(memcmp(after_src, seed, seed_size) == 0);
	CHECK(memcmp(after_dst, seed, seed_size) == 0);
	CHECK(memcmp(before_dst, after_dst, seed_size) != 0);

	PreparedGraphPackage::destroy(from);
	PreparedGraphPackage::destroy(to);
}

TEST_CASE("[Symphony][Playback] migrate skips mismatched structural fingerprints") {
	GraphDescription desc_a = _make_io_graph();
	GraphDescription desc_b = _make_io_graph();
	// Same node id 3, different operator type → type/structural mismatch.
	for (int i = 0; i < desc_b.nodes.size(); i++) {
		if (desc_b.nodes[i].id == 3) {
			desc_b.nodes.write[i].type_name = "LFO";
			desc_b.nodes.write[i].params.clear();
			desc_b.nodes.write[i].params.insert("frequency", 1.0f);
			desc_b.nodes.write[i].params.insert("waveform", 0.0f);
			break;
		}
	}
	// LFO expects float pin wiring similar enough to compile with GraphInput→LFO→GraphOutput.
	GraphCompiler::CompileResult a = GraphCompiler::compile(desc_a, 48000.0f);
	GraphCompiler::CompileResult b = GraphCompiler::compile(desc_b, 48000.0f);
	REQUIRE(a.success());
	REQUIRE(b.success());

	PreparedGraphPackage *from = PreparedGraphPackage::create_from_graph(a.graph);
	PreparedGraphPackage *to = PreparedGraphPackage::create_from_graph(b.graph);
	REQUIRE(from != nullptr);
	REQUIRE(to != nullptr);

	const auto *from_fp = from->find_fingerprint(3);
	const auto *to_fp = to->find_fingerprint(3);
	REQUIRE(from_fp != nullptr);
	REQUIRE(to_fp != nullptr);
	CHECK(from_fp->type_hash != to_fp->type_hash);

	SymphonyOperator *src = from->graph->operators[from_fp->exec_index];
	SymphonyOperator *dst = to->graph->operators[to_fp->exec_index];
	uint8_t seed[16];
	size_t seed_size = src->export_state(nullptr, 0);
	REQUIRE(seed_size > 0);
	REQUIRE(seed_size <= sizeof(seed));
	for (size_t i = 0; i < seed_size; i++) {
		seed[i] = 0x3C;
	}
	src->import_state(seed, seed_size);

	uint8_t dst_before[16] = {};
	size_t dst_size = dst->export_state(dst_before, sizeof(dst_before));

	PreparedGraphPackage::migrate_compatible_state(from, to);

	uint8_t dst_after[16] = {};
	CHECK(dst->export_state(dst_after, sizeof(dst_after)) == dst_size);
	CHECK(memcmp(dst_before, dst_after, dst_size) == 0);

	PreparedGraphPackage::destroy(from);
	PreparedGraphPackage::destroy(to);
}

TEST_CASE("[Symphony][Playback] set_parameter and trigger target published control package") {
	Ref<AudioStreamSymphony> stream;
	stream.instantiate();
	stream->set_mix_rate(48000.0f);
	stream->set_graph_description(_make_io_graph());

	Ref<AudioStreamPlayback> base = stream->instantiate_playback();
	Ref<AudioStreamPlaybackSymphony> playback = base;
	REQUIRE(playback.is_valid());

	// Pending only until start() publishes control_package.
	CHECK(playback->trigger(StringName("gate"), 1.0f) == false);
	playback->set_parameter(StringName("freq"), 880.0f);

	playback->start();
	CHECK(playback->trigger(StringName("gate"), 1.0f) == true);
	playback->set_parameter(StringName("freq"), 660.0f);
	CHECK(playback->get_estimated_cost_units() > 0.0f);

	AudioFrame buf[64];
	CHECK(playback->mix(buf, 1.0f, 64) == 64);

	// Hot-swap: control stays on current until mix adopts pending.
	CompiledGraph *replacement = stream->compile_graph();
	REQUIRE(replacement != nullptr);
	playback->swap_graph(replacement);
	CHECK(playback->trigger(StringName("gate"), 0.5f) == true);
	playback->set_parameter(StringName("freq"), 220.0f);

	CHECK(playback->mix(buf, 1.0f, 64) == 64);
	CHECK(playback->trigger(StringName("gate"), 1.0f) == true);
	playback->set_parameter(StringName("freq"), 110.0f);
	CHECK(playback->trigger(StringName("missing"), 1.0f) == false);

	playback->stop();
	GraphPackageRetirement::drain();
}

TEST_CASE("[Symphony][Playback] RT-scope mix and execute report no violations") {
	SymphonyRealtimeScope::reset_violations();

	GraphCompiler::CompileResult result = GraphCompiler::compile(_make_io_graph(), 48000.0f);
	REQUIRE(result.success());
	PreparedGraphPackage *pkg = PreparedGraphPackage::create_from_graph(result.graph, result.arena_bytes, result.total_package_bytes);
	REQUIRE(pkg != nullptr);
	REQUIRE(pkg->graph != nullptr);

	AudioFrame exec_buf[SYMPHONY_MICRO_BLOCK_SIZE];
	pkg->graph_output->set_output(exec_buf, 0);
	pkg->graph->execute(SYMPHONY_MICRO_BLOCK_SIZE);
	CHECK(SymphonyRealtimeScope::violation_count() == 0);

	Ref<AudioStreamSymphony> stream;
	stream.instantiate();
	stream->set_mix_rate(48000.0f);
	stream->set_graph_description(_make_io_graph());
	Ref<AudioStreamPlayback> base = stream->instantiate_playback();
	Ref<AudioStreamPlaybackSymphony> playback = base;
	REQUIRE(playback.is_valid());
	playback->start();

	AudioFrame mix_buf[64];
	CHECK(playback->mix(mix_buf, 1.0f, 64) == 64);

	CompiledGraph *replacement = stream->compile_graph();
	REQUIRE(replacement != nullptr);
	playback->swap_graph(replacement);
	CHECK(playback->mix(mix_buf, 1.0f, 64) == 64);

	playback->stop();
	CHECK(playback->mix(mix_buf, 1.0f, 64) == 0);
	GraphPackageRetirement::drain();
	PreparedGraphPackage::destroy(pkg);

	CHECK(SymphonyRealtimeScope::violation_count() == 0);
}

TEST_CASE("[Symphony][Playback] RT-scope flags compile alloc free mutex ObjectDB container") {
	SymphonyRealtimeAssertSuppressor suppress;
	SymphonyRealtimeScope::reset_violations();
	GraphPackageRetirement::drain();

	CompiledGraph *compiled_in_scope = nullptr;
	{
		SymphonyRealtimeScope rt_scope;
		GraphCompiler::CompileResult ignored = GraphCompiler::compile(_make_io_graph(), 48000.0f);
		compiled_in_scope = ignored.graph;
	}
	CHECK(SymphonyRealtimeScope::violation_count(SymphonyRTViolation::Compile) >= 1);
	if (compiled_in_scope) {
		memdelete(compiled_in_scope);
	}

	SymphonyRealtimeScope::reset_violations();
	{
		SymphonyRealtimeScope rt_scope;
		ArenaAllocator arena;
		CHECK(arena.init(1024));
		arena.free();
	}
	CHECK(SymphonyRealtimeScope::violation_count(SymphonyRTViolation::Alloc) >= 1);
	CHECK(SymphonyRealtimeScope::violation_count(SymphonyRTViolation::Free) >= 1);

	SymphonyRealtimeScope::reset_violations();
	{
		SymphonyRealtimeScope rt_scope;
		GraphPackageRetirement::drain();
	}
	CHECK(SymphonyRealtimeScope::violation_count(SymphonyRTViolation::Free) >= 1);

	REQUIRE(SharedPCMCache::get_singleton() != nullptr);
	SymphonyRealtimeScope::reset_violations();
	{
		SymphonyRealtimeScope rt_scope;
		(void)SharedPCMCache::get_singleton()->get_entry_count();
	}
	CHECK(SymphonyRealtimeScope::violation_count(SymphonyRTViolation::Mutex) >= 1);

	Ref<AudioStreamSymphony> stream;
	stream.instantiate();
	stream->set_mix_rate(48000.0f);
	stream->set_graph_description(_make_io_graph());
	Ref<AudioStreamPlayback> base = stream->instantiate_playback();
	Ref<AudioStreamPlaybackSymphony> playback = base;
	REQUIRE(playback.is_valid());

	SymphonyRealtimeScope::reset_violations();
	{
		SymphonyRealtimeScope rt_scope;
		playback->process_manager_requests();
	}
	CHECK(SymphonyRealtimeScope::violation_count(SymphonyRTViolation::ObjectDB) >= 1);

	SymphonyVoiceManager *mgr = SymphonyVoiceManager::get_singleton();
	REQUIRE(mgr != nullptr);
	SymphonyRealtimeScope::reset_violations();
	{
		SymphonyRealtimeScope rt_scope;
		(void)mgr->get_debug_metrics();
	}
	CHECK(SymphonyRealtimeScope::violation_count(SymphonyRTViolation::ContainerMutation) >= 1);
	CHECK(mgr->get_rt_violation_count() >= 1);
}

static GraphDescription _make_spatial_wrapper_graph(bool p_loop) {
	// Mirrors SpatialGraphWrapper::create_spatial_stream without WavePlayer I/O.
	GraphDescription desc;
	desc.smooth_parameters = false;

	NodeDesc wp;
	wp.id = 0;
	wp.type_name = "WavePlayer";
	wp.params["loop_mode"] = p_loop ? 1.0f : 0.0f;
	wp.params["auto_play"] = 1.0f;
	desc.nodes.push_back(wp);

	NodeDesc onepole;
	onepole.id = 1;
	onepole.type_name = "OnePole";
	onepole.params["cutoff"] = 20000.0f;
	desc.nodes.push_back(onepole);

	NodeDesc svf;
	svf.id = 2;
	svf.type_name = "SVFilter";
	svf.params["cutoff"] = 20000.0f;
	svf.params["resonance"] = 0.0f;
	desc.nodes.push_back(svf);

	NodeDesc gain;
	gain.id = 3;
	gain.type_name = "Gain";
	gain.params["gain"] = 1.0f;
	desc.nodes.push_back(gain);

	NodeDesc out;
	out.id = 4;
	out.type_name = "GraphOutput";
	desc.nodes.push_back(out);

	NodeDesc in_air;
	in_air.id = 5;
	in_air.type_name = "GraphInput";
	in_air.params["parameter_name"] = "spatial_air_cutoff";
	in_air.params["default_value"] = 20000.0f;
	in_air.params["pin_type"] = 1.0f;
	desc.nodes.push_back(in_air);

	NodeDesc in_occ;
	in_occ.id = 6;
	in_occ.type_name = "GraphInput";
	in_occ.params["parameter_name"] = "spatial_occlusion_cutoff";
	in_occ.params["default_value"] = 20000.0f;
	in_occ.params["pin_type"] = 1.0f;
	desc.nodes.push_back(in_occ);

	NodeDesc in_gain;
	in_gain.id = 7;
	in_gain.type_name = "GraphInput";
	in_gain.params["parameter_name"] = "spatial_gain";
	in_gain.params["default_value"] = 1.0f;
	in_gain.params["pin_type"] = 1.0f;
	desc.nodes.push_back(in_gain);

	desc.connections.push_back({ 0, 0, 1, 0 });
	desc.connections.push_back({ 1, 0, 2, 0 });
	desc.connections.push_back({ 2, 0, 3, 0 });
	desc.connections.push_back({ 3, 0, 4, 0 });
	desc.connections.push_back({ 5, 0, 1, 1 });
	desc.connections.push_back({ 6, 0, 2, 1 });
	desc.connections.push_back({ 7, 0, 3, 1 });
	return desc;
}

TEST_CASE("[Symphony][Playback] Spatial wrapper rejects unsupported streams") {
	Ref<AudioStreamWAV> no_path;
	no_path.instantiate();
	no_path->set_format(AudioStreamWAV::FORMAT_16_BITS);
	CHECK_FALSE(SpatialGraphWrapper::is_wrappable_wav(no_path));
	CHECK_FALSE(SpatialGraphWrapper::needs_wrapping(no_path));
	CHECK(SpatialGraphWrapper::create_spatial_stream(no_path).is_null());

	Ref<AudioStreamWAV> eight_bit;
	eight_bit.instantiate();
	eight_bit->set_format(AudioStreamWAV::FORMAT_8_BITS);
	eight_bit->set_path("user://symphony_fake_8bit.wav");
	CHECK_FALSE(SpatialGraphWrapper::is_wrappable_wav(eight_bit));
	CHECK_FALSE(SpatialGraphWrapper::needs_wrapping(eight_bit));
	CHECK(SpatialGraphWrapper::create_spatial_stream(eight_bit).is_null());

	Ref<AudioStreamSymphony> already;
	already.instantiate();
	CHECK_FALSE(SpatialGraphWrapper::needs_wrapping(already));
}

TEST_CASE("[Symphony][Playback] Spatial wrapper param names resolve and set_parameter is bound") {
	CHECK(ClassDB::has_method("AudioStreamPlaybackSymphony", "set_parameter"));

	const StringName air = SpatialGraphWrapper::param_air_cutoff();
	const StringName occ = SpatialGraphWrapper::param_occlusion_cutoff();
	const StringName gain = SpatialGraphWrapper::param_gain();
	CHECK(air != StringName());
	CHECK(occ != StringName());
	CHECK(gain != StringName());

	Ref<AudioStreamSymphony> stream;
	stream.instantiate();
	stream->set_stop_on_source_finished(true);
	stream->set_graph_description(_make_spatial_wrapper_graph(false));

	GraphCompiler::CompileResult result = GraphCompiler::compile(stream->get_graph_description(), 44100.0f);
	REQUIRE(result.success());
	PreparedGraphPackage *pkg = PreparedGraphPackage::create_from_graph(result.graph, result.arena_bytes, result.total_package_bytes);
	REQUIRE(pkg != nullptr);
	CHECK(pkg->find_param(air) != nullptr);
	CHECK(pkg->find_param(occ) != nullptr);
	CHECK(pkg->find_param(gain) != nullptr);
	CHECK(pkg->source_finished_triggers.size() >= 1);
	PreparedGraphPackage::destroy(pkg);

	Ref<AudioStreamPlayback> base = stream->instantiate_playback();
	Ref<AudioStreamPlaybackSymphony> playback = base;
	REQUIRE(playback.is_valid());
	playback->start();
	playback->set_parameter(gain, 0.25f);
	CHECK((float)playback->get_parameter(gain) == doctest::Approx(0.25f));
	playback->stop();
}

TEST_CASE("[Symphony][Playback] stop_on_source_finished clears is_playing when source finishes") {
	Ref<AudioStreamSymphony> stream;
	stream.instantiate();
	stream->set_stop_on_source_finished(true);
	stream->set_graph_description(_make_spatial_wrapper_graph(false));

	Ref<AudioStreamPlayback> base = stream->instantiate_playback();
	Ref<AudioStreamPlaybackSymphony> playback = base;
	REQUIRE(playback.is_valid());
	playback->start();
	CHECK(playback->is_playing());

	AudioFrame buf[64];
	REQUIRE(playback->fire_source_finished_and_mix(buf, 64));
	CHECK_FALSE(playback->is_playing());
	CHECK(playback->is_registered_with_voice_manager());

	SymphonyVoiceManager *mgr = SymphonyVoiceManager::get_singleton();
	REQUIRE(mgr != nullptr);
	if (AudioServer::get_singleton()) {
		CHECK(mgr->is_mix_callback_registered());
	}
	mgr->enforce_voice_limits();
	CHECK(playback->is_registered_with_voice_manager());
	mgr->enforce_voice_limits();
	mgr->process_deferred_lod();
	CHECK_FALSE(playback->is_registered_with_voice_manager());
}

static GraphDescription _make_sine_graph() {
	GraphDescription desc;
	desc.smooth_parameters = false;
	desc.anti_alias_staircase = false;
	NodeDesc osc;
	osc.id = 1;
	osc.type_name = "Oscillator";
	osc.params.insert("frequency", 440.0f);
	osc.params.insert("waveform", 0.0f);
	desc.nodes.push_back(osc);
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

static float _measure_hz(const Vector<AudioFrame> &p_frames, int p_start, float p_rate) {
	int crossings = 0;
	const int last = p_frames.size() - 1;
	for (int i = p_start + 1; i <= last; i++) {
		const float previous = p_frames[i - 1].left;
		const float sample = p_frames[i].left;
		if ((previous <= 0.0f && sample > 0.0f) || (previous >= 0.0f && sample < 0.0f)) {
			crossings++;
		}
	}
	const int measured = last - p_start;
	if (measured <= 0 || p_rate <= 0.0f) {
		return 0.0f;
	}
	return (float)crossings * 0.5f * p_rate / (float)measured;
}

TEST_CASE("[Symphony][Playback][Audio] Resampling keeps 440 Hz across graph rates and pitch") {
	AudioServer *server = AudioServer::get_singleton();
	REQUIRE(server != nullptr);
	const float output_rate = server->get_mix_rate();
	REQUIRE(output_rate > 0.0f);
	const float graph_rates[3] = { 44100.0f, 48000.0f, 96000.0f };
	const float pitches[2] = { 1.0f, 2.0f };
	const float expected[2] = { 440.0f, 880.0f };
	const float tolerance[2] = { 1.0f, 2.0f };

	for (float graph_rate : graph_rates) {
		for (int pitch_index = 0; pitch_index < 2; pitch_index++) {
			Ref<AudioStreamSymphony> stream;
			stream.instantiate();
			stream->set_mix_rate(graph_rate);
			stream->set_graph_description(_make_sine_graph());
			Ref<AudioStreamPlayback> base = stream->instantiate_playback();
			Ref<AudioStreamPlaybackSymphony> playback = base;
			REQUIRE(playback.is_valid());
			playback->start();

			const int total = (int)output_rate * 2;
			Vector<AudioFrame> captured;
			captured.resize(total);
			int filled = 0;
			AudioFrame chunk[512];
			while (filled < total) {
				const int got = playback->mix(chunk, pitches[pitch_index], 512);
				if (got <= 0) {
					break;
				}
				for (int i = 0; i < got && filled < total; i++) {
					captured.write[filled++] = chunk[i];
				}
			}
			CHECK(filled == total);
			const int skip = MIN(filled / 4, (int)output_rate / 4);
			const float hz = _measure_hz(captured, skip, output_rate);
			CHECK(std::abs(hz - expected[pitch_index]) <= tolerance[pitch_index]);

			const double position_before = playback->get_playback_position();
			CHECK(position_before > 0.5);
			playback->mix(chunk, pitches[pitch_index], 512);
			CHECK(playback->get_playback_position() > position_before);
			playback->seek(1.25);
			CHECK(playback->has_unsupported_seek());
			playback->stop();
		}
	}
}

TEST_CASE("[Symphony][Playback] Duration limit ends a one-shot") {
	Ref<AudioStreamSymphony> stream;
	stream.instantiate();
	stream->set_mix_rate(44100.0f);
	stream->set_duration_limit_seconds(0.05);
	stream->set_graph_description(_make_sine_graph());
	Ref<AudioStreamPlayback> base = stream->instantiate_playback();
	Ref<AudioStreamPlaybackSymphony> playback = base;
	REQUIRE(playback.is_valid());
	playback->start();
	AudioFrame chunk[256];
	int guard = 0;
	while (playback->is_playing() && guard < 100) {
		playback->mix(chunk, 1.0f, 256);
		guard++;
	}
	CHECK_FALSE(playback->is_playing());
	CHECK(playback->get_playback_position() >= 0.05);
	CHECK(playback->get_playback_position() < 0.2);
	playback->stop();
}

} // namespace TestSymphonyPlayback
