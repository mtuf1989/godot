#include "beat_clock.h"
#include "core/config/engine.h"
#include "core/math/math_funcs.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "servers/audio/audio_server.h"

BeatClock *BeatClock::singleton = nullptr;

void BeatClock::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_bpm", "bpm"), &BeatClock::set_bpm);
	ClassDB::bind_method(D_METHOD("get_bpm"), &BeatClock::get_bpm);
	ClassDB::bind_method(D_METHOD("set_beats_per_bar", "beats"), &BeatClock::set_beats_per_bar);
	ClassDB::bind_method(D_METHOD("get_beats_per_bar"), &BeatClock::get_beats_per_bar);
	ClassDB::bind_method(D_METHOD("set_fire_ahead_s", "seconds"), &BeatClock::set_fire_ahead_s);
	ClassDB::bind_method(D_METHOD("get_fire_ahead_s"), &BeatClock::get_fire_ahead_s);
	ClassDB::bind_method(D_METHOD("set_loop_length_hint", "seconds"), &BeatClock::set_loop_length_hint);
	ClassDB::bind_method(D_METHOD("get_loop_length_hint"), &BeatClock::get_loop_length_hint);
	ClassDB::bind_method(D_METHOD("is_playing"), &BeatClock::is_playing);

	ClassDB::bind_method(D_METHOD("start", "bpm", "beats_per_bar"), &BeatClock::start);
	ClassDB::bind_method(D_METHOD("stop"), &BeatClock::stop);
	ClassDB::bind_method(D_METHOD("update_playback_position", "position"), &BeatClock::update_playback_position);
	ClassDB::bind_method(D_METHOD("update_transport", "beat_position", "bpm", "beats_per_bar"), &BeatClock::update_transport);

	ClassDB::bind_method(D_METHOD("get_current_beat"), &BeatClock::get_current_beat);
	ClassDB::bind_method(D_METHOD("get_current_bar"), &BeatClock::get_current_bar);
	ClassDB::bind_method(D_METHOD("get_beat_fraction"), &BeatClock::get_beat_fraction);
	ClassDB::bind_method(D_METHOD("get_time_to_next_beat"), &BeatClock::get_time_to_next_beat);
	ClassDB::bind_method(D_METHOD("get_time_to_next_bar"), &BeatClock::get_time_to_next_bar);
	ClassDB::bind_method(D_METHOD("process", "delta"), &BeatClock::process);

	ClassDB::bind_method(D_METHOD("calculate_time_stretch_for_alignment", "target_time_sec", "tolerance_percent", "max_stretch"), &BeatClock::calculate_time_stretch_for_alignment, DEFVAL(5.0f), DEFVAL(0.1f));
	ClassDB::bind_method(D_METHOD("calculate_duration_stretch_for_alignment", "target_time_sec", "tolerance_percent", "max_stretch"), &BeatClock::calculate_duration_stretch_for_alignment, DEFVAL(5.0f), DEFVAL(0.1f));

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bpm"), "set_bpm", "get_bpm");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "beats_per_bar"), "set_beats_per_bar", "get_beats_per_bar");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fire_ahead_s"), "set_fire_ahead_s", "get_fire_ahead_s");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "loop_length_hint"), "set_loop_length_hint", "get_loop_length_hint");

	ADD_SIGNAL(MethodInfo("beat_hit",
			PropertyInfo(Variant::INT, "beat_index"),
			PropertyInfo(Variant::FLOAT, "target_time")));
	ADD_SIGNAL(MethodInfo("bar_hit",
			PropertyInfo(Variant::INT, "bar_index"),
			PropertyInfo(Variant::FLOAT, "target_time")));
}

BeatClock::BeatClock() {
	singleton = this;
}

BeatClock::~BeatClock() {
	singleton = nullptr;
}

void BeatClock::set_bpm(float p_bpm) {
	// Keep beat_acc; only the slope changes (N1).
	bpm = CLAMP(p_bpm, 1.0f, 999.0f);
}

void BeatClock::set_beats_per_bar(int p_beats) {
	beats_per_bar = CLAMP(p_beats, 1, 32);
}

void BeatClock::set_fire_ahead_s(float p_seconds) {
	fire_ahead_s = MAX(0.0f, p_seconds);
}

void BeatClock::set_loop_length_hint(double p_seconds) {
	loop_length_hint = MAX(0.0, p_seconds);
}

