#include "spatial_acoustics_engine.h"
#include "core/object/class_db.h"
#include "core/math/math_funcs.h"
#include "core/config/project_settings.h"
#include "servers/physics_3d/physics_server_3d.h"

#include "acoustic_portal_3d.h"
#include "acoustic_room_3d.h"
#include "../runtime/voice_manager.h"

SpatialAcousticsEngine *SpatialAcousticsEngine::singleton = nullptr;

SpatialAcousticsEngine::SpatialAcousticsEngine() {
	singleton = this;
	smooth_alpha = GLOBAL_DEF("audio/symphony/spatial_smooth_alpha", DEFAULT_SMOOTH_ALPHA);
	ProjectSettings::get_singleton()->set_custom_property_info(PropertyInfo(
			Variant::FLOAT, "audio/symphony/spatial_smooth_alpha", PROPERTY_HINT_RANGE, "0.01,0.99,0.01"));

	// Reverb pool slot count — platform defaults per plan (Task 10):
	// 8 desktop / 4 mobile / 2 web. Configurable via project setting.
#if defined(WEB_ENABLED)
	const int default_reverb_slots = 2;
#elif defined(ANDROID_ENABLED) || defined(IOS_ENABLED)
	const int default_reverb_slots = 4;
#else
	const int default_reverb_slots = 8;
#endif
	int reverb_slots = GLOBAL_DEF("audio/symphony/reverb_pool_slots", default_reverb_slots);
	ProjectSettings::get_singleton()->set_custom_property_info(PropertyInfo(
			Variant::INT, "audio/symphony/reverb_pool_slots", PROPERTY_HINT_RANGE, "1,8,1"));

	ReverbPool::Config rp_cfg;
	rp_cfg.active_slots = reverb_slots;
	reverb_pool.init(rp_cfg);

	// Air absorption artistic scale knob (Phase 4.1). Physical default 1.0; the
	// ISO 9613-1 fit is distance-absolute, so document this prominently or a
	// tightened value will read as "too dark".
	air_absorption_scale = GLOBAL_DEF("audio/symphony/air_absorption_scale", 1.0f);
	ProjectSettings::get_singleton()->set_custom_property_info(PropertyInfo(
			Variant::FLOAT, "audio/symphony/air_absorption_scale", PROPERTY_HINT_RANGE, "0.0,10.0,0.1"));
}

SpatialAcousticsEngine::~SpatialAcousticsEngine() {
	singleton = nullptr;
}

int SpatialAcousticsEngine::_find_free_emitter() const {
	for (int i = 0; i < MAX_EMITTERS; i++) {
		if (!emitters[i].active) {
			return i;
		}
	}
	return -1;
}

// --- Emitter lifecycle ---

int SpatialAcousticsEngine::register_emitter(int p_voice_slot) {
	if (slot_to_emitter.has(p_voice_slot)) {
		return slot_to_emitter[p_voice_slot]; // Already registered.
	}

	int idx = _find_free_emitter();
	if (idx < 0) {
		ERR_PRINT_ONCE("SpatialAcousticsEngine: MAX_EMITTERS reached, cannot register.");
		return -1;
	}

	emitters[idx].active = true;
	emitters[idx].first_update = true;
	emitters[idx].voice_slot = p_voice_slot;
	emitters[idx].source_radius = 0.0f;
	emitters[idx].max_distance = 0.0f;
	emitters[idx].target = SpatialParams();
	emitters[idx].smoothed = SpatialParams();

	slot_to_emitter[p_voice_slot] = idx;
	return idx;
}

void SpatialAcousticsEngine::unregister_emitter(int p_voice_slot) {
	int *idx_ptr = slot_to_emitter.getptr(p_voice_slot);
	if (!idx_ptr) {
		return;
	}
	int idx = *idx_ptr;

	emitters[idx].active = false;
	emitters[idx].first_update = true;
	emitters[idx].voice_slot = -1;

	reverb_pool.release(p_voice_slot);
	slot_to_emitter.erase(p_voice_slot);
}

bool SpatialAcousticsEngine::has_emitter(int p_voice_slot) const {
	return slot_to_emitter.has(p_voice_slot);
}

// --- Per-frame update ---

