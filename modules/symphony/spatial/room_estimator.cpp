#include "room_estimator.h"
#include "acoustic_body_3d.h"
#include "acoustic_material.h"
#include "core/math/math_funcs.h"
#include "servers/physics_3d/physics_server_3d_types.h"

void RoomEstimator::generate_fibonacci_sphere(int p_count, Vector<Vector3> &r_directions) {
	r_directions.clear();
	if (p_count <= 0) {
		return;
	}
	r_directions.resize(p_count);

	const double golden_ratio = (1.0 + Math::sqrt(5.0)) / 2.0;

	for (int i = 0; i < p_count; i++) {
		// Polar angle — uniform in cos(θ) so points aren't bunched at poles.
		double theta = Math::acos(1.0 - 2.0 * ((double)i + 0.5) / (double)p_count);
		// Azimuthal angle — golden-angle increments for even spread.
		double phi = Math::TAU * (double)i / golden_ratio;

		r_directions.write[i] = Vector3(
				(float)(Math::sin(theta) * Math::cos(phi)),
				(float)Math::cos(theta),
				(float)(Math::sin(theta) * Math::sin(phi)));
	}
}

void room_estimator_material_absorption(const AcousticMaterial *p_mat, float p_fallback, float &r_mid, float &r_high) {
	if (p_mat == nullptr) {
		r_mid = p_fallback;
		r_high = p_fallback;
		return;
	}
	r_mid = p_mat->get_absorption_mid();
	r_high = p_mat->get_absorption_high();
	if (p_mat->get_total_absorption()) {
		r_mid = 1.0f;
		r_high = 1.0f;
	}
}

RoomEstimator::Result RoomEstimator::estimate(
		PhysicsDirectSpaceState3D *p_space,
		const Vector3 &p_probe_position,
		const Vector<RID> &p_exclude,
		const Config &p_config) {
	if (p_space == nullptr) {
		return Result();
	}

	// Physics-backed raycast functor: intersect_ray + AcousticBody3D material lookup.
	auto raycast = [&](const Vector3 &from, const Vector3 &to, Vector3 &r_pos, AcousticMaterial **r_mat) -> bool {
		PS3DT::RayParameters ray_params;
		ray_params.from = from;
		ray_params.to = to;
		ray_params.collision_mask = p_config.collision_mask;
		ray_params.collide_with_areas = false;
		ray_params.collide_with_bodies = true;
		for (int e = 0; e < p_exclude.size(); e++) {
			ray_params.exclude.insert(p_exclude[e]);
		}
		PS3DT::RayResult ray_result;
		if (!p_space->intersect_ray(ray_params, ray_result)) {
			return false;
		}
		r_pos = ray_result.position;
		*r_mat = AcousticBody3D::lookup_material(ray_result.collider_id);
		return true;
	};

	return compute(p_probe_position, p_config, raycast);
}

float RoomEstimator::openness_to_reverb_send(float p_openness) {
	// Enclosed (openness=0) → high send (1.0); open (openness=1) → low send (0.0).
	// Power curve so send drops off faster as openness increases (matches addon's wetness).
	float send = (1.0f - p_openness) * (1.0f - p_openness);
	return CLAMP(send, 0.0f, 1.0f);
}
