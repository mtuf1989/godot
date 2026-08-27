#include "spatial_acoustics_engine.h"
#include "core/object/class_db.h"
#include "core/math/math_funcs.h"
#include "core/config/project_settings.h"
#include "servers/physics_3d/physics_server_3d.h"

SpatialAcousticsEngine *SpatialAcousticsEngine::singleton = nullptr;

SpatialAcousticsEngine::SpatialAcousticsEngine() {
	singleton = this;
	smooth_alpha = GLOBAL_DEF("audio/symphony/spatial_smooth_alpha", DEFAULT_SMOOTH_ALPHA);
	ProjectSettings::get_singleton()->set_custom_property_info(PropertyInfo(
			Variant::FLOAT, "audio/symphony/spatial_smooth_alpha", PROPERTY_HINT_RANGE, "0.01,0.99,0.01"));
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

	slot_to_emitter.erase(p_voice_slot);
}

bool SpatialAcousticsEngine::has_emitter(int p_voice_slot) const {
	return slot_to_emitter.has(p_voice_slot);
}

// --- Per-frame update ---

void SpatialAcousticsEngine::update(float p_delta) {
	// Get physics space for occlusion raycasts.
	// The space RID is set by the caller (AudioManager) via set_physics_space().
	PhysicsDirectSpaceState3D *space = nullptr;
	if (occlusion_enabled && physics_space_rid.is_valid()) {
		PhysicsServer3D *ps = PhysicsServer3D::get_singleton();
		if (ps) {
			space = ps->space_get_direct_state(physics_space_rid);
		}
	}

	for (int i = 0; i < MAX_EMITTERS; i++) {
		if (!emitters[i].active) {
			continue;
		}

		// Run occlusion solve.
		if (occlusion_enabled && space) {
			_solve_occlusion_for_emitter(i, space);
		}

		_smooth_params(i, p_delta);
		_publish_params(i);
	}
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

	e.target.occlusion = result.occlusion;
	e.target.transmission[0] = result.transmission[0];
	e.target.transmission[1] = result.transmission[1];
	e.target.transmission[2] = result.transmission[2];
}

void SpatialAcousticsEngine::set_emitter_position(int p_voice_slot, const Vector3 &p_position) {
	int *idx_ptr = slot_to_emitter.getptr(p_voice_slot);
	if (!idx_ptr) return;
	emitters[*idx_ptr].source_position = p_position;
	// Also update apparent position target (until portal redirect changes it).
	emitters[*idx_ptr].target.apparent_position = p_position;
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

	// IIR smooth: smoothed += alpha * (target - smoothed)
	// Alpha is frame-rate independent via: effective_alpha = 1 - (1 - alpha)^(dt * rate)
	// At 60 fps with rate=10 Hz base, this simplifies. We use the raw alpha as a per-update
	// coefficient since update() is called once per frame, matching Resonance Audio's pattern.
	const float a = smooth_alpha;
	const float b = 1.0f - a;

	e.smoothed.occlusion = a * e.target.occlusion + b * e.smoothed.occlusion;
	e.smoothed.transmission[0] = a * e.target.transmission[0] + b * e.smoothed.transmission[0];
	e.smoothed.transmission[1] = a * e.target.transmission[1] + b * e.smoothed.transmission[1];
	e.smoothed.transmission[2] = a * e.target.transmission[2] + b * e.smoothed.transmission[2];
	e.smoothed.air_cutoff = a * e.target.air_cutoff + b * e.smoothed.air_cutoff;
	e.smoothed.reverb_send = a * e.target.reverb_send + b * e.smoothed.reverb_send;
	e.smoothed.rt60 = a * e.target.rt60 + b * e.smoothed.rt60;
	e.smoothed.delay_s = a * e.target.delay_s + b * e.smoothed.delay_s;
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

	// Graph wrapper
	ClassDB::bind_method(D_METHOD("wrap_stream", "stream", "loop"), &SpatialAcousticsEngine::wrap_stream, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("stream_needs_wrapping", "stream"), &SpatialAcousticsEngine::stream_needs_wrapping);
	ClassDB::bind_method(D_METHOD("compute_occlusion_cutoff", "voice_slot"), &SpatialAcousticsEngine::compute_occlusion_cutoff);
	ClassDB::bind_method(D_METHOD("compute_air_cutoff", "voice_slot"), &SpatialAcousticsEngine::compute_air_cutoff);
	ClassDB::bind_method(D_METHOD("compute_spatial_gain", "voice_slot"), &SpatialAcousticsEngine::compute_spatial_gain);
}