void SpatialAcousticsEngine::update(float p_delta) {
	// Get physics space for occlusion / room-estimation raycasts. Phase 3.2:
	// room estimation must run even when occlusion is disabled, so fetch the
	// space whenever either subsystem needs it.
	PhysicsDirectSpaceState3D *space = nullptr;
	if ((occlusion_enabled || room_estimation_enabled) && physics_space_rid.is_valid()) {
		PhysicsServer3D *ps = PhysicsServer3D::get_singleton();
		if (ps) {
			space = ps->space_get_direct_state(physics_space_rid);
		}
	}

	// Advance probe cache time.
	probe_cache.advance_time(p_delta);
	probe_cache.reset_metrics();

	// Build emitter info array for the scheduler.
	ProbeScheduler::EmitterInfo emitter_infos[MAX_EMITTERS];
	int info_count = 0;

	// Phase 2.3: pull live emitter state from the VoicePool once per frame, so
	// moving sources re-solve + re-pan, recycled slots never carry a stale
	// position, and the scheduler sees real audibility/importance. set_emitter_*
	// setters remain as overrides for non-pooled callers.
	SymphonyVoicePool *pool = SymphonyVoicePool::get_singleton();

	for (int i = 0; i < MAX_EMITTERS; i++) {
		if (!emitters[i].active) {
			continue;
		}

		bool audible = true;
		float importance = 1.0f;
		// Pull live state from the pool only when the slot is genuinely an active
		// pooled voice. A FREE pool slot means this emitter was driven by a
		// non-pooled caller via set_emitter_position() (e.g. tests, or a custom
		// integration) — in that case we must NOT clobber its position with the
		// pool's stale (0,0,0). set_emitter_position stays the override path.
		if (pool != nullptr && emitters[i].voice_slot >= 0 && emitters[i].voice_slot < pool->get_pool_size() &&
				pool->get_slot_state(emitters[i].voice_slot) != SymphonyVoicePool::VOICE_FREE) {
			const int vs = emitters[i].voice_slot;
			emitters[i].source_position = pool->get_slot_position(vs);
			emitters[i].target.attenuation = pool->get_slot_attenuation(vs);
			const float pool_max = pool->get_slot_max_distance(vs);
			if (pool_max > 0.0f) {
				emitters[i].max_distance = pool_max;
			}
			audible = pool->is_slot_audible(vs);
			importance = pool->get_slot_importance(vs);
		}

		// Accumulate time since last occlusion solve.
		emitters[i].last_update_time += p_delta;

		ProbeScheduler::EmitterInfo &info = emitter_infos[info_count];
		info.emitter_index = i;
		info.distance_sq = emitters[i].source_position.distance_squared_to(listener_position);
		info.importance = importance;
		info.audible = audible;
		info.last_update_time = emitters[i].last_update_time;
		info_count++;
	}

	// Run the scheduler whenever a physics space is available; gate each solver
	// independently so room estimation runs even when occlusion is off (3.2).
	if (space) {
		scheduler.schedule(emitter_infos, info_count, p_delta, scheduled_emitters);

		// Budget-scale the volumetric sample count: split the remaining ray
		// budget across the scheduled emitters (each volumetric sample costs up
		// to 2 rays), clamped to [2, configured max]. This keeps volumetric
		// occlusion within the same per-frame ray ceiling as the direct solves.
		int scheduled = MAX(scheduled_emitters.size(), 1);
		int rays_left = MAX(scheduler.get_ray_budget() - scheduler.get_metrics().rays_issued, 0);
		int per_emitter_rays = rays_left / scheduled;
		int budget_samples = per_emitter_rays / 2; // 2 rays per sample
		_volumetric_samples_this_frame = CLAMP(budget_samples, 2, volumetric_config.sample_count);

		for (int k = 0; k < scheduled_emitters.size(); k++) {
			int emitter_idx = scheduled_emitters[k];
			if (occlusion_enabled) {
				_solve_occlusion_for_emitter(emitter_idx, space);
			}
			if (room_estimation_enabled) {
				_solve_room_for_emitter(emitter_idx, space);
			}
			emitters[emitter_idx].last_update_time = 0.0f; // Reset timer after solve.
		}
	}

	// Portal propagation (Task 15): resolve room-to-room paths and fold apparent
	// position, per-hop attenuation, and diffraction LPF into each emitter's
	// target. Runs before smoothing so the results are smoothed like everything
	// else. Cheap when there are no rooms/portals authored.
	if (portal_propagation_enabled) {
		_rebuild_portal_graph_if_needed();
		if (portal_graph.node_count > 0) {
			for (int i = 0; i < MAX_EMITTERS; i++) {
				if (emitters[i].active) {
					_solve_portals_for_emitter(i);
				}
			}
		}
	}

	// Smoothing and publish run for ALL active emitters every frame (cheap).
	for (int i = 0; i < MAX_EMITTERS; i++) {
		if (!emitters[i].active) {
			continue;
		}
		_smooth_params(i, p_delta);
		_publish_params(i);
	}

	// Reverb pool: cluster emitters by smoothed RT60 and advance crossfades.
	if (reverb_pool_enabled) {
		_update_reverb_pool(p_delta);
	}
}

// --- Reverb pool (Task 10) ---

void SpatialAcousticsEngine::_update_reverb_pool(float p_delta) {
	reverb_pool.reset_migration_metrics();

	// (Re)assign every active emitter from its smoothed room parameters.
	// assign() is idempotent for a stable assignment; it only starts a
	// crossfade when the target slot actually changes.
	for (int i = 0; i < MAX_EMITTERS; i++) {
		if (!emitters[i].active) {
			continue;
		}
		const SpatialParams &sp = emitters[i].smoothed;
		reverb_pool.assign(emitters[i].voice_slot, sp.rt60, sp.damping, sp.reverb_send, sp.room_volume);
	}

	// Advance crossfades and recompute per-slot aggregated FDN parameters.
	reverb_pool.update(p_delta);
}

// --- Portal propagation (Task 15) ---

void SpatialAcousticsEngine::_rebuild_portal_graph_if_needed() {
	// Combine the room + portal epochs into one signature. Any add/remove/move/
	// open/close bumps one of them, triggering a rebuild + path-cache flush.
	const uint64_t epoch = AcousticRoom3D::get_registry_epoch() ^ (AcousticPortal3D::get_state_epoch() * 1099511628211ULL);
	if (epoch == portal_graph_epoch && portal_graph.node_count == AcousticRoom3D::get_room_count()) {
		return;
	}
	portal_graph_epoch = epoch;

	const int room_count = AcousticRoom3D::get_room_count();
	portal_graph.set_node_count(room_count);
	_room_ptr_to_node.clear();
	for (int i = 0; i < room_count; i++) {
		AcousticRoom3D *room = AcousticRoom3D::get_room(i);
		if (room) {
			_room_ptr_to_node[room->get_instance_id()] = i;
		}
	}

	// Add an edge per portal that connects two registered rooms. Weight is the
	// straight-line distance between the two room centres through the portal
	// (source→portal + portal→dest is unknown here, so use portal-centred
	// half-distances as a stable proxy). Distance is refined per-emitter later.
	const int portal_count = AcousticPortal3D::get_portal_count();
	for (int i = 0; i < portal_count; i++) {
		AcousticPortal3D *portal = AcousticPortal3D::get_portal(i);
		if (portal == nullptr) {
			continue;
		}
		AcousticRoom3D *ra = portal->get_room_a();
		AcousticRoom3D *rb = portal->get_room_b();
		if (ra == nullptr || rb == nullptr) {
			continue;
		}
		int *na = _room_ptr_to_node.getptr(ra->get_instance_id());
		int *nb = _room_ptr_to_node.getptr(rb->get_instance_id());
		if (na == nullptr || nb == nullptr) {
			continue;
		}
		const Vector3 center = portal->get_world_center();
		const float weight = ra->get_global_transform().origin.distance_to(center) +
				center.distance_to(rb->get_global_transform().origin);
		portal_graph.add_edge(*na, *nb, MAX(weight, 0.01f), portal->is_open(), center, i);
	}

	// Portal state change invalidates cached paths.
	portal_path_cache.check_epoch(epoch);
}

