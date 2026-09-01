#include "spatial_graph_wrapper.h"
#include "core/math/math_funcs.h"

const StringName SpatialGraphWrapper::PARAM_AIR_CUTOFF = "spatial_air_cutoff";
const StringName SpatialGraphWrapper::PARAM_OCCLUSION_CUTOFF = "spatial_occlusion_cutoff";
const StringName SpatialGraphWrapper::PARAM_GAIN = "spatial_gain";

bool SpatialGraphWrapper::needs_wrapping(const Ref<AudioStream> &p_stream) {
	if (p_stream.is_null()) {
		return false;
	}
	// Already a Symphony graph — no wrapping needed.
	return !Object::cast_to<AudioStreamSymphony>(p_stream.ptr());
}

Ref<AudioStreamSymphony> SpatialGraphWrapper::create_spatial_stream(const Ref<AudioStream> &p_source, bool p_loop) {
	if (p_source.is_null()) {
		return Ref<AudioStreamSymphony>();
	}

	String resource_path = p_source->get_path();
	if (resource_path.is_empty()) {
		return Ref<AudioStreamSymphony>(); // Can't load without a path.
	}

	Ref<AudioStreamSymphony> stream;
	stream.instantiate();
	stream->set_mix_rate(44100.0f); // Will be overridden by AudioServer mix rate at playback.

	// Build the graph description:
	// Node IDs:
	//   0 = WavePlayer (source)
	//   1 = OnePole (air absorption LPF)
	//   2 = SVFilter (occlusion LPF — uses lp_out)
	//   3 = Gain (volume from spatial system)
	//   4 = GraphOutput
	//   5 = GraphInput "spatial_air_cutoff"
	//   6 = GraphInput "spatial_occlusion_cutoff"
	//   7 = GraphInput "spatial_gain"

	GraphDescription desc;
	desc.smooth_parameters = false; // We handle smoothing externally via SpatialAcousticsEngine IIR.

	// --- Node 0: WavePlayer ---
	{
		NodeDesc node;
		node.id = 0;
		node.type_name = "WavePlayer";
		node.params["resource_path"] = resource_path;
		node.params["bake_audio"] = 1.0f; // Bake into arena for cache efficiency.
		node.params["loop_mode"] = p_loop ? 1.0f : 0.0f;
		node.params["auto_play"] = 1.0f;
		desc.nodes.push_back(node);
	}

	// --- Node 1: OnePole (air absorption) ---
	{
		NodeDesc node;
		node.id = 1;
		node.type_name = "OnePole";
		node.params["cutoff"] = 20000.0f; // Default: wide open (no filtering)
		desc.nodes.push_back(node);
	}

	// --- Node 2: SVFilter (occlusion) ---
	{
		NodeDesc node;
		node.id = 2;
		node.type_name = "SVFilter";
		node.params["cutoff"] = 20000.0f; // Default: wide open
		node.params["resonance"] = 0.0f;  // No resonance for natural occlusion
		desc.nodes.push_back(node);
	}

	// --- Node 3: Gain ---
	{
		NodeDesc node;
		node.id = 3;
		node.type_name = "Gain";
		node.params["gain"] = 1.0f; // Default: unity
		desc.nodes.push_back(node);
	}

	// --- Node 4: GraphOutput ---
	{
		NodeDesc node;
		node.id = 4;
		node.type_name = "GraphOutput";
		desc.nodes.push_back(node);
	}

	// --- Node 5: GraphInput "spatial_air_cutoff" ---
	{
		NodeDesc node;
		node.id = 5;
		node.type_name = "GraphInput";
		node.params["parameter_name"] = String(PARAM_AIR_CUTOFF);
		node.params["default_value"] = 20000.0f;
		node.params["pin_type"] = 1.0f; // FLOAT
		desc.nodes.push_back(node);
	}

	// --- Node 6: GraphInput "spatial_occlusion_cutoff" ---
	{
		NodeDesc node;
		node.id = 6;
		node.type_name = "GraphInput";
		node.params["parameter_name"] = String(PARAM_OCCLUSION_CUTOFF);
		node.params["default_value"] = 20000.0f;
		node.params["pin_type"] = 1.0f; // FLOAT
		desc.nodes.push_back(node);
	}

	// --- Node 7: GraphInput "spatial_gain" ---
	{
		NodeDesc node;
		node.id = 7;
		node.type_name = "GraphInput";
		node.params["parameter_name"] = String(PARAM_GAIN);
		node.params["default_value"] = 1.0f;
		node.params["pin_type"] = 1.0f; // FLOAT
		desc.nodes.push_back(node);
	}

	// --- Connections ---
	// Audio chain: WavePlayer.output(0) → OnePole.input(0) → SVFilter.audio_in(0) → Gain.input(0) → GraphOutput.input(0)
	desc.connections.push_back({ 0, 0, 1, 0 }); // WavePlayer output → OnePole input
	desc.connections.push_back({ 1, 0, 2, 0 }); // OnePole output → SVFilter audio_in
	desc.connections.push_back({ 2, 0, 3, 0 }); // SVFilter lp_out → Gain input
	desc.connections.push_back({ 3, 0, 4, 0 }); // Gain output → GraphOutput input

	// Parameter connections:
	desc.connections.push_back({ 5, 0, 1, 1 }); // air_cutoff GraphInput → OnePole cutoff (pin 1)
	desc.connections.push_back({ 6, 0, 2, 1 }); // occlusion_cutoff GraphInput → SVFilter cutoff (pin 1)
	desc.connections.push_back({ 7, 0, 3, 1 }); // gain GraphInput → Gain gain (pin 1)

	stream->set_graph_description(desc);
	return stream;
}

