#ifndef TRANSITION_ANALYZER_H
#define TRANSITION_ANALYZER_H

#include "core/object/object.h"
#include "core/object/class_db.h"
#include "servers/audio/audio_stream.h"

/// TransitionAnalyzer — Spectral/loudness analysis for music transitions.
///
/// Computes a feasibility score (0.0-1.0) for a transition between two AudioStreams
/// based on the paper metric: Feasibility = L(n,n+1) + S(n,n+1) + D(n,n+1)
///   L = loudness continuity (RMS difference at transition boundary)
///   S = spectral continuity (centroid distance at transition boundary)
///   D = duration appropriateness (not applicable here — handled by quantization)
///
/// Analysis runs on the main thread at transition-request time. Uses PFFFT for spectral
/// analysis. Allocates temporary buffers (~16KB) and frees immediately after.
///
/// Usage (from MusicSystem GDScript via bound methods):
///   var result = TransitionAnalyzer.analyze_transition(outgoing_stream, incoming_stream, 1.0)
///   # result: {feasibility: 0.82, recommended_curve: "equal_power", rms_delta_db: -2.1, centroid_ratio: 1.3}
///
/// Singleton — created in register_types.cpp.
class TransitionAnalyzer : public Object {
	GDCLASS(TransitionAnalyzer, Object);

public:
	/// Crossfade curve recommendation based on spectral analysis.
	enum CurveRecommendation {
		CURVE_LINEAR = 0,       // Spectrally similar — linear crossfade is transparent
		CURVE_EQUAL_POWER,      // Moderate spectral difference — equal-power avoids dip
		CURVE_S_CURVE,          // Large spectral difference — S-curve masks the transition
		CURVE_FADE_SILENCE,     // Very different material — recommend fade-through-silence instead
	};

private:
	static TransitionAnalyzer *singleton;

	// Configuration
	int fft_size = 2048;            // FFT analysis window (2048 samples ≈ 46ms at 44.1kHz)
	float analysis_mix_rate = 44100.0f;

	// Thresholds for curve recommendation
	float centroid_ratio_linear_max = 1.3f;       // Below this → LINEAR
	float centroid_ratio_equal_power_max = 1.8f;  // Below this → EQUAL_POWER
	float centroid_ratio_s_curve_max = 2.5f;      // Below this → S_CURVE, above → FADE_SILENCE
	float rms_delta_linear_max_db = 3.0f;         // Below this → LINEAR
	float rms_delta_equal_power_max_db = 6.0f;    // Below this → EQUAL_POWER

	// Internal helpers
	float _compute_rms(const float *p_samples, int p_count) const;
	float _compute_spectral_centroid(const float *p_samples, int p_count) const;
	CurveRecommendation _recommend_curve(float p_rms_delta_db, float p_centroid_ratio) const;
	float _compute_feasibility(float p_rms_delta_db, float p_centroid_ratio) const;

	// Render audio from a stream into a buffer. Returns actual samples rendered.
	// p_from_end: if true, renders the tail of the stream (last fft_size samples).
	// p_from_start: if true, renders the head of the stream (first fft_size samples).
	int _render_samples(const Ref<AudioStream> &p_stream, float *p_buffer, int p_count, bool p_from_end) const;

protected:
	static void _bind_methods();

public:
	static TransitionAnalyzer *get_singleton() { return singleton; }

	/// Analyze a transition between two streams.
	/// p_outgoing: the stream currently playing (analyzes tail).
	/// p_incoming: the stream about to start (analyzes head).
	/// p_crossfade_duration: planned crossfade duration in seconds (used for duration appropriateness).
	///
	/// Returns Dictionary:
	///   "feasibility": float (0.0-1.0, higher = smoother transition)
	///   "recommended_curve": int (CurveRecommendation enum)
	///   "recommended_curve_name": String ("linear", "equal_power", "s_curve", "fade_silence")
	///   "rms_delta_db": float (loudness difference at boundary, 0 = same level)
	///   "centroid_ratio": float (spectral brightness ratio, 1.0 = identical)
	///   "outgoing_rms_db": float (RMS of outgoing tail in dB)
	///   "incoming_rms_db": float (RMS of incoming head in dB)
	///   "outgoing_centroid_hz": float
	///   "incoming_centroid_hz": float
	Dictionary analyze_transition(const Ref<AudioStream> &p_outgoing, const Ref<AudioStream> &p_incoming, float p_crossfade_duration = 1.0f) const;

	/// Simplified API: returns just the feasibility score (0.0-1.0).
	float get_feasibility_score(const Ref<AudioStream> &p_outgoing, const Ref<AudioStream> &p_incoming) const;

	/// Get the recommended curve for a pair of streams.
	CurveRecommendation get_recommended_curve(const Ref<AudioStream> &p_outgoing, const Ref<AudioStream> &p_incoming) const;

	// Configuration
	void set_fft_size(int p_size);
	int get_fft_size() const { return fft_size; }
	void set_analysis_mix_rate(float p_rate);
	float get_analysis_mix_rate() const { return analysis_mix_rate; }

	TransitionAnalyzer();
	~TransitionAnalyzer();
};

VARIANT_ENUM_CAST(TransitionAnalyzer::CurveRecommendation);

#endif // TRANSITION_ANALYZER_H
