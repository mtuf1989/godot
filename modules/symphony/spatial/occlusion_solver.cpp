#include "occlusion_solver.h"
#include "acoustic_body_3d.h"
#include "acoustic_material.h"
#include "core/math/math_funcs.h"
#include "servers/physics_3d/physics_server_3d_types.h"

OcclusionSolver::Result OcclusionSolver::solve(
		PhysicsDirectSpaceState3D *p_space,
		const Vector3 &p_source,
		const Vector3 &p_listener,
		const Vector<RID> &p_exclude,
		const Config &p_config) {
	Result result;

	if (p_space == nullptr) {
		return result; // No physics space — return unoccluded.
	}

	float total_distance = p_source.distance_to(p_listener);
	if (total_distance < 0.001f) {
		return result; // Source and listener at same point.
	}

	// Accumulated transmission per band (multiplicative).
	float accum_low = 1.0f;
	float accum_mid = 1.0f;
	float accum_high = 1.0f;

	// Ray march state — alternating direction (Steam Audio pattern).
	// Even hits: source → listener direction
	// Odd hits: listener → source direction
	Vector3 forward_pos = p_source;   // Current march position for source→listener
	Vector3 backward_pos = p_listener; // Current march position for listener→source

	int hit_count = 0;

	for (int step = 0; step < p_config.max_hits; step++) {
		// Alternate direction each step.
		bool forward = (step % 2 == 0);

		Vector3 from, to;
		if (forward) {
			from = forward_pos;
			to = p_listener;
		} else {
			from = backward_pos;
			to = p_source;
		}

		// Skip if from and to are too close (converged).
		if (from.distance_to(to) < p_config.ray_offset * 2.0f) {
			break;
		}

		// Set up ray query.
		PS3DT::RayParameters ray_params;
		ray_params.from = from;
		ray_params.to = to;
		ray_params.collision_mask = p_config.collision_mask;
		ray_params.collide_with_areas = false;
		ray_params.collide_with_bodies = true;

		// Exclude RIDs (listener body, etc.)
		for (int i = 0; i < p_exclude.size(); i++) {
			ray_params.exclude.insert(p_exclude[i]);
		}

		PS3DT::RayResult ray_result;
		bool hit = p_space->intersect_ray(ray_params, ray_result);

		if (!hit) {
			break; // Clear path in this direction — done.
		}

		// Verify the hit is between the endpoints (not behind us).
		float hit_dist_from_source = p_source.distance_to(ray_result.position);
		if (hit_dist_from_source >= total_distance) {
			break; // Hit is beyond the listener.
		}

		hit_count++;

		// Look up acoustic material from the collider registry.
		float t_low, t_mid, t_high;
		bool is_total_absorption = false;
		float ta_speed = 2.5f;

		AcousticMaterial *mat = AcousticBody3D::lookup_material(ray_result.collider_id);
		if (mat != nullptr) {
			if (mat->get_total_absorption()) {
				is_total_absorption = true;
				ta_speed = mat->get_total_absorption_transition_speed();
				t_low = 0.0f;
				t_mid = 0.0f;
				t_high = 0.0f;
			} else {
				t_low = mat->get_transmission_low();
				t_mid = mat->get_transmission_mid();
				t_high = mat->get_transmission_high();
			}
		} else {
			// No AcousticBody3D on this collider — use fallback values.
			t_low = p_config.fallback_transmission_low;
			t_mid = p_config.fallback_transmission_mid;
			t_high = p_config.fallback_transmission_high;
		}

		// Accumulate transmission.
		accum_low *= t_low;
		accum_mid *= t_mid;
		accum_high *= t_high;

		// Handle total absorption — immediate termination.
		if (is_total_absorption) {
			result.total_absorption_hit = true;
			result.total_absorption_speed = ta_speed;
			// Transmission is already 0 from multiplication above.
			break;
		}

		// Advance the march position past the hit.
		Vector3 advance_dir = (to - from).normalized();
		Vector3 new_pos = ray_result.position + advance_dir * p_config.ray_offset;

		if (forward) {
			forward_pos = new_pos;
		} else {
			backward_pos = new_pos;
		}
	}

	result.hit_count = hit_count;

	// Apply sqrt correction for double-counting wall entry/exit faces.
	// When a ray passes through a solid wall, it typically hits both the
	// entry face and exit face, counting one wall as two hits. Taking sqrt
	// of the accumulated product compensates for this.
	if (hit_count > 1 && !result.total_absorption_hit) {
		accum_low = Math::sqrt(accum_low);
		accum_mid = Math::sqrt(accum_mid);
		accum_high = Math::sqrt(accum_high);
	}

	// Clamp and store.
	result.transmission[0] = CLAMP(accum_low, 0.0f, 1.0f);
	result.transmission[1] = CLAMP(accum_mid, 0.0f, 1.0f);
	result.transmission[2] = CLAMP(accum_high, 0.0f, 1.0f);

	// Derive scalar occlusion from mean transmission (inverse).
	float mean_transmission = (result.transmission[0] + result.transmission[1] + result.transmission[2]) / 3.0f;
	result.occlusion = 1.0f - mean_transmission;

	return result;
}

