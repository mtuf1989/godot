#ifndef BEAT_CLOCK_H
#define BEAT_CLOCK_H

#include "core/object/object.h"
#include "core/object/class_db.h"

// Latency-compensated beat position tracker with drift correction.
// Incremental beat accumulator (N1): loop-wrap safe, emits every crossed beat
// (capped), optional fire_ahead_s with target_time in signal args.
class BeatClock : public Object {
	GDCLASS(BeatClock, Object);

	static BeatClock *singleton;

	// Configuration
	float bpm = 120.0f;
	int beats_per_bar = 4;
	bool playing = false;
	float fire_ahead_s = 0.0f; // beats fire this early; target_time in signal args
	bool transport_driven = false;
	double transport_beat = 0.0;

	// Playback tracking
	double playback_position = 0.0; // Set externally by MusicSystem
	double prev_playback_position = 0.0;
	double logic_time = 0.0; // Wall-clock accumulated time (smoothed)
	double loop_length_hint = 0.0; // >0: expected loop length for wrap detection

	// Incremental beat accumulator (same units as get_current_beat).
	double beat_acc = 0.0;

	// Drift correction
	float cached_output_latency = 0.0f;
	uint64_t latency_cache_time = 0;

	// Beat detection state (last emitted integer beat/bar)
	int prev_beat_index = -1;
	int prev_bar_index = -1;

	// Double-call guard: same engine frame + unchanged position → skip.
	uint64_t last_process_frame = UINT64_MAX;
	double last_processed_position = -1.0;

	static constexpr int MAX_EMIT_PER_FRAME = 8;

	// Internal
	double _get_corrected_time() const;
	void _refresh_latency_cache();
	void _emit_crossed(double p_from_beat, double p_to_beat);

protected:
	static void _bind_methods();

public:
	static BeatClock *get_singleton() { return singleton; }

	// Configuration
	void set_bpm(float p_bpm);
	float get_bpm() const { return bpm; }
	void set_beats_per_bar(int p_beats);
	int get_beats_per_bar() const { return beats_per_bar; }
	void set_fire_ahead_s(float p_seconds);
	float get_fire_ahead_s() const { return fire_ahead_s; }
	void set_loop_length_hint(double p_seconds);
	double get_loop_length_hint() const { return loop_length_hint; }

	// Playback control (called by MusicSystem)
	void start(float p_bpm, int p_beats_per_bar);
	void stop();
	void update_playback_position(double p_position);
	void update_transport(double p_beat_position, float p_bpm, int p_beats_per_bar);

	bool is_playing() const { return playing; }

	// Query API (GDScript) — uses the same smoothed time as signals.
	[[nodiscard]] float get_current_beat() const;
	[[nodiscard]] int get_current_bar() const;
	[[nodiscard]] float get_beat_fraction() const;
	[[nodiscard]] float get_time_to_next_beat() const;
	[[nodiscard]] float get_time_to_next_bar() const;

	// Called each frame to detect beat crossings and emit signals
	void process(double p_delta);

	// S4.4: Music Moment Integration
	[[nodiscard]] float calculate_time_stretch_for_alignment(float p_target_time_sec, float p_tolerance_percent = 5.0f, float p_max_stretch = 0.1f) const;
	[[nodiscard]] float calculate_duration_stretch_for_alignment(float p_target_time_sec, float p_tolerance_percent = 5.0f, float p_max_stretch = 0.1f) const;

	BeatClock();
	~BeatClock();
};

#endif // BEAT_CLOCK_H
