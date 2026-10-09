/**************************************************************************/
/*  test_symphony_operator_reset.cpp                                      */
/*  Suite: [Symphony][Reset] — P10 resettable-state conformance.          */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_operator_reset)

#include "modules/symphony/core/symphony_operator_registry.h"
#include "modules/symphony/core/symphony_graph_compiler.h"
#include "modules/symphony/core/symphony_graph_description.h"
#include "modules/symphony/core/symphony_prepared_graph_package.h"
#include "modules/symphony/nodes/io/symphony_graph_output.h"
#include "modules/symphony/nodes/synthesis/symphony_modal_bank.h"
#include "tests/modules/symphony_test_helpers.h"

#include "core/math/math_funcs.h"

#include <cstring>

namespace TestSymphonyOperatorReset {

static bool _type_in_allowlist(const StringName &p_type) {
	static const char *k_allow[] = {
		"PhaseVocoder",
		"SpectralGate",
		"GrainCloud",
		"DelayLine",
		"PitchShifter",
		"FDNReverb",
		"EarlyReflections",
		"Clock",
		"TriggerDelay",
		"StochasticTrigger",
		"Compressor",
		"ResonatorAnalyzer",
		"GraphInput",
		"GraphInputAudio",
		"GraphOutput",
		"TriggerInput",
		"SubGraph",
		// Test-only operators registered by other suites.
		"NullFactoryOp",
		"UnderReportOp",
	};
	for (const char *name : k_allow) {
		if (p_type == StringName(name)) {
			return true;
		}
	}
	return false;
}

static GraphDescription _minimal_graph_for(const StringName &p_type, const OperatorDescriptor &p_desc) {
	GraphDescription desc;
	desc.smooth_parameters = false;
	desc.anti_alias_staircase = false;
	int32_t next_id = 1;
	Vector<int32_t> input_node_ids;

	for (int i = 0; i < p_desc.inputs.size(); i++) {
		const PinDescriptor &pin = p_desc.inputs[i];
		NodeDesc src;
		src.id = next_id++;
		if (pin.type == SymphonyPinType::TRIGGER) {
			src.type_name = "TriggerInput";
			src.params["trigger_name"] = String("t") + String::num_int64(i);
			src.params["auto_trigger_on_play"] = 1.0f;
		} else if (pin.type == SymphonyPinType::AUDIO) {
			src.type_name = "Noise";
			src.params["mode"] = 0.0f;
			src.params["deterministic_seed"] = 1.0f;
			src.params["seed"] = 1.0f;
		} else {
			src.type_name = "GraphInput";
			src.params["parameter_name"] = String(pin.name);
			src.params["default_value"] = (pin.name == StringName("pitch_ratio")) ? 1.0f : 440.0f;
		}
		desc.nodes.push_back(src);
		input_node_ids.push_back(src.id);
	}

	NodeDesc op;
	op.id = next_id++;
	op.type_name = p_type;
	for (int i = 0; i < p_desc.params.size(); i++) {
		const ParamDescriptor &param = p_desc.params[i];
		if (param.value_type == ParamValueType::FLOAT_ARRAY) {
			continue;
		}
		op.params[param.name] = param.default_value;
	}
	if (p_type == StringName("ModalBank")) {
		PackedFloat32Array freqs;
		PackedFloat32Array decays;
		PackedFloat32Array gains;
		freqs.push_back(440.0f);
		decays.push_back(0.5f);
		gains.push_back(1.0f);
		op.params["frequencies"] = freqs;
		op.params["decay_times"] = decays;
		op.params["gains"] = gains;
		op.params["max_modes"] = 4.0f;
		op.params["num_modes"] = 1.0f;
	}
	if (p_type == StringName("WavePlayer")) {
		op.params["resource_path"] = String(); // silence path — no WAV load in unit test
	}
	desc.nodes.push_back(op);

	NodeDesc out;
	out.id = next_id++;
	out.type_name = "GraphOutput";
	desc.nodes.push_back(out);

	for (int i = 0; i < p_desc.inputs.size(); i++) {
		ConnectionDesc c;
		c.from_node = input_node_ids[i];
		c.from_pin = 0;
		c.to_node = op.id;
		c.to_pin = i;
		desc.connections.push_back(c);
	}

	if (p_desc.outputs.size() > 0 && p_desc.outputs[0].type == SymphonyPinType::AUDIO) {
		ConnectionDesc c;
		c.from_node = op.id;
		c.from_pin = 0;
		c.to_node = out.id;
		c.to_pin = 0;
		desc.connections.push_back(c);
	} else {
		NodeDesc noise;
		noise.id = next_id++;
		noise.type_name = "Noise";
		noise.params["deterministic_seed"] = 1.0f;
		noise.params["seed"] = 2.0f;
		desc.nodes.push_back(noise);
		ConnectionDesc c;
		c.from_node = noise.id;
		c.from_pin = 0;
		c.to_node = out.id;
		c.to_pin = 0;
		desc.connections.push_back(c);
	}
	return desc;
}

static Vector<float> _render_compiled(CompiledGraph *p_graph, int p_frames) {
	Vector<float> out;
	out.resize(p_frames);
	Vector<AudioFrame> frames;
	frames.resize(p_frames);
	SymphonyGraphOutput *gout = nullptr;
	for (int32_t i = 0; i < p_graph->operator_count; i++) {
		gout = dynamic_cast<SymphonyGraphOutput *>(p_graph->operators[i]);
		if (gout) {
			break;
		}
	}
	REQUIRE(gout != nullptr);
	for (int off = 0; off < p_frames; off += SYMPHONY_MICRO_BLOCK_SIZE) {
		const int block = MIN(SYMPHONY_MICRO_BLOCK_SIZE, p_frames - off);
		gout->set_output(frames.ptrw(), off);
		p_graph->execute(block);
	}
	for (int i = 0; i < p_frames; i++) {
		out.write[i] = frames[i].left;
	}
	return out;
}

TEST_CASE("[Symphony][Reset] every registered type is resettable or allow-listed") {
	OperatorRegistry *reg = OperatorRegistry::get_singleton();
	REQUIRE(reg != nullptr);
	Vector<StringName> types;
	reg->get_registered_types(types);
	CHECK(types.size() > 20);
	for (int i = 0; i < types.size(); i++) {
		const OperatorDescriptor *desc = reg->find(types[i]);
		REQUIRE(desc != nullptr);
		if (!desc->resettable) {
			CHECK_MESSAGE(_type_in_allowlist(types[i]), String(types[i]));
		}
	}
}

TEST_CASE("[Symphony][Reset] reset_for_note matches fresh construct for resettable ops") {
	OperatorRegistry *reg = OperatorRegistry::get_singleton();
	REQUIRE(reg != nullptr);
	Vector<StringName> types;
	reg->get_registered_types(types);

	const uint64_t seed_s = 0xC0FFEEu;
	const int frames = SYMPHONY_MICRO_BLOCK_SIZE * 4;

	for (int i = 0; i < types.size(); i++) {
		const OperatorDescriptor *desc = reg->find(types[i]);
		REQUIRE(desc != nullptr);
		if (!desc->resettable) {
			continue;
		}

		GraphDescription gdesc = _minimal_graph_for(types[i], *desc);
		GraphCompiler::CompileResult fresh = GraphCompiler::compile(gdesc, 48000.0f, String(), seed_s);
		if (!fresh.success()) {
			FAIL(vformat("resettable '%s' failed to compile: %s", String(types[i]),
					fresh.errors.size() ? String(fresh.errors[0]) : String("?")));
			continue;
		}
		GraphCompiler::CompileResult reused = GraphCompiler::compile(gdesc, 48000.0f, String(), seed_s);
		REQUIRE(reused.success());

		(void)_render_compiled(reused.graph, frames);
		// Reset every operator (DUT + drivers). Warm-up advances Noise/LFOs; note reset
		// must restore the whole voice to the same seed as a fresh construct.
		for (int32_t oi = 0; oi < reused.graph->operator_count; oi++) {
			reused.graph->operators[oi]->reset_for_note(seed_s);
		}

		Vector<float> a = _render_compiled(fresh.graph, frames);
		Vector<float> b = _render_compiled(reused.graph, frames);
		REQUIRE(a.size() == b.size());
		float max_abs_diff = 0.0f;
		for (int s = 0; s < a.size(); s++) {
			max_abs_diff = MAX(max_abs_diff, Math::abs(a[s] - b[s]));
		}
		CHECK_MESSAGE(max_abs_diff == 0.0f, String(types[i]));

		memdelete(fresh.graph);
		memdelete(reused.graph);
	}
}

TEST_CASE("[Symphony][Reset] ModalBank pitch_ratio doubles mode frequency") {
	auto make_desc = [](float p_ratio, float p_base_hz = 220.0f) {
		GraphDescription desc;
		desc.smooth_parameters = false;
		NodeDesc impulse;
		impulse.id = 1;
		impulse.type_name = "Constant";
		impulse.params["value"] = 0.0f; // quiet after first-sample strike via Noise would smear; use Trigger+...
		desc.nodes.push_back(impulse);
		// Use Noise only for energy; pitch test uses Goertzel on ModalBank of a sine-like ring.
		NodeDesc noise;
		noise.id = 1;
		noise.type_name = "Noise";
		noise.params["deterministic_seed"] = 1.0f;
		noise.params["seed"] = 1.0f;
		desc.nodes.clear();
		desc.nodes.push_back(noise);
		NodeDesc ratio;
		ratio.id = 2;
		ratio.type_name = "GraphInput";
		ratio.params["parameter_name"] = "pitch_ratio";
		ratio.params["default_value"] = p_ratio;
		desc.nodes.push_back(ratio);
		NodeDesc bank;
		bank.id = 3;
		bank.type_name = "ModalBank";
		bank.params["max_modes"] = 4.0f;
		bank.params["num_modes"] = 1.0f;
		bank.params["root_hz"] = p_base_hz;
		PackedFloat32Array freqs;
		PackedFloat32Array decays;
		PackedFloat32Array gains;
		freqs.push_back(p_base_hz);
		decays.push_back(2.0f);
		gains.push_back(1.0f);
		bank.params["frequencies"] = freqs;
		bank.params["decay_times"] = decays;
		bank.params["gains"] = gains;
		desc.nodes.push_back(bank);
		NodeDesc out;
		out.id = 4;
		out.type_name = "GraphOutput";
		desc.nodes.push_back(out);
		ConnectionDesc c0;
		c0.from_node = 1;
		c0.to_node = 3;
		c0.to_pin = 0;
		ConnectionDesc c1;
		c1.from_node = 2;
		c1.to_node = 3;
		c1.to_pin = 1;
		ConnectionDesc c2;
		c2.from_node = 3;
		c2.to_node = 4;
		desc.connections.push_back(c0);
		desc.connections.push_back(c1);
		desc.connections.push_back(c2);
		return desc;
	};

	// Direct unit check: ratio 2 recomputes to ~440 Hz tone (Goertzel).
	Vector<float> at_1 = SymphonyTestHelpers::render_graph(make_desc(1.0f), 4800, 48000.0f);
	Vector<float> at_2 = SymphonyTestHelpers::render_graph(make_desc(2.0f), 4800, 48000.0f);
	const float level_220 = SymphonyTestHelpers::tone_level_db(at_1, 48000.0f, 220.0f);
	const float level_440_from_1 = SymphonyTestHelpers::tone_level_db(at_1, 48000.0f, 440.0f);
	const float level_440_from_2 = SymphonyTestHelpers::tone_level_db(at_2, 48000.0f, 440.0f);
	const float level_220_from_2 = SymphonyTestHelpers::tone_level_db(at_2, 48000.0f, 220.0f);
	CHECK(level_220 > level_440_from_1); // ratio 1 peaks near 220
	CHECK(level_440_from_2 > level_220_from_2); // ratio 2 peaks near 440

	// Mode above 0.45·sr is muted (3000 Hz * ratio 8 = 24 kHz at 48 kHz; ratio clamps to 8).
	Vector<float> muted = SymphonyTestHelpers::render_graph(make_desc(8.0f, 3000.0f), 512, 48000.0f);
	float peak = 0.0f;
	for (int i = 0; i < muted.size(); i++) {
		peak = MAX(peak, Math::abs(muted[i]));
	}
	CHECK(peak < 1e-5f);
}

TEST_CASE("[Symphony][Reset] migrate_compatible_state handles >256-byte ModalBank state") {
	GraphDescription desc;
	desc.smooth_parameters = false;
	NodeDesc noise;
	noise.id = 1;
	noise.type_name = "Constant";
	noise.params["value"] = 0.0f;
	desc.nodes.push_back(noise);
	NodeDesc bank;
	bank.id = 2;
	bank.type_name = "ModalBank";
	bank.params["max_modes"] = 40.0f;
	bank.params["num_modes"] = 1.0f;
	PackedFloat32Array freqs;
	PackedFloat32Array decays;
	PackedFloat32Array gains;
	freqs.push_back(100.0f);
	decays.push_back(0.2f);
	gains.push_back(1.0f);
	bank.params["frequencies"] = freqs;
	bank.params["decay_times"] = decays;
	bank.params["gains"] = gains;
	desc.nodes.push_back(bank);
	NodeDesc out;
	out.id = 3;
	out.type_name = "GraphOutput";
	desc.nodes.push_back(out);
	ConnectionDesc c0;
	c0.from_node = 1;
	c0.to_node = 2;
	ConnectionDesc c1;
	c1.from_node = 2;
	c1.to_node = 3;
	desc.connections.push_back(c0);
	desc.connections.push_back(c1);

	GraphCompiler::CompileResult a = GraphCompiler::compile(desc, 48000.0f);
	GraphCompiler::CompileResult b = GraphCompiler::compile(desc, 48000.0f);
	REQUIRE(a.success());
	REQUIRE(b.success());

	int modal_idx = -1;
	for (int32_t i = 0; i < a.graph->operator_count; i++) {
		if (dynamic_cast<SymphonyModalBank *>(a.graph->operators[i])) {
			modal_idx = i;
			break;
		}
	}
	REQUIRE(modal_idx >= 0);
	const size_t state_bytes = a.graph->operators[modal_idx]->export_state(nullptr, 0);
	CHECK(state_bytes > 256);

	PreparedGraphPackage *pkg_a = PreparedGraphPackage::create_from_graph(a.graph);
	PreparedGraphPackage *pkg_b = PreparedGraphPackage::create_from_graph(b.graph);
	REQUIRE(pkg_a->migrate_scratch_bytes >= state_bytes);

	AudioFrame frames[SYMPHONY_MICRO_BLOCK_SIZE];
	SymphonyGraphOutput *gout = nullptr;
	for (int32_t i = 0; i < a.graph->operator_count; i++) {
		gout = dynamic_cast<SymphonyGraphOutput *>(a.graph->operators[i]);
		if (gout) {
			break;
		}
	}
	REQUIRE(gout != nullptr);
	gout->set_output(frames, 0);
	a.graph->execute(SYMPHONY_MICRO_BLOCK_SIZE);

	PreparedGraphPackage::migrate_compatible_state(pkg_a, pkg_b);
	uint8_t *buf_a = (uint8_t *)memalloc(state_bytes);
	uint8_t *buf_b = (uint8_t *)memalloc(state_bytes);
	CHECK(a.graph->operators[modal_idx]->export_state(buf_a, state_bytes) == state_bytes);
	CHECK(b.graph->operators[modal_idx]->export_state(buf_b, state_bytes) == state_bytes);
	CHECK(memcmp(buf_a, buf_b, state_bytes) == 0);
	memfree(buf_a);
	memfree(buf_b);
	pkg_a->graph = nullptr;
	pkg_b->graph = nullptr;
	PreparedGraphPackage::destroy(pkg_a);
	PreparedGraphPackage::destroy(pkg_b);
	memdelete(a.graph);
	memdelete(b.graph);
}

} // namespace TestSymphonyOperatorReset