void BeatClock::start(float p_bpm, int p_beats_per_bar) {
	bpm = CLAMP(p_bpm, 1.0f, 999.0f);
	beats_per_bar = CLAMP(p_beats_per_bar, 1, 32);
	playing = true;
	playback_position = 0.0;
	prev_playback_position = 0.0;
	logic_time = 0.0;
	beat_acc = 0.0;
	transport_driven = false;
	transport_beat = 0.0;
	prev_beat_index = -1;
	prev_bar_index = -1;
	last_process_frame = UINT64_MAX;
	last_processed_position = -1.0;
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

void BeatClock::update_transport(double p_beat_position, float p_bpm, int p_beats_per_bar) {
	bpm = CLAMP(p_bpm, 1.0f, 999.0f);
	beats_per_bar = CLAMP(p_beats_per_bar, 1, 32);
	transport_beat = MAX(0.0, p_beat_position);
	transport_driven = true;
}

void BeatClock::_refresh_latency_cache() {
	if (!AudioServer::get_singleton()) {
		return;
	}
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
	double time_since_mix = 0.0;
	if (AudioServer::get_singleton()) {
		time_since_mix = AudioServer::get_singleton()->get_time_since_last_mix();
	}
	double corrected = playback_position + time_since_mix - (double)cached_output_latency + (double)fire_ahead_s;
	if (corrected < 0.0) {
		corrected = 0.0;
	}
	return corrected;
}

float BeatClock::get_current_beat() const {
	if (!playing) {
		return 0.0f;
	}
	return (float)beat_acc;
}

int BeatClock::get_current_bar() const {
	if (!playing || beats_per_bar <= 0) {
		return 0;
	}
	return (int)(beat_acc / (double)beats_per_bar);
}

float BeatClock::get_beat_fraction() const {
	if (!playing) {
		return 0.0f;
	}
	const double beat = beat_acc;
	return (float)(beat - Math::floor(beat));
}

float BeatClock::get_time_to_next_beat() const {
	if (!playing || bpm <= 0.0f) {
		return 0.0f;
	}
	const float frac = get_beat_fraction();
	const float beat_duration = 60.0f / bpm;
	return (1.0f - frac) * beat_duration;
}

float BeatClock::get_time_to_next_bar() const {
	if (!playing || bpm <= 0.0f || beats_per_bar <= 0) {
		return 0.0f;
	}
	const double beats_into_bar = beat_acc - Math::floor(beat_acc / (double)beats_per_bar) * (double)beats_per_bar;
	const double beats_left = (double)beats_per_bar - beats_into_bar;
	return (float)(beats_left * (60.0 / (double)bpm));
}

void BeatClock::_emit_crossed(double p_from_beat, double p_to_beat) {
	if (p_to_beat <= p_from_beat) {
		return;
	}
	const int from_i = (int)Math::floor(p_from_beat);
	const int to_i = (int)Math::floor(p_to_beat);
	int emitted = 0;
	for (int bi = from_i + 1; bi <= to_i && emitted < MAX_EMIT_PER_FRAME; bi++) {
		if (bi < 0) {
			continue;
		}
		// Exact musical time of this beat index at current bpm (heard/target).
		const float target_time = (bpm > 0.0f) ? ((float)bi * 60.0f / bpm) : 0.0f;
		emit_signal(SNAME("beat_hit"), bi, target_time);
		emitted++;
		if (beats_per_bar > 0 && (bi % beats_per_bar) == 0) {
			const int bar_i = bi / beats_per_bar;
			emit_signal(SNAME("bar_hit"), bar_i, target_time);
		}
		prev_beat_index = bi;
		if (beats_per_bar > 0) {
			prev_bar_index = bi / beats_per_bar;
		}
	}
}

void BeatClock::process(double p_delta) {
	if (!playing) {
		return;
	}

	uint64_t current_frame = Engine::get_singleton()->get_process_frames();
	const double guard_pos = transport_driven ? transport_beat : playback_position;
	// Skip duplicate calls in one engine frame when position is unchanged.
	// Headless tests keep process_frames at 0, so still allow position advances.
	if (current_frame == last_process_frame && guard_pos == last_processed_position) {
		return;
	}
	last_process_frame = current_frame;
	last_processed_position = guard_pos;

	_refresh_latency_cache();

	const double ahead_beats = (bpm > 0.0f) ? ((double)fire_ahead_s * (double)bpm / 60.0) : 0.0;
	if (transport_driven) {
		const double before = beat_acc;
		beat_acc = transport_beat;
		if (prev_beat_index < 0) {
			prev_beat_index = (int)Math::floor(beat_acc);
			prev_bar_index = beats_per_bar > 0 ? prev_beat_index / beats_per_bar : 0;
			if (before <= 0.0 && beat_acc >= 0.0 && prev_beat_index == 0) {
				emit_signal(SNAME("beat_hit"), 0, 0.0f);
				emit_signal(SNAME("bar_hit"), 0, 0.0f);
			}
			_emit_crossed(beat_acc, beat_acc + ahead_beats);
			return;
		}
		_emit_crossed(before + ahead_beats, beat_acc + ahead_beats);
		return;
	}

	const double audio_time = _get_corrected_time();
	const double prev_pos = prev_playback_position;
	const double new_pos = playback_position;
	prev_playback_position = playback_position;

	double delta_pos = new_pos - prev_pos;
	if (delta_pos < 0.0) {
		// Loop wrap or stream switch.
		const bool wrapped_to_start = new_pos < 0.05 || (loop_length_hint > 0.0 && new_pos < loop_length_hint * 0.05);
		if (wrapped_to_start && loop_length_hint > 0.0) {
			// Continue accumulator across the loop seam.
			delta_pos = (loop_length_hint - prev_pos) + new_pos;
			if (delta_pos < 0.0) {
				delta_pos = new_pos; // fallback
			}
		} else if (wrapped_to_start) {
			// Unknown loop length: treat jump to ~0 as a full wrap of whatever we just left.
			delta_pos = MAX(0.0, -prev_pos) + new_pos;
			if (delta_pos <= 0.0) {
				delta_pos = 0.0;
			}
			// Re-base downbeat when wrapping without a length hint and large discontinuity.
			if (prev_pos > 0.25 && new_pos < 0.05) {
				const double before = beat_acc;
				beat_acc = new_pos * (double)bpm / 60.0;
				// Force bar 0 / beat 0 emission on loop.
				_emit_crossed(before + ahead_beats - 0.001, beat_acc + ahead_beats + 0.001);
				// Also ensure beat 0 fires when rebased to near-zero.
				if (prev_beat_index != 0) {
					emit_signal(SNAME("beat_hit"), 0, 0.0f);
					emit_signal(SNAME("bar_hit"), 0, 0.0f);
					prev_beat_index = 0;
					prev_bar_index = 0;
				}
				logic_time = audio_time;
				return;
			}
		} else {
			// Stream switch / scrub: re-base accumulator.
			const double before = beat_acc;
			beat_acc = new_pos * (double)bpm / 60.0;
			_emit_crossed(before + ahead_beats, beat_acc + ahead_beats);
			logic_time = audio_time;
			return;
		}
	}

	// Drift-smoothed logic clock (same as queries).
	logic_time += p_delta;
	const double drift = audio_time - logic_time;
	if (Math::abs(drift) > 0.050) {
		logic_time = audio_time;
	} else if (Math::abs(drift) > 0.002) {
		logic_time += drift * 10.0 * p_delta;
	}

	const double before = beat_acc;
	if (bpm > 0.0f) {
		if (delta_pos > 0.0) {
			beat_acc += delta_pos * (double)bpm / 60.0;
		} else {
			// No position advance this frame: hold accumulator (queries use beat_acc).
		}
	}

	// First process after start: establish baseline without emitting a phantom beat.
	if (prev_beat_index < 0) {
		prev_beat_index = (int)Math::floor(beat_acc);
		prev_bar_index = beats_per_bar > 0 ? prev_beat_index / beats_per_bar : 0;
		// Downbeat at t=0: emit beat 0 / bar 0 once when starting at rest.
		if (before <= 0.0 && beat_acc >= 0.0 && prev_beat_index == 0) {
			emit_signal(SNAME("beat_hit"), 0, 0.0f);
			emit_signal(SNAME("bar_hit"), 0, 0.0f);
		}
		_emit_crossed(beat_acc, beat_acc + ahead_beats);
		return;
	}

	_emit_crossed(before + ahead_beats, beat_acc + ahead_beats);
}

float BeatClock::calculate_time_stretch_for_alignment(float p_target_time_sec, float p_tolerance_percent, float p_max_stretch) const {
	if (!playing || bpm <= 0.0f) {
		return 1.0f;
	}
	float time_to_next_bar = get_time_to_next_bar();
	if (time_to_next_bar < 0.001f || p_target_time_sec <= 0.0f) {
		return 1.0f;
	}
	float stretch = time_to_next_bar / p_target_time_sec;
	float deviation_percent = Math::abs(stretch - 1.0f) * 100.0f;
	if (deviation_percent <= p_tolerance_percent) {
		return 1.0f;
	}
	return CLAMP(stretch, 1.0f - p_max_stretch, 1.0f + p_max_stretch);
}

float BeatClock::calculate_duration_stretch_for_alignment(float p_target_time_sec, float p_tolerance_percent, float p_max_stretch) const {
	float rate_stretch = calculate_time_stretch_for_alignment(p_target_time_sec, p_tolerance_percent, p_max_stretch);
	if (rate_stretch <= 0.0f || rate_stretch == 1.0f) {
		return 1.0f;
	}
	return 1.0f / rate_stretch;
}