float SpatialGraphWrapper::transmission_to_cutoff(float p_transmission_mid, float p_transmission_high) {
	// Map transmission to cutoff frequency using minimum-frequency stacking.
	// Each band independently maps to a cutoff; we take the minimum.
	// Transmission 1.0 → 20000 Hz (open), 0.0 → 200 Hz (fully occluded).
	// Log-scale interpolation for perceptually even sweeps.
	const float open_hz = 20000.0f;
	const float closed_hz = 200.0f;

	// Each band: cutoff = closed_hz * (open_hz/closed_hz)^transmission
	// This gives log-domain interpolation: at t=1 → open_hz, at t=0 → closed_hz.
	float cutoff_mid = closed_hz * Math::pow(open_hz / closed_hz, p_transmission_mid);
	float cutoff_high = closed_hz * Math::pow(open_hz / closed_hz, p_transmission_high);

	// Minimum frequency stacking — never multiply cutoffs.
	return MIN(cutoff_mid, cutoff_high);
}

float SpatialGraphWrapper::distance_to_air_cutoff(float p_distance, float p_scale) {
	// Distance-absolute air absorption (Phase 4.1), fit to ISO 9613-1 at 20 °C,
	// 50 % RH. f_c is the frequency at which cumulative HF absorption reaches
	// ~3 dB over the travelled distance:
	//
	//   f_c = 4000 · (3 / (0.033 · d · scale))^(1/1.7)   clamped to [200, 20000]
	//
	// Sanity points (scale=1): 10 m → ~14.6 kHz, 30 m → ~7.6 kHz,
	// 100 m → ~3.8 kHz, 300 m → ~2.0 kHz. Decoupled from max_distance so it no
	// longer goes inert for the default 2000 m falloff.
	if (p_scale <= 0.0f || p_distance <= 0.0f) {
		return 20000.0f; // Disabled or zero distance — wide open.
	}
	const float denom = 0.033f * p_distance * p_scale;
	if (denom <= 0.0001f) {
		return 20000.0f;
	}
	float f_c = 4000.0f * Math::pow(3.0f / denom, 1.0f / 1.7f);
	return CLAMP(f_c, 200.0f, 20000.0f);
}
