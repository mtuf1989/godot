#ifndef SPATIAL_GRAPH_WRAPPER_H
#define SPATIAL_GRAPH_WRAPPER_H

#include "../stream/audio_stream_symphony.h"
#include "core/string/string_name.h"
#include "scene/resources/audio/audio_stream.h"
#include "scene/resources/audio/audio_stream_wav.h"

// Factory that generates a spatial-processing AudioStreamSymphony for plain
// AudioStreamWAV resources (16-bit PCM only) that don't have their own graph.
// Unsupported formats are not wrapped — callers keep native Godot playback.
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
	// Lazy StringName accessors (SNAME) — never construct StringName at static init.
	static StringName param_air_cutoff() { return SNAME("spatial_air_cutoff"); }
	static StringName param_occlusion_cutoff() { return SNAME("spatial_occlusion_cutoff"); }
	static StringName param_gain() { return SNAME("spatial_gain"); }

	// Create a wrapped AudioStreamSymphony for a 16-bit PCM WAV with a resource path.
	// Returns null if the stream cannot be loaded by WavePlayer (wrong type/format/path).
	static Ref<AudioStreamSymphony> create_spatial_stream(const Ref<AudioStream> &p_source, bool p_loop = false);

	// True only when wrapping will succeed (16-bit WAV with a path, not already Symphony).
	static bool needs_wrapping(const Ref<AudioStream> &p_stream);

	// True when p_stream is a 16-bit PCM AudioStreamWAV with a non-empty resource path.
	static bool is_wrappable_wav(const Ref<AudioStream> &p_stream);

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