void SpatialAcousticsEngine::_solve_portals_for_emitter(int p_emitter_idx) {
	EmitterState &e = emitters[p_emitter_idx];

	// Reset portal outputs to "no effect" each frame BEFORE solving, so a lost
	// path (moved into the same room, door closed, etc.) cleanly reverts and the
	// gain never compounds across frames.
	e.target.portal_gain = 1.0f;
	e.target.apparent_position = e.source_position;

	// Resolve which rooms the source and listener occupy.
	AcousticRoom3D *src_room = AcousticRoom3D::find_room_for_point(e.source_position);
	AcousticRoom3D *lis_room = AcousticRoom3D::find_room_for_point(listener_position);

	// No room info, or same room → no portal effect.
	if (src_room == nullptr || lis_room == nullptr || src_room == lis_room) {
		return;
	}

	int *src_node = _room_ptr_to_node.getptr(src_room->get_instance_id());
	int *lis_node = _room_ptr_to_node.getptr(lis_room->get_instance_id());
	if (src_node == nullptr || lis_node == nullptr) {
		return;
	}

	// Solve (or fetch cached) the room-to-room path.
	PortalPath path;
	if (!portal_path_cache.get(*src_node, *lis_node, path)) {
		path = portal_solver.solve(portal_graph, *src_node, *lis_node);
		portal_path_cache.put(*src_node, *lis_node, path);
	}

	// Unreachable (all doors closed) or same-room short-circuit → keep true
	// position + unity gain; the occlusion/transmission path handles any leak.
	if (!path.reachable || path.edge_ids.is_empty()) {
		return;
	}

	// Build the ordered hop geometry from the path's portal edges.
	LocalVector<PortalHop> hops;
	for (uint32_t h = 0; h < path.edge_ids.size(); h++) {
		const PortalEdge &edge = portal_graph.edges[path.edge_ids[h]];
		AcousticPortal3D *portal = AcousticPortal3D::get_portal(edge.portal_id);
		if (portal == nullptr) {
			continue;
		}
		PortalHop hop;
		hop.center = portal->get_world_center();
		hop.normal = portal->get_world_normal();
		hop.aperture_area = portal->get_aperture_area();
		hops.push_back(hop);
	}
	if (hops.is_empty()) {
		return;
	}

	// Apparent position → the portal nearest the listener (Godot's panner then
	// points at the doorway, not through the wall).
	e.target.apparent_position = PortalRouter::apparent_position(e.source_position, listener_position, hops);

	// Per-hop aperture + incidence attenuation. Published as its OWN gain layer
	// (portal_gain) — NOT folded into `attenuation`, which VoicePool owns/mirrors.
	// The GDScript layer applies this multiplicatively so there is no double count.
	e.target.portal_gain = PortalRouter::path_gain(e.source_position, listener_position, hops);

	// Diffraction LPF: stack by MINIMUM frequency with the base air-absorption
	// cutoff (never multiply — plan invariant). Phase 3.1: ASSIGN from the
	// intermediate air_cutoff_base rather than folding MIN into the previous
	// frame's target.air_cutoff, so the cutoff can recover when the path opens
	// up (no monotonic ratchet).
	const float diff_cut = PortalRouter::diffraction_cutoff(e.source_position, listener_position, hops, 20000.0f, portal_diffraction_min_cutoff);
	e.target.air_cutoff = MIN(e.air_cutoff_base, diff_cut);
}

// --- Occlusion ---

void SpatialAcousticsEngine::_solve_occlusion_for_emitter(int p_emitter_idx, PhysicsDirectSpaceState3D *p_space) {
	EmitterState &e = emitters[p_emitter_idx];

	OcclusionSolver::Result result = OcclusionSolver::solve(
			p_space,
			e.source_position,
			listener_position,
			exclude_rids,
			occlusion_config);

	// Spectral transmission (per band) always comes from the direct-path solve.
	// Keep the RAW material value for the debug overlay; the EFFECTIVE
	// transmission[] (which drives the occlusion LPF + spatial gain) may be
	// blended with volumetric occlusion below.
	e.target.material_transmission[0] = result.transmission[0];
	e.target.material_transmission[1] = result.transmission[1];
	e.target.material_transmission[2] = result.transmission[2];
	e.target.transmission[0] = result.transmission[0];
	e.target.transmission[1] = result.transmission[1];
	e.target.transmission[2] = result.transmission[2];

	// Overall occlusion scalar: use graduated volumetric occlusion for finite
	// sized sources (Task 12), otherwise the binary direct-path occlusion.
	if (volumetric_occlusion_enabled && e.source_radius >= volumetric_config.min_radius) {
		OcclusionSolver::VolumetricConfig vcfg = volumetric_config;
		vcfg.collision_mask = occlusion_config.collision_mask;
		vcfg.sample_count = _volumetric_samples_this_frame;
		OcclusionSolver::VolumetricResult vres = OcclusionSolver::solve_volumetric(
				p_space,
				e.source_position,
				e.source_radius,
				listener_position,
				exclude_rids,
				vcfg);

		// Blend the volumetric occlusion fraction INTO the transmission bands so
		// it drives level *and* timbre (Steam Audio model). occ=0 → fully the
		// material value; occ=1 → fully blocked. Only on the volumetric branch:
		// a point source's result.occlusion is already 1 - mean_transmission, so
		// blending it back would double-apply.
		const float occ = vres.occlusion;
		for (int b = 0; b < 3; b++) {
			e.target.transmission[b] = (1.0f - occ) + occ * result.transmission[b];
		}
		e.target.occlusion = occ;
	} else {
		e.target.occlusion = result.occlusion;
	}

	// Air absorption: drive the air-absorption LPF cutoff from the
	// source→listener distance (Task 12 wiring). Farther sources lose highs.
	// Phase 3.1: write the intermediate air_cutoff_base (never mutate the
	// previous frame's target.air_cutoff — that ratchets). The portal solve
	// assigns target.air_cutoff = MIN(base, diffraction) fresh each frame; here
	// we also seed target.air_cutoff so the no-portal path has a value.
	if (air_absorption_enabled) {
		float distance = e.source_position.distance_to(listener_position);
		e.air_cutoff_base = SpatialGraphWrapper::distance_to_air_cutoff(distance, air_absorption_scale);
	} else {
		e.air_cutoff_base = 20000.0f;
	}
	e.target.air_cutoff = e.air_cutoff_base;
}