// --- Volumetric occlusion (Task 12) -------------------------------------

void OcclusionSolver::generate_volume_samples(int p_count, Vector<Vector3> &r_offsets) {
	r_offsets.clear();
	if (p_count <= 0) {
		return;
	}
	r_offsets.resize(p_count);

	// Fibonacci spiral on the sphere surface for even angular coverage, with a
	// cube-root radial schedule so samples are uniformly distributed by VOLUME
	// (r ∝ (i/N)^(1/3)) rather than bunched near the centre. Deterministic —
	// no RNG — so per-frame estimates are stable.
	const double golden_ratio = (1.0 + Math::sqrt(5.0)) / 2.0;

	for (int i = 0; i < p_count; i++) {
		double t = ((double)i + 0.5) / (double)p_count;
		double theta = Math::acos(1.0 - 2.0 * t);        // polar, uniform in cos
		double phi = Math::TAU * (double)i / golden_ratio; // azimuth, golden angle
		// Radial position filling the volume evenly. Offset by a fractional
		// base so the very first sample isn't exactly at the centre.
		double radius = Math::pow(((double)i + 0.5) / (double)p_count, 1.0 / 3.0);

		Vector3 dir(
				(float)(Math::sin(theta) * Math::cos(phi)),
				(float)Math::cos(theta),
				(float)(Math::sin(theta) * Math::sin(phi)));
		r_offsets.write[i] = dir * (float)radius;
	}
}

OcclusionSolver::VolumetricResult OcclusionSolver::solve_volumetric(
		PhysicsDirectSpaceState3D *p_space,
		const Vector3 &p_source,
		float p_source_radius,
		const Vector3 &p_listener,
		const Vector<RID> &p_exclude,
		const VolumetricConfig &p_config) {
	if (p_space == nullptr) {
		return VolumetricResult(); // No physics space — fully audible.
	}

	// Physics-backed line-of-sight predicate.
	auto clear_los = [&](const Vector3 &a, const Vector3 &b) -> bool {
		if (a.distance_to(b) < 0.001f) {
			return true;
		}
		PS3DT::RayParameters ray_params;
		ray_params.from = a;
		ray_params.to = b;
		ray_params.collision_mask = p_config.collision_mask;
		ray_params.collide_with_areas = false;
		ray_params.collide_with_bodies = true;
		for (int i = 0; i < p_exclude.size(); i++) {
			ray_params.exclude.insert(p_exclude[i]);
		}
		PS3DT::RayResult ray_result;
		return !p_space->intersect_ray(ray_params, ray_result);
	};

	return compute_volumetric(
			p_source, p_source_radius, p_listener,
			p_config.sample_count, p_config.min_radius, clear_los);
}
