/**************************************************************************/
/*  symphony_test_helpers.h                                               */
/**************************************************************************/

#pragma once

#include "core/math/math_funcs.h"
#include "core/templates/vector.h"
#include "modules/symphony/core/symphony_graph_compiler.h"
#include "modules/symphony/core/symphony_graph_description.h"
#include "modules/symphony/core/symphony_operator_registry.h"
#include "modules/symphony/core/symphony_pin_types.h"
#include "modules/symphony/core/symphony_prepared_graph_package.h"
#include "modules/symphony/nodes/io/symphony_graph_output.h"
#include "scene/resources/audio/audio_stream.h"
#include "tests/test_macros.h"

#include <cmath>
#include <cstddef>

namespace SymphonyTestHelpers {

// Bind helpers that size input/output pointer arrays from the operator descriptor.
// Prevents out-of-bounds bind_pins access when fixtures omit optional pins.
inline void expect_descriptor_pins(const StringName &p_type_name, int p_expected_inputs, int p_expected_outputs) {
	const OperatorDescriptor *desc = OperatorRegistry::get_singleton()->find(p_type_name);
	REQUIRE(desc != nullptr);
	CHECK(desc->inputs.size() == p_expected_inputs);
	CHECK(desc->outputs.size() == p_expected_outputs);
}

inline float rms(const float *p_data, int p_count) {
	double sum = 0.0;
	for (int i = 0; i < p_count; i++) {
		sum += (double)p_data[i] * (double)p_data[i];
	}
	return (float)Math::sqrt(sum / (double)MAX(1, p_count));
}

inline float db_ratio(float p_num, float p_den) {
	const float n = MAX(p_num, 1e-12f);
	const float d = MAX(p_den, 1e-12f);
	return 20.0f * Math::log(n / d) / Math::log(10.0f);
}

inline GraphDescription make_sine_graph(float p_hz = 440.0f) {
	GraphDescription desc;
	desc.smooth_parameters = false;
	desc.anti_alias_staircase = false;
	NodeDesc osc;
	osc.id = 1;
	osc.type_name = "Oscillator";
	osc.params.insert("frequency", p_hz);
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

// Zero-crossing frequency estimate over frames[p_start..end].
inline float measure_hz(const Vector<AudioFrame> &p_frames, int p_start, float p_rate) {
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

inline void execute_mix_frames(PreparedGraphPackage *p_pkg, AudioFrame *p_buf, int p_frames) {
	for (int off = 0; off < p_frames; off += SYMPHONY_MICRO_BLOCK_SIZE) {
		p_pkg->graph_output->set_output(p_buf, off);
		p_pkg->graph->execute(SYMPHONY_MICRO_BLOCK_SIZE);
	}
}

// Compile + execute a graph at p_sr for p_frames samples; returns mono left channel.
inline Vector<float> render_graph(const GraphDescription &p_desc, int p_frames, float p_sr) {
	Vector<float> out;
	out.resize(p_frames);
	for (int i = 0; i < p_frames; i++) {
		out.write[i] = 0.0f;
	}
	if (p_frames <= 0 || p_sr <= 0.0f) {
		return out;
	}

	GraphCompiler::CompileResult result = GraphCompiler::compile(p_desc, p_sr);
	REQUIRE(result.success());
	REQUIRE(result.graph != nullptr);

	Vector<AudioFrame> frames;
	frames.resize(p_frames);
	for (int i = 0; i < p_frames; i++) {
		frames.write[i] = AudioFrame(0.0f, 0.0f);
	}

	SymphonyGraphOutput *gout = nullptr;
	for (int32_t i = 0; i < result.graph->operator_count; i++) {
		gout = dynamic_cast<SymphonyGraphOutput *>(result.graph->operators[i]);
		if (gout) {
			break;
		}
	}
	REQUIRE(gout != nullptr);

	for (int off = 0; off < p_frames; off += SYMPHONY_MICRO_BLOCK_SIZE) {
		const int block = MIN(SYMPHONY_MICRO_BLOCK_SIZE, p_frames - off);
		gout->set_output(frames.ptrw(), off);
		result.graph->execute(block);
	}

	for (int i = 0; i < p_frames; i++) {
		out.write[i] = frames[i].left;
	}
	memdelete(result.graph);
	return out;
}

// Goertzel detector: tone level in dBFS at p_hz.
inline float tone_level_db(const Vector<float> &p_buf, float p_sr, float p_hz) {
	const int n = p_buf.size();
	if (n <= 0 || p_sr <= 0.0f || p_hz <= 0.0f) {
		return -120.0f;
	}
	const double w = Math::TAU * (double)p_hz / (double)p_sr;
	const double coeff = 2.0 * Math::cos(w);
	double s0 = 0.0;
	double s1 = 0.0;
	double s2 = 0.0;
	for (int i = 0; i < n; i++) {
		s0 = (double)p_buf[i] + coeff * s1 - s2;
		s2 = s1;
		s1 = s0;
	}
	const double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
	const double mag = Math::sqrt(MAX(0.0, power)) * 2.0 / (double)n;
	return db_ratio((float)mag, 1.0f);
}

// Energy off the harmonic grid {f0, 2f0, ... harmonics*f0} relative to on-grid energy, in dB.
// Positive = more alias/off-grid energy than harmonic energy.
inline float alias_ratio_db(const Vector<float> &p_buf, float p_sr, float p_f0, int p_harmonics) {
	const int n = p_buf.size();
	if (n <= 0 || p_sr <= 0.0f || p_f0 <= 0.0f || p_harmonics < 1) {
		return 0.0f;
	}

	// Naive DFT magnitude at bin k (k cycles over the buffer).
	auto bin_power = [&](double cycles) -> double {
		double re = 0.0;
		double im = 0.0;
		const double w = Math::TAU * cycles / (double)n;
		for (int i = 0; i < n; i++) {
			const double x = (double)p_buf[i];
			re += x * Math::cos(w * (double)i);
			im -= x * Math::sin(w * (double)i);
		}
		return re * re + im * im;
	};

	double on_grid = 0.0;
	double off_grid = 0.0;

	// Bin resolution: 1 cycle over the buffer = sr/n Hz.
	// Accept ±2 bins around each harmonic so mild spectral leakage stays on-grid.
	const double bin_hz = (double)p_sr / (double)n;
	const double half_window = 2.0 * bin_hz;
	const int max_bin = n / 2;
	for (int k = 1; k < max_bin; k++) {
		const double hz = (double)k * bin_hz;
		if (hz >= 0.5 * (double)p_sr) {
			break;
		}
		const double power = bin_power((double)k);

		bool on = false;
		for (int h = 1; h <= p_harmonics; h++) {
			const double target = (double)h * (double)p_f0;
			if (Math::abs(hz - target) <= half_window) {
				on = true;
				break;
			}
		}
		if (on) {
			on_grid += power;
		} else {
			off_grid += power;
		}
	}

	return db_ratio((float)Math::sqrt(MAX(off_grid, 1e-30)), (float)Math::sqrt(MAX(on_grid, 1e-30)));
}

} // namespace SymphonyTestHelpers