void SpatialAcousticsEngine::_solve_room_for_emitter(int p_emitter_idx, PhysicsDirectSpaceState3D *p_space) {
	EmitterState &e = emitters[p_emitter_idx];

	// Check the probe cache first — emitters in the same cell share room data.
	ProbeCache::RoomProbeResult cached;
	if (probe_cache.lookup(e.source_position, cached)) {
		e.target.rt60 = cached.rt60;
		e.target.reverb_send = cached.reverb_send;
		e.target.damping = cached.high_band_absorption;
		e.target.room_volume = cached.volume;
		return;
	}

	// Cache miss — cast the Fibonacci ray fan.
	RoomEstimator::Result room = RoomEstimator::estimate(
			p_space,
			e.source_position,
			exclude_rids,
			room_config);

	e.target.rt60 = room.rt60;
	e.target.reverb_send = RoomEstimator::openness_to_reverb_send(room.openness);
	e.target.damping = room.high_band_absorption;
	e.target.room_volume = room.volume;

	// Store in cache for nearby emitters to reuse.
	ProbeCache::RoomProbeResult to_cache;
	to_cache.rt60 = room.rt60;
	to_cache.volume = room.volume;
	to_cache.mean_absorption = room.mean_absorption;
	to_cache.high_band_absorption = room.high_band_absorption;
	to_cache.openness = room.openness;
	to_cache.reverb_send = e.target.reverb_send;
	probe_cache.store(e.source_position, to_cache);
}

void SpatialAcousticsEngine::set_emitter_position(int p_voice_slot, const Vector3 &p_position) {
	int *idx_ptr = slot_to_emitter.getptr(p_voice_slot);
	if (!idx_ptr) return;
	emitters[*idx_ptr].source_position = p_position;
	// Also update apparent position target (until portal redirect changes it).
	emitters[*idx_ptr].target.apparent_position = p_position;
}

void SpatialAcousticsEngine::set_emitter_source_radius(int p_voice_slot, float p_radius) {
	int *idx_ptr = slot_to_emitter.getptr(p_voice_slot);
	if (!idx_ptr) return;
	emitters[*idx_ptr].source_radius = MAX(p_radius, 0.0f);
}

float SpatialAcousticsEngine::get_emitter_source_radius(int p_voice_slot) const {
	const int *idx_ptr = slot_to_emitter.getptr(p_voice_slot);
	if (!idx_ptr) return 0.0f;
	return emitters[*idx_ptr].source_radius;
}

void SpatialAcousticsEngine::set_emitter_max_distance(int p_voice_slot, float p_max_distance) {
	int *idx_ptr = slot_to_emitter.getptr(p_voice_slot);
	if (!idx_ptr) return;
	emitters[*idx_ptr].max_distance = MAX(p_max_distance, 0.0f);
}

float SpatialAcousticsEngine::get_emitter_max_distance(int p_voice_slot) const {
	const int *idx_ptr = slot_to_emitter.getptr(p_voice_slot);
	if (!idx_ptr) return 0.0f;
	return emitters[*idx_ptr].max_distance;
}

void SpatialAcousticsEngine::set_listener_body_rid(const RID &p_rid) {
	exclude_rids.clear();
	if (p_rid.is_valid()) {
		exclude_rids.push_back(p_rid);
	}
}

void SpatialAcousticsEngine::clear_listener_body_rid() {
	exclude_rids.clear();
}

// --- IIR Smoothing ---
// First-order exponential smoothing: smoothed = alpha * target + (1-alpha) * smoothed
// On first update, snap immediately (UE5 pattern — no fade-in from unoccluded).

void SpatialAcousticsEngine::_smooth_params(int p_emitter_idx, float p_delta) {
	EmitterState &e = emitters[p_emitter_idx];

	if (e.first_update) {
		// Snap: copy target directly to smoothed, no interpolation.
		e.smoothed = e.target;
		e.first_update = false;
		return;
	}

	// IIR smooth: smoothed += a * (target - smoothed), with a frame-rate
	// independent coefficient (Phase 3.5): effective_a = 1 - (1-alpha)^(dt*60),
	// so the settle time is the same at 30/60/144 fps. smooth_alpha is defined
	// as the per-frame coefficient at 60 fps.
	const float a = 1.0f - Math::pow(1.0f - smooth_alpha, p_delta * 60.0f);
	const float b = 1.0f - a;

	e.smoothed.occlusion = a * e.target.occlusion + b * e.smoothed.occlusion;
	e.smoothed.transmission[0] = a * e.target.transmission[0] + b * e.smoothed.transmission[0];
	e.smoothed.transmission[1] = a * e.target.transmission[1] + b * e.smoothed.transmission[1];
	e.smoothed.transmission[2] = a * e.target.transmission[2] + b * e.smoothed.transmission[2];
	e.smoothed.material_transmission[0] = a * e.target.material_transmission[0] + b * e.smoothed.material_transmission[0];
	e.smoothed.material_transmission[1] = a * e.target.material_transmission[1] + b * e.smoothed.material_transmission[1];
	e.smoothed.material_transmission[2] = a * e.target.material_transmission[2] + b * e.smoothed.material_transmission[2];
	// Phase 4.2: smooth air_cutoff in the LOG domain so a filter sweep is
	// perceptually even (equal ratios per step) rather than bunched at the top.
	{
		const float tgt = MAX(e.target.air_cutoff, 1.0f);
		const float cur = MAX(e.smoothed.air_cutoff, 1.0f);
		e.smoothed.air_cutoff = Math::exp(a * Math::log(tgt) + b * Math::log(cur));
	}
	e.smoothed.reverb_send = a * e.target.reverb_send + b * e.smoothed.reverb_send;
	e.smoothed.rt60 = a * e.target.rt60 + b * e.smoothed.rt60;
	e.smoothed.room_volume = a * e.target.room_volume + b * e.smoothed.room_volume;
	e.smoothed.damping = a * e.target.damping + b * e.smoothed.damping;
	e.smoothed.delay_s = a * e.target.delay_s + b * e.smoothed.delay_s;
	e.smoothed.portal_gain = a * e.target.portal_gain + b * e.smoothed.portal_gain;
	e.smoothed.apparent_position = e.target.apparent_position.lerp(e.smoothed.apparent_position, b);

	// Attenuation is not smoothed here — it's driven by VoicePool's own system.
	e.smoothed.attenuation = e.target.attenuation;
}

