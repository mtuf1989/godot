#include "beat_clock.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/config/engine.h"
#include "scene/resources/audio/audio_stream.h"
#include "servers/audio/audio_server.h"

BeatClock *BeatClock::singleton = nullptr;

void BeatClock::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_bpm", "bpm"), &BeatClock::set_bpm);
	ClassDB::bind_method(D_METHOD("get_bpm"), &BeatClock::get_bpm);
	ClassDB::bind_method(D_METHOD("set_beats_per_bar", "beats"), &BeatClock::set_beats_per_bar);
	ClassDB::bind_method(D_METHOD("get_beats_per_bar"), &BeatClock::get_beats_per_bar);
	ClassDB::bind_method(D_METHOD("is_playing"), &BeatClock::is_playing);

	ClassDB::bind_method(D_METHOD("start", "bpm", "beats_per_bar"), &BeatClock::start);
	ClassDB::bind_method(D_METHOD("stop"), &BeatClock::stop);
	ClassDB::bind_method(D_METHOD("update_playback_position", "position"), &BeatClock::update_playback_position);

	ClassDB::bind_method(D_METHOD("get_current_beat"), &BeatClock::get_current_beat);
	ClassDB::bind_method(D_METHOD("get_current_bar"), &BeatClock::get_current_bar);
	ClassDB::bind_method(D_METHOD("get_beat_fraction"), &BeatClock::get_beat_fraction);
	ClassDB::bind_method(D_METHOD("get_time_to_next_beat"), &BeatClock::get_time_to_next_beat);
	ClassDB::bind_method(D_METHOD("get_time_to_next_bar"), &BeatClock::get_time_to_next_bar);
	ClassDB::bind_method(D_METHOD("process", "delta"), &BeatClock::process);

	// S4.4: Music Moment Integration
	ClassDB::bind_method(D_METHOD("calculate_time_stretch_for_alignment", "target_time_sec", "tolerance_percent", "max_stretch"), &BeatClock::calculate_time_stretch_for_alignment, DEFVAL(5.0f), DEFVAL(0.1f));
	ClassDB::bind_method(D_METHOD("calculate_duration_stretch_for_alignment", "target_time_sec", "tolerance_percent", "max_stretch"), &BeatClock::calculate_duration_stretch_for_alignment, DEFVAL(5.0f), DEFVAL(0.1f));

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bpm"), "set_bpm", "get_bpm");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "beats_per_bar"), "set_beats_per_bar", "get_beats_per_bar");

	ADD_SIGNAL(MethodInfo("beat_hit", PropertyInfo(Variant::INT, "beat_index")));
	ADD_SIGNAL(MethodInfo("bar_hit", PropertyInfo(Variant::INT, "bar_index")));
}

BeatClock::BeatClock() {
	singleton = this;
}

BeatClock::~BeatClock() {
	singleton = nullptr;
}

void BeatClock::set_bpm(float p_bpm) {
	bpm = CLAMP(p_bpm, 1.0f, 999.0f);
}

void BeatClock::set_beats_per_bar(int p_beats) {
	beats_per_bar = CLAMP(p_beats, 1, 32);
}

void BeatClock::start(float p_bpm, int p_beats_per_bar) {
	bpm = CLAMP(p_bpm, 1.0f, 999.0f);
	beats_per_bar = CLAMP(p_beats_per_bar, 1, 32);
	playing = true;
	playback_position = 0.0;
	logic_time = 0.0;
	prev_beat_index = -1;
	prev_bar_index = -1;
	last_process_frame = UINT64_MAX;
	_refresh_latency_cache();
}

void BeatClock::stop() {
	playing = false;
	prev_beat_index = -1;
	prev_bar_index = -1;
}

void BeatClock::update_playback_position(double p_position) {
	playback_position = p_position;
}

void BeatClock::_refresh_latency_cache() {
	uint64_t now = OS::get_singleton()->get_ticks_msec();
	if (now - latency_cache_time > 1000) {
		cached_output_latency = (float)AudioServer::get_singleton()->get_output_latency();
		latency_cache_time = now;
	}
}

double BeatClock::_get_corrected_time() const {
	if (!playing) {
		return 0.0;
	}
	double time_since_mix = AudioServer::get_singleton()->get_time_since_last_mix();
	double corrected = playback_position + time_since_mix - (double)cached_output_latency;
	if (corrected < 0.0) {
		corrected = 0.0;
	}
	return corrected;
}

float BeatClock::get_current_beat() const {
	if (!playing) return 0.0f;
	double t = _get_corrected_time();
	return (float)(t * (double)bpm / 60.0);
}

