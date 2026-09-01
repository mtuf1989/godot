#ifndef SPATIAL_GRAPH_WRAPPER_H
#define SPATIAL_GRAPH_WRAPPER_H

#include "../stream/audio_stream_symphony.h"
#include "scene/resources/audio/audio_stream.h"

// Factory that generates a spatial-processing AudioStreamSymphony for plain
// AudioStream resources (WAVs, OGGs, etc.) that don't have their own graph.
//
// The generated graph:
//   WavePlayer → OnePole (air absorption) → SVFilter LP (occlusion) → Gain → GraphOutput
//
// Three GraphInput nodes expose runtime parameters:
//   "spatial_air_cutoff"       → OnePole cutoff (Hz, default 20000)
//   "spatial_occlusion_cutoff" → SVFilter cutoff (Hz, default 20000)
//   "spatial_gain"             → Gain multiplier (0-1, default 1.0)
//
// The caller drives these parameters per frame via set_parameter() based on
// SpatialAcousticsEngine output (occlusion solver, air absorption, etc.).
//
// Graph-authored SoundEvents (those already using AudioStreamSymphony) are NOT
// wrapped — they use their own graph as-is.
class SpatialGraphWrapper {
public:
	// Parameter names used by the wrapper graph.
	static const StringName PARAM_AIR_CUTOFF;
	static const StringName PARAM_OCCLUSION_CUTOFF;
	static const StringName PARAM_GAIN;

	// Create a wrapped AudioStreamSymphony for the given plain audio stream.
	// The stream's resource_path is used by WavePlayer to load the audio.
	// Returns null if the stream has no resource_path (can't be loaded by WavePlayer).
	static Ref<AudioStreamSymphony> create_spatial_stream(const Ref<AudioStream> &p_source, bool p_loop = false);

	// Check if a stream is already an AudioStreamSymphony (no wrapping needed).
	static bool needs_wrapping(const Ref<AudioStream> &p_stream);

	// Compute occlusion cutoff from transmission values.
	// Uses minimum-frequency stacking (plan requirement): takes the min of
	// the derived cutoff from each band rather than multiplying.
	// Transmission 1.0 → 20000 Hz (open), 0.0 → 200 Hz (fully occluded).
	static float transmission_to_cutoff(float p_transmission_mid, float p_transmission_high);

	// Compute air absorption cutoff from distance.
	// Models HF rolloff due to air absorption using log-frequency scaling.
	// At 0m → 20000 Hz, increases distance → lower cutoff.
	// Air absorption cutoff (Hz) as a function of absolute source→listener
	// distance (metres). Distance-absolute ISO 9613-1 fit (Phase 4.1) — no
	// longer normalized against an event's max_distance. `p_scale` is the
	// artistic knob (project setting audio/symphony/air_absorption_scale):
	// 1.0 = physical, >1 = harsher HF rolloff, 0 = disabled (returns 20 kHz).
	static float distance_to_air_cutoff(float p_distance, float p_scale = 1.0f);
};

#endif // SPATIAL_GRAPH_WRAPPER_H