void SpatialAcousticsEngine::_publish_params(int p_emitter_idx) {
	emitters[p_emitter_idx].published.store(emitters[p_emitter_idx].smoothed);
}

// --- Target setters ---

void SpatialAcousticsEngine::set_emitter_occlusion(int p_voice_slot, float p_occlusion) {
	int *idx_ptr = slot_to_emitter.getptr(p_voice_slot);
	if (!idx_ptr) return;
	emitters[*idx_ptr].target.occlusion = CLAMP(p_occlusion, 0.0f, 1.0f);
}

void SpatialAcousticsEngine::set_emitter_transmission(int p_voice_slot, float p_low, float p_mid, float p_high) {
	int *idx_ptr = slot_to_emitter.getptr(p_voice_slot);
	if (!idx_ptr) return;
	emitters[*idx_ptr].target.transmission[0] = CLAMP(p_low, 0.0f, 1.0f);
	emitters[*idx_ptr].target.transmission[1] = CLAMP(p_mid, 0.0f, 1.0f);
	emitters[*idx_ptr].target.transmission[2] = CLAMP(p_high, 0.0f, 1.0f);
}

void SpatialAcousticsEngine::set_emitter_air_cutoff(int p_voice_slot, float p_cutoff_hz) {
	int *idx_ptr = slot_to_emitter.getptr(p_voice_slot);
	if (!idx_ptr) return;
	emitters[*idx_ptr].target.air_cutoff = MAX(p_cutoff_hz, 20.0f);
}

void SpatialAcousticsEngine::set_emitter_reverb_send(int p_voice_slot, float p_send) {
	int *idx_ptr = slot_to_emitter.getptr(p_voice_slot);
	if (!idx_ptr) return;
	emitters[*idx_ptr].target.reverb_send = CLAMP(p_send, 0.0f, 1.0f);
}

void SpatialAcousticsEngine::set_emitter_rt60(int p_voice_slot, float p_rt60) {
	int *idx_ptr = slot_to_emitter.getptr(p_voice_slot);
	if (!idx_ptr) return;
	emitters[*idx_ptr].target.rt60 = MAX(p_rt60, 0.0f);
}

void SpatialAcousticsEngine::set_emitter_delay(int p_voice_slot, float p_delay_s) {
	int *idx_ptr = slot_to_emitter.getptr(p_voice_slot);
	if (!idx_ptr) return;
	emitters[*idx_ptr].target.delay_s = MAX(p_delay_s, 0.0f);
}

void SpatialAcousticsEngine::set_emitter_apparent_position(int p_voice_slot, const Vector3 &p_position) {
	int *idx_ptr = slot_to_emitter.getptr(p_voice_slot);
	if (!idx_ptr) return;
	emitters[*idx_ptr].target.apparent_position = p_position;
}

// --- Reader access ---

SpatialParams SpatialAcousticsEngine::read_params(int p_voice_slot) const {
	const int *idx_ptr = slot_to_emitter.getptr(p_voice_slot);
	if (!idx_ptr) {
		return SpatialParams(); // Default (unoccluded, full transmission)
	}
	return emitters[*idx_ptr].published.load();
}

bool SpatialAcousticsEngine::try_read_params(int p_voice_slot, SpatialParams &r_params) const {
	const int *idx_ptr = slot_to_emitter.getptr(p_voice_slot);
	if (!idx_ptr) {
		return false;
	}
	return emitters[*idx_ptr].published.try_load(r_params);
}

// --- Configuration ---

void SpatialAcousticsEngine::set_smooth_alpha(float p_alpha) {
	smooth_alpha = CLAMP(p_alpha, 0.01f, 0.99f);
}

// --- Debug ---

int SpatialAcousticsEngine::get_active_emitter_count() const {
	int count = 0;
	for (int i = 0; i < MAX_EMITTERS; i++) {
		if (emitters[i].active) {
			count++;
		}
	}
	return count;
}

// --- Graph wrapper ---

Ref<AudioStreamSymphony> SpatialAcousticsEngine::wrap_stream(const Ref<AudioStream> &p_stream, bool p_loop) {
	return SpatialGraphWrapper::create_spatial_stream(p_stream, p_loop);
}

bool SpatialAcousticsEngine::stream_needs_wrapping(const Ref<AudioStream> &p_stream) const {
	return SpatialGraphWrapper::needs_wrapping(p_stream);
}

float SpatialAcousticsEngine::compute_occlusion_cutoff(int p_voice_slot) const {
	const int *idx_ptr = slot_to_emitter.getptr(p_voice_slot);
	if (!idx_ptr) {
		return 20000.0f;
	}
	const SpatialParams &sp = emitters[*idx_ptr].smoothed;
	return SpatialGraphWrapper::transmission_to_cutoff(sp.transmission[1], sp.transmission[2]);
}

float SpatialAcousticsEngine::compute_air_cutoff(int p_voice_slot) const {
	const int *idx_ptr = slot_to_emitter.getptr(p_voice_slot);
	if (!idx_ptr) {
		return 20000.0f;
	}
	return emitters[*idx_ptr].smoothed.air_cutoff;
}

