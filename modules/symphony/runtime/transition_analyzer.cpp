#include "transition_analyzer.h"
#include "core/object/class_db.h"
#include "core/math/math_funcs.h"
#include "servers/audio/audio_server.h"
#include "pffft.h"

#include <cstring>
#include <cmath>

TransitionAnalyzer *TransitionAnalyzer::singleton = nullptr;

TransitionAnalyzer::TransitionAnalyzer() {
	singleton = this;
	analysis_mix_rate = AudioServer::get_singleton() ? AudioServer::get_singleton()->get_mix_rate() : 44100.0f;
}

TransitionAnalyzer::~TransitionAnalyzer() {
	singleton = nullptr;
}

void TransitionAnalyzer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("analyze_transition", "outgoing", "incoming", "crossfade_duration"), &TransitionAnalyzer::analyze_transition, DEFVAL(1.0f));
	ClassDB::bind_method(D_METHOD("get_feasibility_score", "outgoing", "incoming"), &TransitionAnalyzer::get_feasibility_score);
	ClassDB::bind_method(D_METHOD("get_recommended_curve", "outgoing", "incoming"), &TransitionAnalyzer::get_recommended_curve);
	ClassDB::bind_method(D_METHOD("set_fft_size", "size"), &TransitionAnalyzer::set_fft_size);
	ClassDB::bind_method(D_METHOD("get_fft_size"), &TransitionAnalyzer::get_fft_size);
	ClassDB::bind_method(D_METHOD("set_analysis_mix_rate", "rate"), &TransitionAnalyzer::set_analysis_mix_rate);
	ClassDB::bind_method(D_METHOD("get_analysis_mix_rate"), &TransitionAnalyzer::get_analysis_mix_rate);

	BIND_ENUM_CONSTANT(CURVE_LINEAR);
	BIND_ENUM_CONSTANT(CURVE_EQUAL_POWER);
	BIND_ENUM_CONSTANT(CURVE_S_CURVE);
	BIND_ENUM_CONSTANT(CURVE_FADE_SILENCE);
}

void TransitionAnalyzer::set_fft_size(int p_size) {
	// Must be a power of 2, minimum 256, maximum 4096.
	if (p_size < 256) p_size = 256;
	if (p_size > 4096) p_size = 4096;
	// Round to nearest power of 2
	int result = 256;
	while (result < p_size) result <<= 1;
	fft_size = result;
}

void TransitionAnalyzer::set_analysis_mix_rate(float p_rate) {
	analysis_mix_rate = CLAMP(p_rate, 8000.0f, 192000.0f);
}

// ============================================================================
// Audio Rendering
// ============================================================================

int TransitionAnalyzer::_render_samples(const Ref<AudioStream> &p_stream, float *p_buffer, int p_count, bool p_from_end) const {
	if (p_stream.is_null()) {
		memset(p_buffer, 0, sizeof(float) * p_count);
		return 0;
	}

	Ref<AudioStreamPlayback> playback = p_stream->instantiate_playback();
	if (playback.is_null()) {
		memset(p_buffer, 0, sizeof(float) * p_count);
		return 0;
	}

	double length = p_stream->get_length();

	if (p_from_end && length > 0.0) {
		// Seek to the tail of the stream (last p_count samples worth of time).
		double tail_start = length - (double)p_count / analysis_mix_rate;
		if (tail_start < 0.0) tail_start = 0.0;
		playback->start(tail_start);
	} else {
		playback->start(0.0);
	}

	// Render into an AudioFrame buffer (stereo), then downmix to mono.
	Vector<AudioFrame> frames;
	frames.resize(p_count);
	AudioFrame *frame_ptr = frames.ptrw();
	memset(frame_ptr, 0, sizeof(AudioFrame) * p_count);

	int rendered = playback->mix(frame_ptr, 1.0f, p_count);

	// Downmix stereo to mono.
	for (int i = 0; i < p_count; i++) {
		p_buffer[i] = (frame_ptr[i].left + frame_ptr[i].right) * 0.5f;
	}

	return rendered > 0 ? rendered : p_count;
}

// ============================================================================
// Signal Analysis
// ============================================================================

float TransitionAnalyzer::_compute_rms(const float *p_samples, int p_count) const {
	if (p_count <= 0) return 0.0f;

	float sum_sq = 0.0f;
	for (int i = 0; i < p_count; i++) {
		sum_sq += p_samples[i] * p_samples[i];
	}
	return sqrtf(sum_sq / (float)p_count);
}

