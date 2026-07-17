#ifndef BEAT_CLOCK_H
#define BEAT_CLOCK_H

#include "core/object/object.h"
#include "core/object/class_db.h"

// Latency-compensated beat position tracker with drift correction.
// Exposed as a singleton to GDScript. Emits beat_hit/bar_hit signals from _process.
class BeatClock : public Object {
	GDCLASS(BeatClock, Object);

	static BeatClock *singleton;

	// Configuration
	float bpm = 120.0f;
	int beats_per_bar = 4;
	bool playing = false;

	// Playback tracking
	double playback_position = 0.0; // Set externally by MusicSystem
	double logic_time = 0.0;        // Wall-clock accumulated time
	double resume_time = 0.0;       // When playback resumed (for logic clock)

	// Drift correction
	float cached_output_latency = 0.0f;
	uint64_t latency_cache_time = 0;

	// Beat detection state
	int prev_beat_index = -1;
	int prev_bar_index = -1;

	// Double-call guard: tracks the last engine frame we processed
	uint64_t last_process_frame = UINT64_MAX;

	// Internal
	double _get_corrected_time() const;
	void _refresh_latency_cache();

protected:
	static void _bind_methods();

public:
	static BeatClock *get_singleton() { return singleton; }

	// Configuration
	void set_bpm(float p_bpm);
	float get_bpm() const { return bpm; }
	void set_beats_per_bar(int p_beats);
	int get_beats_per_bar() const { return beats_per_bar; }

	// Playback control (called by MusicSystem)
	void start(float p_bpm, int p_beats_per_bar);
	void stop();
	void update_playback_position(double p_position);

	bool is_playing() const { return playing; }

	// Query API (GDScript)
	[[nodiscard]] float get_current_beat() const;
	[[nodiscard]] int get_current_bar() const;
	[[nodiscard]] float get_beat_fraction() const;
	[[nodiscard]] float get_time_to_next_beat() const;
	[[nodiscard]] float get_time_to_next_bar() const;

	// Called each frame to detect beat crossings and emit signals
	void process(double p_delta);

	// S4.4: Music Moment Integration
	// Calculates the time-stretch ratio needed to align the next strong beat
	// (bar boundary) with a target time point.
	// Returns: stretch ratio (e.g., 0.95 = slightly slow down, 1.05 = slightly speed up)
	// Returns 1.0 if not playing, if alignment is impossible, or if within tolerance.
	// tolerance_percent: acceptable deviation (default 5% = within 5% of target is "close enough")
	// max_stretch: maximum allowed stretch deviation from 1.0 (default 0.1 = ±10%)
	[[nodiscard]] float calculate_time_stretch_for_alignment(float p_target_time_sec, float p_tolerance_percent = 5.0f, float p_max_stretch = 0.1f) const;

	// Duration-stretch helper (PhaseVocoder convention).
	// Returns: duration factor for PhaseVocoder (>1.0 = slower/longer, <1.0 = faster/shorter).
	// This is the reciprocal of calculate_time_stretch_for_alignment().
	// Use this when routing the result directly to a PhaseVocoder's time_stretch input.
	[[nodiscard]] float calculate_duration_stretch_for_alignment(float p_target_time_sec, float p_tolerance_percent = 5.0f, float p_max_stretch = 0.1f) const;

	BeatClock();
	~BeatClock();
};

#endif // BEAT_CLOCK_H
