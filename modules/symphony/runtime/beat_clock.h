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
	float get_current_beat() const;
	int get_current_bar() const;
	float get_beat_fraction() const;
	float get_time_to_next_beat() const;
	float get_time_to_next_bar() const;

	// Called each frame to detect beat crossings and emit signals
	void process(double p_delta);

	BeatClock();
	~BeatClock();
};

#endif // BEAT_CLOCK_H