float TransitionAnalyzer::_compute_spectral_centroid(const float *p_samples, int p_count) const {
	if (p_count < 256) return 0.0f;

	// Use a temporary PFFFT setup for this analysis.
	int N = fft_size;
	if (p_count < N) {
		N = 256;
		while (N * 2 <= p_count) N *= 2;
	}

	PFFFT_Setup *setup = pffft_new_setup(N, PFFFT_REAL);
	if (!setup) return 0.0f;

	// Allocate aligned temporary buffers.
	float *windowed = (float *)pffft_aligned_malloc(sizeof(float) * N);
	float *fft_out = (float *)pffft_aligned_malloc(sizeof(float) * N);
	float *work = (float *)pffft_aligned_malloc(sizeof(float) * N);

	// Apply Hanning window to the last N samples of the input.
	int offset = p_count > N ? p_count - N : 0;
	for (int i = 0; i < N; i++) {
		float w = 0.5f * (1.0f - cosf(2.0f * (float)Math::PI * (float)i / (float)(N - 1)));
		windowed[i] = p_samples[offset + i] * w;
	}

	// Forward FFT.
	pffft_transform_ordered(setup, windowed, fft_out, work, PFFFT_FORWARD);

	// Compute spectral centroid from magnitude spectrum.
	// PFFFT ordered real output: [DC, Re1, Im1, Re2, Im2, ..., Nyquist]
	// For real transforms of size N, we have N/2+1 bins.
	float weighted_sum = 0.0f;
	float magnitude_sum = 0.0f;
	float bin_hz = analysis_mix_rate / (float)N;

	int num_bins = N / 2;
	for (int bin = 1; bin < num_bins; bin++) {
		float re = fft_out[bin * 2];
		float im = fft_out[bin * 2 + 1];
		float magnitude = sqrtf(re * re + im * im);
		float freq = (float)bin * bin_hz;

		weighted_sum += freq * magnitude;
		magnitude_sum += magnitude;
	}

	pffft_aligned_free(windowed);
	pffft_aligned_free(fft_out);
	pffft_aligned_free(work);
	pffft_destroy_setup(setup);

	if (magnitude_sum < 1e-10f) return 0.0f;
	return weighted_sum / magnitude_sum;
}

// ============================================================================
// Scoring & Recommendation
// ============================================================================

TransitionAnalyzer::CurveRecommendation TransitionAnalyzer::_recommend_curve(float p_rms_delta_db, float p_centroid_ratio) const {
	float abs_rms_delta = Math::abs(p_rms_delta_db);

	// Spectral similarity dominates the decision.
	if (p_centroid_ratio <= centroid_ratio_linear_max && abs_rms_delta <= rms_delta_linear_max_db) {
		return CURVE_LINEAR;
	}
	if (p_centroid_ratio <= centroid_ratio_equal_power_max && abs_rms_delta <= rms_delta_equal_power_max_db) {
		return CURVE_EQUAL_POWER;
	}
	if (p_centroid_ratio <= centroid_ratio_s_curve_max) {
		return CURVE_S_CURVE;
	}
	return CURVE_FADE_SILENCE;
}

float TransitionAnalyzer::_compute_feasibility(float p_rms_delta_db, float p_centroid_ratio) const {
	// Feasibility score: weighted combination of loudness continuity and spectral continuity.
	//
	// L (loudness continuity): 1.0 when RMS delta = 0 dB, decays toward 0 as delta → 12 dB.
	// S (spectral continuity): 1.0 when centroid ratio = 1.0, decays toward 0 as ratio → 4.0.
	//
	// Weights: L=0.4, S=0.6 (spectral discontinuity is perceptually more noticeable than level).

	float abs_rms = Math::abs(p_rms_delta_db);
	float L = 1.0f - CLAMP(abs_rms / 12.0f, 0.0f, 1.0f);
	L = L * L; // Quadratic falloff — small differences are fine, large ones compound

	float S = 1.0f - CLAMP((p_centroid_ratio - 1.0f) / 3.0f, 0.0f, 1.0f);
	S = S * S;

	return 0.4f * L + 0.6f * S;
}

// ============================================================================
// Public API
// ============================================================================