int BeatClock::get_current_bar() const {
	if (!playing) return 0;
	float beat = get_current_beat();
	return (int)(beat / (float)beats_per_bar);
}

float BeatClock::get_beat_fraction() const {
	if (!playing) return 0.0f;
	float beat = get_current_beat();
	return beat - (float)(int)beat;
}

float BeatClock::get_time_to_next_beat() const {
	if (!playing || bpm <= 0.0f) return 0.0f;
	float beat = get_current_beat();
	float next_beat = (float)((int)beat + 1);
	float beat_duration = 60.0f / bpm;
	return (next_beat - beat) * beat_duration;
}

float BeatClock::get_time_to_next_bar() const {
	if (!playing || bpm <= 0.0f) return 0.0f;
	float beat = get_current_beat();
	int current_bar = (int)(beat / (float)beats_per_bar);
	float next_bar_beat = (float)((current_bar + 1) * beats_per_bar);
	float beat_duration = 60.0f / bpm;
	return (next_bar_beat - beat) * beat_duration;
}

void BeatClock::process(double p_delta) {
	if (!playing) return;

	// Double-call guard: skip if already processed this frame.
	uint64_t current_frame = Engine::get_singleton()->get_process_frames();
	if (current_frame == last_process_frame) {
		return;
	}
	last_process_frame = current_frame;

	_refresh_latency_cache();

	// Drift correction: advance logic_time by delta, compare with audio time
	logic_time += p_delta;
	double audio_time = _get_corrected_time();
	double drift = audio_time - logic_time;

	if (Math::abs(drift) > 0.050) {
		// Snap correction for large drift (>50ms)
		logic_time = audio_time;
	} else if (Math::abs(drift) > 0.002) {
		// Proportional correction (Kp=10) for medium drift (>2ms)
		logic_time += drift * 10.0 * p_delta;
	}
	// Dead band: drift < 2ms — ignore

	// Beat detection using corrected time
	float beat = (float)(logic_time * (double)bpm / 60.0);
	int beat_index = (int)beat;
	int bar_index = beat_index / beats_per_bar;

	if (beat_index > prev_beat_index && prev_beat_index >= 0) {
		emit_signal(SNAME("beat_hit"), beat_index);
	}
	if (bar_index > prev_bar_index && prev_bar_index >= 0) {
		emit_signal(SNAME("bar_hit"), bar_index);
	}

	prev_beat_index = beat_index;
	prev_bar_index = bar_index;
}

float BeatClock::calculate_time_stretch_for_alignment(float p_target_time_sec, float p_tolerance_percent, float p_max_stretch) const {
	// S4.4: Music Moment Integration
	// Calculate the stretch ratio needed so that the next bar boundary
	// arrives exactly at p_target_time_sec from now.
	//
	// Use case: "Boss door opens in 3.2 seconds — stretch music so downbeat hits at that moment."

	if (!playing || bpm <= 0.0f) {
		return 1.0f;
	}

	// How long until the next bar boundary at current tempo?
	float time_to_next_bar = get_time_to_next_bar();

	if (time_to_next_bar < 0.001f) {
		// We're essentially AT the bar boundary already
		return 1.0f;
	}

	// Target time must be positive
	if (p_target_time_sec <= 0.0f) {
		return 1.0f;
	}

	// Calculate required stretch ratio:
	// If time_to_next_bar / stretch = p_target_time_sec
	// Then stretch = time_to_next_bar / p_target_time_sec
	float stretch = time_to_next_bar / p_target_time_sec;

	// Check if we're already within tolerance (no stretch needed)
	float deviation_percent = Math::abs(stretch - 1.0f) * 100.0f;
	if (deviation_percent <= p_tolerance_percent) {
		return 1.0f; // Close enough, no correction needed
	}

	// Clamp stretch to max allowed deviation
	float min_stretch = 1.0f - p_max_stretch;
	float max_stretch = 1.0f + p_max_stretch;
	stretch = CLAMP(stretch, min_stretch, max_stretch);

	return stretch;
}

float BeatClock::calculate_duration_stretch_for_alignment(float p_target_time_sec, float p_tolerance_percent, float p_max_stretch) const {
	float rate_stretch = calculate_time_stretch_for_alignment(p_target_time_sec, p_tolerance_percent, p_max_stretch);
	if (rate_stretch <= 0.0f || rate_stretch == 1.0f) {
		return 1.0f;
	}
	return 1.0f / rate_stretch;
}