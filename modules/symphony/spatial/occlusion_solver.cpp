#include "occlusion_solver.h"
#include "acoustic_body_3d.h"
#include "acoustic_material.h"
#include "core/math/math_funcs.h"
#include "servers/physics_3d/physics_server_3d_types.h"

// --- Material accessor helpers (used by the header-only compute<>() template) ---

bool occlusion_material_is_total_absorption(const AcousticMaterial *p_mat, float &r_speed) {
	if (p_mat == nullptr) {
		return false;
	}
	if (p_mat->get_total_absorption()) {
		r_speed = p_mat->get_total_absorption_transition_speed();
		return true;
	}
	return false;
}

void occlusion_material_transmission(const AcousticMaterial *p_mat, float &r_low, float &r_mid, float &r_high) {
	if (p_mat == nullptr) {
		r_low = 1.0f;
		r_mid = 1.0f;
		r_high = 1.0f;
		return;
	}
	r_low = p_mat->get_transmission_low();
	r_mid = p_mat->get_transmission_mid();
	r_high = p_mat->get_transmission_high();
}

OcclusionSolver::Result OcclusionSolver::solve(
		PhysicsDirectSpaceState3D *p_space,
		const Vector3 &p_source,
		const Vector3 &p_listener,
		const Vector<RID> &p_exclude,
		const Config &p_config) {
	if (p_space == nullptr) {
		return Result(); // No physics space — return unoccluded.
	}

	// Physics-backed raycast functor: intersect_ray + AcousticBody3D material lookup.
	auto raycast = [&](const Vector3 &from, const Vector3 &to, Vector3 &r_pos, AcousticMaterial **r_mat) -> bool {
		PS3DT::RayParameters ray_params;
		ray_params.from = from;
		ray_params.to = to;
		ray_params.collision_mask = p_config.collision_mask;
		ray_params.collide_with_areas = false;
		ray_params.collide_with_bodies = true;
		for (int i = 0; i < p_exclude.size(); i++) {
			ray_params.exclude.insert(p_exclude[i]);
		}
		PS3DT::RayResult ray_result;
		if (!p_space->intersect_ray(ray_params, ray_result)) {
			return false;
		}
		r_pos = ray_result.position;
		*r_mat = AcousticBody3D::lookup_material(ray_result.collider_id);
		return true;
	};

	return compute(p_source, p_listener, p_config, raycast);
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