Dictionary TransitionAnalyzer::analyze_transition(const Ref<AudioStream> &p_outgoing, const Ref<AudioStream> &p_incoming, float p_crossfade_duration) const {
	Dictionary result;
	result["feasibility"] = 1.0f;
	result["recommended_curve"] = (int)CURVE_EQUAL_POWER;
	result["recommended_curve_name"] = "equal_power";
	result["rms_delta_db"] = 0.0f;
	result["centroid_ratio"] = 1.0f;
	result["outgoing_rms_db"] = -80.0f;
	result["incoming_rms_db"] = -80.0f;
	result["outgoing_centroid_hz"] = 0.0f;
	result["incoming_centroid_hz"] = 0.0f;

	if (p_outgoing.is_null() && p_incoming.is_null()) {
		return result;
	}

	int analysis_samples = fft_size;

	// Allocate temporary analysis buffers.
	float *outgoing_buf = (float *)memalloc(sizeof(float) * analysis_samples);
	float *incoming_buf = (float *)memalloc(sizeof(float) * analysis_samples);

	// Render tail of outgoing stream and head of incoming stream.
	if (p_outgoing.is_valid()) {
		_render_samples(p_outgoing, outgoing_buf, analysis_samples, true);
	} else {
		memset(outgoing_buf, 0, sizeof(float) * analysis_samples);
	}

	if (p_incoming.is_valid()) {
		_render_samples(p_incoming, incoming_buf, analysis_samples, false);
	} else {
		memset(incoming_buf, 0, sizeof(float) * analysis_samples);
	}

	// Compute RMS for both segments.
	float rms_out = _compute_rms(outgoing_buf, analysis_samples);
	float rms_in = _compute_rms(incoming_buf, analysis_samples);

	// Convert to dB (clamp to avoid -inf).
	float rms_out_db = rms_out > 1e-7f ? 20.0f * log10f(rms_out) : -80.0f;
	float rms_in_db = rms_in > 1e-7f ? 20.0f * log10f(rms_in) : -80.0f;
	float rms_delta_db = rms_in_db - rms_out_db;

	// Compute spectral centroids.
	float centroid_out = _compute_spectral_centroid(outgoing_buf, analysis_samples);
	float centroid_in = _compute_spectral_centroid(incoming_buf, analysis_samples);

	// Centroid ratio (always >= 1.0).
	float centroid_ratio = 1.0f;
	if (centroid_out > 1.0f && centroid_in > 1.0f) {
		centroid_ratio = centroid_in / centroid_out;
		if (centroid_ratio < 1.0f) {
			centroid_ratio = 1.0f / centroid_ratio;
		}
	}

	// Compute feasibility and recommendation.
	float feasibility = _compute_feasibility(rms_delta_db, centroid_ratio);
	CurveRecommendation curve = _recommend_curve(rms_delta_db, centroid_ratio);

	// Duration bonus: longer crossfades are more forgiving.
	// A 2s crossfade can tolerate more spectral distance than a 0.2s one.
	if (p_crossfade_duration >= 2.0f) {
		feasibility = MIN(1.0f, feasibility + 0.1f);
	} else if (p_crossfade_duration < 0.5f) {
		feasibility = MAX(0.0f, feasibility - 0.1f);
	}

	// Map curve enum to name string.
	static const char *curve_names[] = { "linear", "equal_power", "s_curve", "fade_silence" };

	result["feasibility"] = feasibility;
	result["recommended_curve"] = (int)curve;
	result["recommended_curve_name"] = String(curve_names[(int)curve]);
	result["rms_delta_db"] = rms_delta_db;
	result["centroid_ratio"] = centroid_ratio;
	result["outgoing_rms_db"] = rms_out_db;
	result["incoming_rms_db"] = rms_in_db;
	result["outgoing_centroid_hz"] = centroid_out;
	result["incoming_centroid_hz"] = centroid_in;

	memfree(outgoing_buf);
	memfree(incoming_buf);

	return result;
}

float TransitionAnalyzer::get_feasibility_score(const Ref<AudioStream> &p_outgoing, const Ref<AudioStream> &p_incoming) const {
	Dictionary result = analyze_transition(p_outgoing, p_incoming);
	return (float)result["feasibility"];
}

TransitionAnalyzer::CurveRecommendation TransitionAnalyzer::get_recommended_curve(const Ref<AudioStream> &p_outgoing, const Ref<AudioStream> &p_incoming) const {
	Dictionary result = analyze_transition(p_outgoing, p_incoming);
	return (CurveRecommendation)(int)result["recommended_curve"];
}