float SpatialAcousticsEngine::compute_spatial_gain(int p_voice_slot) const {
	const int *idx_ptr = slot_to_emitter.getptr(p_voice_slot);
	if (!idx_ptr) {
		return 1.0f;
	}
	const SpatialParams &sp = emitters[*idx_ptr].smoothed;
	// Gain derived from mean transmission (keeps in sync with occlusion).
	// Fully occluded (transmission=0) → gain 0; fully open → gain 1.
	return (sp.transmission[0] + sp.transmission[1] + sp.transmission[2]) / 3.0f;
}

// --- Portal routing accessors (Task 15) ---

Vector3 SpatialAcousticsEngine::get_emitter_apparent_position(int p_voice_slot) const {
	const int *idx_ptr = slot_to_emitter.getptr(p_voice_slot);
	if (!idx_ptr) {
		return Vector3();
	}
	// Portal-redirected position (equals the true source when no portal path
	// applies). GDScript sets the AudioStreamPlayer3D global_position to this so
	// the panner points at the doorway rather than through the wall.
	return emitters[*idx_ptr].smoothed.apparent_position;
}

float SpatialAcousticsEngine::get_emitter_portal_gain(int p_voice_slot) const {
	const int *idx_ptr = slot_to_emitter.getptr(p_voice_slot);
	if (!idx_ptr) {
		return 1.0f;
	}
	// Per-hop aperture/incidence gain [0,1]. Its own layer — the GDScript layer
	// applies it multiplicatively on top of VoicePool attenuation (no double count).
	return emitters[*idx_ptr].smoothed.portal_gain;
}

// --- Reverb pool accessors (Task 10) ---

void SpatialAcousticsEngine::set_reverb_pool_slots(int p_slots) {
	ReverbPool::Config cfg = reverb_pool.get_config();
	cfg.active_slots = CLAMP(p_slots, 1, ReverbPool::MAX_SLOTS);
	reverb_pool.init(cfg);
}

float SpatialAcousticsEngine::get_reverb_slot_decay_time(int p_slot) const {
	return reverb_pool.slot_params(p_slot).decay_time;
}

float SpatialAcousticsEngine::get_reverb_slot_damping(int p_slot) const {
	return reverb_pool.slot_params(p_slot).damping;
}

float SpatialAcousticsEngine::get_reverb_slot_room_size(int p_slot) const {
	return reverb_pool.slot_params(p_slot).room_size;
}

float SpatialAcousticsEngine::get_reverb_slot_wet_gain(int p_slot) const {
	return reverb_pool.slot_params(p_slot).wet_gain;
}

int SpatialAcousticsEngine::get_emitter_reverb_slot(int p_voice_slot) const {
	int slot = -1;
	float send = 0.0f;
	if (reverb_pool.emitter_slot(p_voice_slot, slot, send)) {
		return slot;
	}
	return -1;
}

float SpatialAcousticsEngine::get_emitter_reverb_send(int p_voice_slot) const {
	int slot = -1;
	float send = 0.0f;
	if (reverb_pool.emitter_slot(p_voice_slot, slot, send)) {
		return send;
	}
	return 0.0f;
}

int SpatialAcousticsEngine::get_emitter_reverb_prev_slot(int p_voice_slot) const {
	int slot = -1;
	float send = 0.0f;
	if (reverb_pool.emitter_prev_slot(p_voice_slot, slot, send)) {
		return slot;
	}
	return -1;
}

float SpatialAcousticsEngine::get_emitter_reverb_prev_send(int p_voice_slot) const {
	int slot = -1;
	float send = 0.0f;
	if (reverb_pool.emitter_prev_slot(p_voice_slot, slot, send)) {
		return send;
	}
	return 0.0f;
}

bool SpatialAcousticsEngine::is_emitter_reverb_migrating(int p_voice_slot) const {
	return reverb_pool.is_migrating(p_voice_slot);
}

// --- Bindings ---

