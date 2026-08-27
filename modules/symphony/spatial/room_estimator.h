#ifndef ROOM_ESTIMATOR_H
#define ROOM_ESTIMATOR_H

#include "core/math/vector3.h"
#include "core/templates/vector.h"
#include "servers/physics_3d/direct_states/physics_direct_space_state_3d.h"

// Estimates room acoustics (volume, surface absorption, RT60) by casting a
// Fibonacci-sphere ray fan from a probe point and analyzing the hits.
//
// Physical model:
// - N rays uniformly cover the sphere; each subtends solid angle 4π/N.
// - Volume via sum-of-cones:   V = Σ (1/3) · dᵢ³ · (4π/N)
// - Surface via sum-of-patches: S = Σ dᵢ² · (4π/N)
// - Absorbed area:              Sα = Σ αᵢ · dᵢ² · (4π/N)
// - Sabine RT60:                RT60 = 0.161 · V / (Sα)
// - Eyring correction (high α): RT60 = 0.161 · V / (−S · ln(1 − ᾱ))
//
// Escaped rays (no hit within max_distance) contribute to openness and are
// treated as fully absorptive (open sky = infinite absorption).
class RoomEstimator {
public:
	struct Config {
		int ray_count = 32;             // Number of Fibonacci rays
		float max_distance = 100.0f;    // Ray length; beyond = escaped
		uint32_t collision_mask = 1;    // Physics mask
		bool ignore_floor = true;       // Skip downward rays (floor skews volume)
		float floor_angle_threshold = 45.0f; // Degrees below horizon to ignore
		float eyring_threshold = 0.3f;  // Mean absorption above which Eyring is used
		float fallback_absorption = 0.15f; // Absorption for hits with no AcousticMaterial
	};

	struct Result {
		float rt60 = 0.0f;            // Reverberation time (seconds)
		float volume = 0.0f;          // Estimated room volume (m³)
		float surface_area = 0.0f;    // Estimated total surface area (m²)
		float mean_absorption = 0.0f; // Area-weighted mean absorption coefficient
		float openness = 0.0f;        // Ratio of escaped rays [0=sealed, 1=open sky]
		float high_band_absorption = 0.0f; // Mean high-band absorption (for reverb damping)
		int rays_cast = 0;
		int rays_hit = 0;
	};

	// Generate `count` unit directions on a sphere via golden-angle spiral.
	// Ported from the reference addon (verified correct).
	static void generate_fibonacci_sphere(int p_count, Vector<Vector3> &r_directions);

	// Probe the room from a point. Casts the ray fan and computes RT60.
	static Result estimate(
			PhysicsDirectSpaceState3D *p_space,
			const Vector3 &p_probe_position,
			const Vector<RID> &p_exclude,
			const Config &p_config);

	// Compute reverb send level from openness.
	// Enclosed (openness=0) → high send; open (openness=1) → low send.
	static float openness_to_reverb_send(float p_openness);
};

#endif // ROOM_ESTIMATOR_H