void SpatialAcousticsEngine::_bind_methods() {
	ClassDB::bind_method(D_METHOD("register_emitter", "voice_slot"), &SpatialAcousticsEngine::register_emitter);
	ClassDB::bind_method(D_METHOD("unregister_emitter", "voice_slot"), &SpatialAcousticsEngine::unregister_emitter);
	ClassDB::bind_method(D_METHOD("has_emitter", "voice_slot"), &SpatialAcousticsEngine::has_emitter);

	ClassDB::bind_method(D_METHOD("update", "delta"), &SpatialAcousticsEngine::update);

	ClassDB::bind_method(D_METHOD("set_emitter_occlusion", "voice_slot", "occlusion"), &SpatialAcousticsEngine::set_emitter_occlusion);
	ClassDB::bind_method(D_METHOD("set_emitter_transmission", "voice_slot", "low", "mid", "high"), &SpatialAcousticsEngine::set_emitter_transmission);
	ClassDB::bind_method(D_METHOD("set_emitter_air_cutoff", "voice_slot", "cutoff_hz"), &SpatialAcousticsEngine::set_emitter_air_cutoff);
	ClassDB::bind_method(D_METHOD("set_emitter_reverb_send", "voice_slot", "send"), &SpatialAcousticsEngine::set_emitter_reverb_send);
	ClassDB::bind_method(D_METHOD("set_emitter_rt60", "voice_slot", "rt60"), &SpatialAcousticsEngine::set_emitter_rt60);
	ClassDB::bind_method(D_METHOD("set_emitter_delay", "voice_slot", "delay_s"), &SpatialAcousticsEngine::set_emitter_delay);
	ClassDB::bind_method(D_METHOD("set_emitter_apparent_position", "voice_slot", "position"), &SpatialAcousticsEngine::set_emitter_apparent_position);

	ClassDB::bind_method(D_METHOD("set_smooth_alpha", "alpha"), &SpatialAcousticsEngine::set_smooth_alpha);
	ClassDB::bind_method(D_METHOD("get_smooth_alpha"), &SpatialAcousticsEngine::get_smooth_alpha);

	ClassDB::bind_method(D_METHOD("set_listener_position", "position"), &SpatialAcousticsEngine::set_listener_position);
	ClassDB::bind_method(D_METHOD("get_listener_position"), &SpatialAcousticsEngine::get_listener_position);

	ClassDB::bind_method(D_METHOD("get_active_emitter_count"), &SpatialAcousticsEngine::get_active_emitter_count);
	ClassDB::bind_method(D_METHOD("get_max_emitters"), &SpatialAcousticsEngine::get_max_emitters);

	// Scheduler
	ClassDB::bind_method(D_METHOD("set_ray_budget", "budget"), &SpatialAcousticsEngine::set_ray_budget);
	ClassDB::bind_method(D_METHOD("get_ray_budget"), &SpatialAcousticsEngine::get_ray_budget);
	ClassDB::bind_method(D_METHOD("set_scheduler_base_rate", "hz"), &SpatialAcousticsEngine::set_scheduler_base_rate);
	ClassDB::bind_method(D_METHOD("get_scheduler_base_rate"), &SpatialAcousticsEngine::get_scheduler_base_rate);
	ClassDB::bind_method(D_METHOD("get_scheduler_rays_issued"), &SpatialAcousticsEngine::get_scheduler_rays_issued);
	ClassDB::bind_method(D_METHOD("get_scheduler_emitters_serviced"), &SpatialAcousticsEngine::get_scheduler_emitters_serviced);
	ClassDB::bind_method(D_METHOD("get_scheduler_emitters_skipped"), &SpatialAcousticsEngine::get_scheduler_emitters_skipped);

	// Probe cache
	ClassDB::bind_method(D_METHOD("set_cache_cell_size", "size"), &SpatialAcousticsEngine::set_cache_cell_size);
	ClassDB::bind_method(D_METHOD("get_cache_cell_size"), &SpatialAcousticsEngine::get_cache_cell_size);
	ClassDB::bind_method(D_METHOD("get_cache_hits"), &SpatialAcousticsEngine::get_cache_hits);
	ClassDB::bind_method(D_METHOD("get_cache_misses"), &SpatialAcousticsEngine::get_cache_misses);
	ClassDB::bind_method(D_METHOD("invalidate_cache"), &SpatialAcousticsEngine::invalidate_cache);
	ClassDB::bind_method(D_METHOD("invalidate_cache_near", "position", "radius"), &SpatialAcousticsEngine::invalidate_cache_near);

	// Room estimation
	ClassDB::bind_method(D_METHOD("set_room_estimation_enabled", "enabled"), &SpatialAcousticsEngine::set_room_estimation_enabled);
	ClassDB::bind_method(D_METHOD("get_room_estimation_enabled"), &SpatialAcousticsEngine::get_room_estimation_enabled);
	ClassDB::bind_method(D_METHOD("set_room_ray_count", "count"), &SpatialAcousticsEngine::set_room_ray_count);
	ClassDB::bind_method(D_METHOD("get_room_ray_count"), &SpatialAcousticsEngine::get_room_ray_count);
	ClassDB::bind_method(D_METHOD("set_room_max_distance", "distance"), &SpatialAcousticsEngine::set_room_max_distance);
	ClassDB::bind_method(D_METHOD("get_room_max_distance"), &SpatialAcousticsEngine::get_room_max_distance);
	ClassDB::bind_method(D_METHOD("set_room_ignore_floor", "ignore"), &SpatialAcousticsEngine::set_room_ignore_floor);
	ClassDB::bind_method(D_METHOD("get_room_ignore_floor"), &SpatialAcousticsEngine::get_room_ignore_floor);

	// Occlusion configuration
	ClassDB::bind_method(D_METHOD("set_occlusion_enabled", "enabled"), &SpatialAcousticsEngine::set_occlusion_enabled);
	ClassDB::bind_method(D_METHOD("get_occlusion_enabled"), &SpatialAcousticsEngine::get_occlusion_enabled);
	ClassDB::bind_method(D_METHOD("set_occlusion_max_hits", "max_hits"), &SpatialAcousticsEngine::set_occlusion_max_hits);
	ClassDB::bind_method(D_METHOD("get_occlusion_max_hits"), &SpatialAcousticsEngine::get_occlusion_max_hits);
	ClassDB::bind_method(D_METHOD("set_occlusion_collision_mask", "mask"), &SpatialAcousticsEngine::set_occlusion_collision_mask);
	ClassDB::bind_method(D_METHOD("get_occlusion_collision_mask"), &SpatialAcousticsEngine::get_occlusion_collision_mask);
	ClassDB::bind_method(D_METHOD("set_listener_body_rid", "rid"), &SpatialAcousticsEngine::set_listener_body_rid);
	ClassDB::bind_method(D_METHOD("clear_listener_body_rid"), &SpatialAcousticsEngine::clear_listener_body_rid);
	ClassDB::bind_method(D_METHOD("set_physics_space", "space_rid"), &SpatialAcousticsEngine::set_physics_space);

	// Emitter position
	ClassDB::bind_method(D_METHOD("set_emitter_position", "voice_slot", "position"), &SpatialAcousticsEngine::set_emitter_position);

	// Volumetric occlusion + air absorption (Task 12)
	ClassDB::bind_method(D_METHOD("set_emitter_source_radius", "voice_slot", "radius"), &SpatialAcousticsEngine::set_emitter_source_radius);
	ClassDB::bind_method(D_METHOD("get_emitter_source_radius", "voice_slot"), &SpatialAcousticsEngine::get_emitter_source_radius);
	ClassDB::bind_method(D_METHOD("set_emitter_max_distance", "voice_slot", "max_distance"), &SpatialAcousticsEngine::set_emitter_max_distance);
	ClassDB::bind_method(D_METHOD("get_emitter_max_distance", "voice_slot"), &SpatialAcousticsEngine::get_emitter_max_distance);
	ClassDB::bind_method(D_METHOD("set_volumetric_occlusion_enabled", "enabled"), &SpatialAcousticsEngine::set_volumetric_occlusion_enabled);
	ClassDB::bind_method(D_METHOD("get_volumetric_occlusion_enabled"), &SpatialAcousticsEngine::get_volumetric_occlusion_enabled);
	ClassDB::bind_method(D_METHOD("set_volumetric_sample_count", "count"), &SpatialAcousticsEngine::set_volumetric_sample_count);
	ClassDB::bind_method(D_METHOD("get_volumetric_sample_count"), &SpatialAcousticsEngine::get_volumetric_sample_count);
	ClassDB::bind_method(D_METHOD("set_air_absorption_enabled", "enabled"), &SpatialAcousticsEngine::set_air_absorption_enabled);
	ClassDB::bind_method(D_METHOD("get_air_absorption_enabled"), &SpatialAcousticsEngine::get_air_absorption_enabled);
	ClassDB::bind_method(D_METHOD("set_air_absorption_max_distance", "distance"), &SpatialAcousticsEngine::set_air_absorption_max_distance);
	ClassDB::bind_method(D_METHOD("get_air_absorption_max_distance"), &SpatialAcousticsEngine::get_air_absorption_max_distance);
	ClassDB::bind_method(D_METHOD("set_air_absorption_scale", "scale"), &SpatialAcousticsEngine::set_air_absorption_scale);
	ClassDB::bind_method(D_METHOD("get_air_absorption_scale"), &SpatialAcousticsEngine::get_air_absorption_scale);

	// Graph wrapper
	ClassDB::bind_method(D_METHOD("wrap_stream", "stream", "loop"), &SpatialAcousticsEngine::wrap_stream, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("stream_needs_wrapping", "stream"), &SpatialAcousticsEngine::stream_needs_wrapping);
	ClassDB::bind_method(D_METHOD("compute_occlusion_cutoff", "voice_slot"), &SpatialAcousticsEngine::compute_occlusion_cutoff);
	ClassDB::bind_method(D_METHOD("compute_air_cutoff", "voice_slot"), &SpatialAcousticsEngine::compute_air_cutoff);
	ClassDB::bind_method(D_METHOD("compute_spatial_gain", "voice_slot"), &SpatialAcousticsEngine::compute_spatial_gain);

	// Reverb pool (Task 10)
	ClassDB::bind_method(D_METHOD("set_reverb_pool_slots", "slots"), &SpatialAcousticsEngine::set_reverb_pool_slots);
	ClassDB::bind_method(D_METHOD("get_reverb_pool_slots"), &SpatialAcousticsEngine::get_reverb_pool_slots);
	ClassDB::bind_method(D_METHOD("set_reverb_pool_enabled", "enabled"), &SpatialAcousticsEngine::set_reverb_pool_enabled);
	ClassDB::bind_method(D_METHOD("get_reverb_pool_enabled"), &SpatialAcousticsEngine::get_reverb_pool_enabled);
	ClassDB::bind_method(D_METHOD("get_reverb_slot_decay_time", "slot"), &SpatialAcousticsEngine::get_reverb_slot_decay_time);
	ClassDB::bind_method(D_METHOD("get_reverb_slot_damping", "slot"), &SpatialAcousticsEngine::get_reverb_slot_damping);
	ClassDB::bind_method(D_METHOD("get_reverb_slot_room_size", "slot"), &SpatialAcousticsEngine::get_reverb_slot_room_size);
	ClassDB::bind_method(D_METHOD("get_reverb_slot_wet_gain", "slot"), &SpatialAcousticsEngine::get_reverb_slot_wet_gain);
	ClassDB::bind_method(D_METHOD("get_emitter_reverb_slot", "voice_slot"), &SpatialAcousticsEngine::get_emitter_reverb_slot);
	ClassDB::bind_method(D_METHOD("get_emitter_reverb_send", "voice_slot"), &SpatialAcousticsEngine::get_emitter_reverb_send);
	ClassDB::bind_method(D_METHOD("get_emitter_reverb_prev_slot", "voice_slot"), &SpatialAcousticsEngine::get_emitter_reverb_prev_slot);
	ClassDB::bind_method(D_METHOD("get_emitter_reverb_prev_send", "voice_slot"), &SpatialAcousticsEngine::get_emitter_reverb_prev_send);
	ClassDB::bind_method(D_METHOD("is_emitter_reverb_migrating", "voice_slot"), &SpatialAcousticsEngine::is_emitter_reverb_migrating);
	ClassDB::bind_method(D_METHOD("get_reverb_active_slot_count"), &SpatialAcousticsEngine::get_reverb_active_slot_count);
	ClassDB::bind_method(D_METHOD("get_reverb_assigned_emitters"), &SpatialAcousticsEngine::get_reverb_assigned_emitters);
	ClassDB::bind_method(D_METHOD("get_reverb_migrations"), &SpatialAcousticsEngine::get_reverb_migrations);
	ClassDB::bind_method(D_METHOD("get_reverb_degraded_assignments"), &SpatialAcousticsEngine::get_reverb_degraded_assignments);
	ClassDB::bind_method(D_METHOD("reverb_pool_touched_audio_server"), &SpatialAcousticsEngine::reverb_pool_touched_audio_server);

	// Portal propagation (Task 15)
	ClassDB::bind_method(D_METHOD("set_portal_propagation_enabled", "enabled"), &SpatialAcousticsEngine::set_portal_propagation_enabled);
	ClassDB::bind_method(D_METHOD("get_portal_propagation_enabled"), &SpatialAcousticsEngine::get_portal_propagation_enabled);
	ClassDB::bind_method(D_METHOD("set_portal_diffraction_min_cutoff", "hz"), &SpatialAcousticsEngine::set_portal_diffraction_min_cutoff);
	ClassDB::bind_method(D_METHOD("get_portal_diffraction_min_cutoff"), &SpatialAcousticsEngine::get_portal_diffraction_min_cutoff);
	ClassDB::bind_method(D_METHOD("get_portal_graph_room_count"), &SpatialAcousticsEngine::get_portal_graph_room_count);
	ClassDB::bind_method(D_METHOD("get_portal_graph_edge_count"), &SpatialAcousticsEngine::get_portal_graph_edge_count);
	ClassDB::bind_method(D_METHOD("get_emitter_apparent_position", "voice_slot"), &SpatialAcousticsEngine::get_emitter_apparent_position);
	ClassDB::bind_method(D_METHOD("get_emitter_portal_gain", "voice_slot"), &SpatialAcousticsEngine::get_emitter_portal_gain);
}
