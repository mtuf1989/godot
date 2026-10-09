/**************************************************************************/
/*  audio_effect_symphony_reverb.cpp                                      */
/**************************************************************************/

#include "audio_effect_symphony_reverb.h"

#include "core/math/math_funcs.h"
#include "core/object/class_db.h"
#include "servers/audio/audio_server.h"

#include <cmath>
#include <cstring>

namespace {
constexpr int kMaxLineMs = 200;
constexpr float kMaxRoomDim = 100.0f;
} // namespace

void AudioEffectSymphonyReverbInstance::_ensure_design(float rt60, float hf, float dx, float dy, float dz, float refl, float pred_ms, bool auto_pred) {
	const bool geom_dirty = (dx != cached_dx) || (dy != cached_dy) || (dz != cached_dz) || (refl != cached_refl);
	const bool fdn_dirty = (rt60 != cached_rt60) || (hf != cached_hf) || geom_dirty;

	if (geom_dirty) {
		SymphonyShoeboxER::TapSet taps;
		SymphonyShoeboxER::compute_relative(dx, dy, dz, refl, SymphonyShoeboxER::DEFAULT_SPEED, taps);
		SymphonyShoeboxER::set_taps(er, taps);
		cached_dx = dx;
		cached_dy = dy;
		cached_dz = dz;
		cached_refl = refl;
	}

	float mean_delay_s = 0.02f;
	if (dx > 0.0f && dy > 0.0f && dz > 0.0f) {
		const float side = std::cbrt(dx * dy * dz);
		mean_delay_s = CLAMP(side * 0.002f, 0.008f, 0.08f);
	}

	if (fdn_dirty) {
		SymphonyFDNCore::Design d;
		if (SymphonyFDNCore::design(mix_rate, 8, mean_delay_s, rt60, hf, max_line, d)) {
			fdn_design = d;
			SymphonyFDNCore::apply_design(fdn, fdn_design, cached_rt60 < 0.0f);
		}
		cached_rt60 = rt60;
		cached_hf = hf;
	}

	float pred = pred_ms;
	if (auto_pred) {
		pred = er.a.first_reflection_s * 1000.0f;
		if (er.has_b && er.crossfade_pos < SymphonyShoeboxER::CROSSFADE_SAMPLES) {
			// Blend toward destination first-reflection during crossfade.
			const float t = (float)er.crossfade_pos / (float)SymphonyShoeboxER::CROSSFADE_SAMPLES;
			pred = ((1.0f - t) * er.a.first_reflection_s + t * er.b.first_reflection_s) * 1000.0f;
		}
	}
	pred = CLAMP(pred, 0.0f, 200.0f);
	if (pred != cached_pred_ms) {
		predelay_target = pred * mix_rate * 0.001f;
		predelay_target = CLAMP(predelay_target, 0.0f, (float)(predelay_max - 2));
		cached_pred_ms = pred;
	}
}

void AudioEffectSymphonyReverbInstance::process(const AudioFrame *p_src_frames, AudioFrame *p_dst_frames, int p_frame_count) {
	AudioEffectSymphonyReverb::Params p;
	if (!base.is_valid() || !base->params_lock.try_load(p)) {
		// Keep last design; still process with shadow defaults if needed.
		if (base.is_valid()) {
			p = base->params_shadow;
		}
	}

	_ensure_design(p.rt60, p.hf_rt60_ratio, p.dim_x, p.dim_y, p.dim_z, p.er_reflection, p.predelay_ms, p.auto_predelay != 0);

	const float wet = CLAMP(p.wet, 0.0f, 1.0f);
	const float dry = CLAMP(p.dry, 0.0f, 1.0f);
	const float er_level = CLAMP(p.er_level, 0.0f, 2.0f);

	// Process in chunks to bound stack temps.
	constexpr int kChunk = 64;
	float mono[kChunk];
	float delayed[kChunk];
	float er_l[kChunk], er_r[kChunk];
	float late_l[kChunk], late_r[kChunk];

	int offset = 0;
	while (offset < p_frame_count) {
		const int n = MIN(p_frame_count - offset, kChunk);

		for (int i = 0; i < n; i++) {
			// L+R sum calibrated for Area3D uniformity=1 center send (0.5+0.5).
			mono[i] = p_src_frames[offset + i].left + p_src_frames[offset + i].right;
		}

		for (int i = 0; i < n; i++) {
			if (predelay_memory) {
				predelay_memory[predelay_write] = mono[i];
				predelay_f += (predelay_target - predelay_f) * pre_smooth;
				if (predelay_f < 0.5f) {
					delayed[i] = mono[i];
				} else {
					float read_pos = (float)predelay_write - predelay_f;
					while (read_pos < 0.0f) {
						read_pos += (float)predelay_max;
					}
					const int idx = (int)read_pos;
					const float frac = read_pos - (float)idx;
					int next = idx + 1;
					if (next >= predelay_max) {
						next = 0;
					}
					delayed[i] = predelay_memory[idx] + frac * (predelay_memory[next] - predelay_memory[idx]);
				}
				predelay_write++;
				if (predelay_write >= predelay_max) {
					predelay_write = 0;
				}
			} else {
				delayed[i] = mono[i];
			}
		}

		// ER taps from undelayed mono (early arrivals); late from pre-delayed.
		SymphonyShoeboxER::process(er, mono, er_l, er_r, n);
		SymphonyFDNCore::process(fdn, delayed, late_l, late_r, n);

		for (int i = 0; i < n; i++) {
			const AudioFrame src = p_src_frames[offset + i];
			const float wl = er_level * er_l[i] + late_l[i];
			const float wr = er_level * er_r[i] + late_r[i];
			p_dst_frames[offset + i].left = dry * src.left + wet * wl;
			p_dst_frames[offset + i].right = dry * src.right + wet * wr;
		}
		offset += n;
	}
}

AudioEffectSymphonyReverbInstance::~AudioEffectSymphonyReverbInstance() {
	if (fdn_memory) {
		memfree(fdn_memory);
		fdn_memory = nullptr;
	}
	if (er_memory) {
		memfree(er_memory);
		er_memory = nullptr;
	}
	if (predelay_memory) {
		memfree(predelay_memory);
		predelay_memory = nullptr;
	}
}

void AudioEffectSymphonyReverb::_publish() {
	params_lock.store(params_shadow);
}

void AudioEffectSymphonyReverb::set_rt60(float p_rt60) {
	params_shadow.rt60 = CLAMP(p_rt60, 0.1f, 20.0f);
	_publish();
}

float AudioEffectSymphonyReverb::get_rt60() const {
	return params_shadow.rt60;
}

void AudioEffectSymphonyReverb::set_hf_rt60_ratio(float p_ratio) {
	params_shadow.hf_rt60_ratio = CLAMP(p_ratio, 0.05f, 1.0f);
	_publish();
}

float AudioEffectSymphonyReverb::get_hf_rt60_ratio() const {
	return params_shadow.hf_rt60_ratio;
}

void AudioEffectSymphonyReverb::set_room_dimensions(const Vector3 &p_dims) {
	params_shadow.dim_x = MAX(p_dims.x, 0.0f);
	params_shadow.dim_y = MAX(p_dims.y, 0.0f);
	params_shadow.dim_z = MAX(p_dims.z, 0.0f);
	_publish();
}

Vector3 AudioEffectSymphonyReverb::get_room_dimensions() const {
	return Vector3(params_shadow.dim_x, params_shadow.dim_y, params_shadow.dim_z);
}

void AudioEffectSymphonyReverb::set_er_level(float p_level) {
	params_shadow.er_level = CLAMP(p_level, 0.0f, 2.0f);
	_publish();
}

float AudioEffectSymphonyReverb::get_er_level() const {
	return params_shadow.er_level;
}

void AudioEffectSymphonyReverb::set_er_reflection(float p_refl) {
	params_shadow.er_reflection = CLAMP(p_refl, 0.0f, 1.0f);
	_publish();
}

float AudioEffectSymphonyReverb::get_er_reflection() const {
	return params_shadow.er_reflection;
}

void AudioEffectSymphonyReverb::set_auto_predelay(bool p_auto) {
	params_shadow.auto_predelay = p_auto ? 1u : 0u;
	_publish();
}

bool AudioEffectSymphonyReverb::get_auto_predelay() const {
	return params_shadow.auto_predelay != 0;
}

void AudioEffectSymphonyReverb::set_predelay_ms(float p_ms) {
	params_shadow.predelay_ms = CLAMP(p_ms, 0.0f, 200.0f);
	_publish();
}

float AudioEffectSymphonyReverb::get_predelay_ms() const {
	return params_shadow.predelay_ms;
}

void AudioEffectSymphonyReverb::set_wet(float p_wet) {
	params_shadow.wet = CLAMP(p_wet, 0.0f, 1.0f);
	_publish();
}

float AudioEffectSymphonyReverb::get_wet() const {
	return params_shadow.wet;
}

void AudioEffectSymphonyReverb::set_dry(float p_dry) {
	params_shadow.dry = CLAMP(p_dry, 0.0f, 1.0f);
	_publish();
}

float AudioEffectSymphonyReverb::get_dry() const {
	return params_shadow.dry;
}

void AudioEffectSymphonyReverb::set_room(float p_rt60, float p_hf_ratio, const Vector3 &p_dims, float p_er_reflection) {
	params_shadow.rt60 = CLAMP(p_rt60, 0.1f, 20.0f);
	params_shadow.hf_rt60_ratio = CLAMP(p_hf_ratio, 0.05f, 1.0f);
	params_shadow.dim_x = MAX(p_dims.x, 0.0f);
	params_shadow.dim_y = MAX(p_dims.y, 0.0f);
	params_shadow.dim_z = MAX(p_dims.z, 0.0f);
	params_shadow.er_reflection = CLAMP(p_er_reflection, 0.0f, 1.0f);
	_publish();
}

float AudioEffectSymphonyReverb::compute_first_reflection_ms(const Vector3 &p_dims) {
	SymphonyShoeboxER::TapSet taps;
	SymphonyShoeboxER::compute_relative(p_dims.x, p_dims.y, p_dims.z, 1.0f, SymphonyShoeboxER::DEFAULT_SPEED, taps);
	return taps.first_reflection_s * 1000.0f;
}

Ref<AudioEffectInstance> AudioEffectSymphonyReverb::instantiate() {
	Ref<AudioEffectSymphonyReverbInstance> ins;
	ins.instantiate();
	ins->base = Ref<AudioEffectSymphonyReverb>(this);
	ins->mix_rate = 48000.0f;
	if (AudioServer::get_singleton()) {
		const float sr = AudioServer::get_singleton()->get_mix_rate();
		if (sr > 1.0f) {
			ins->mix_rate = sr;
		}
	}

	ins->max_line = (int)(kMaxLineMs * 0.001f * ins->mix_rate) + 2;
	ins->er_max = (int)(kMaxRoomDim * 3.0f / SymphonyShoeboxER::DEFAULT_SPEED * ins->mix_rate) + 2;
	ins->predelay_max = (int)(0.200f * ins->mix_rate) + 2;

	ins->fdn_memory = (float *)memalloc(sizeof(float) * (size_t)SymphonyFDNCore::MAX_LINES * (size_t)ins->max_line);
	ins->er_memory = (float *)memalloc(sizeof(float) * (size_t)ins->er_max);
	ins->predelay_memory = (float *)memalloc(sizeof(float) * (size_t)ins->predelay_max);
	ERR_FAIL_COND_V(!ins->fdn_memory || !ins->er_memory || !ins->predelay_memory, Ref<AudioEffectInstance>());

	SymphonyFDNCore::bind_buffers(ins->fdn, ins->fdn_memory, SymphonyFDNCore::MAX_LINES, ins->max_line, ins->mix_rate);
	SymphonyShoeboxER::bind_buffer(ins->er, ins->er_memory, ins->er_max, ins->mix_rate);
	std::memset(ins->predelay_memory, 0, sizeof(float) * (size_t)ins->predelay_max);

	const float smooth_n = 0.020f * ins->mix_rate;
	ins->pre_smooth = (smooth_n > 1.0f) ? (1.0f - exp2f(-1.0f / smooth_n)) : 1.0f;

	Params p = params_shadow;
	ins->_ensure_design(p.rt60, p.hf_rt60_ratio, p.dim_x, p.dim_y, p.dim_z, p.er_reflection, p.predelay_ms, p.auto_predelay != 0);
	ins->predelay_f = ins->predelay_target;
	return ins;
}

AudioEffectSymphonyReverb::AudioEffectSymphonyReverb() {
	_publish();
}

void AudioEffectSymphonyReverb::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_rt60", "seconds"), &AudioEffectSymphonyReverb::set_rt60);
	ClassDB::bind_method(D_METHOD("get_rt60"), &AudioEffectSymphonyReverb::get_rt60);
	ClassDB::bind_method(D_METHOD("set_hf_rt60_ratio", "ratio"), &AudioEffectSymphonyReverb::set_hf_rt60_ratio);
	ClassDB::bind_method(D_METHOD("get_hf_rt60_ratio"), &AudioEffectSymphonyReverb::get_hf_rt60_ratio);
	ClassDB::bind_method(D_METHOD("set_room_dimensions", "dimensions"), &AudioEffectSymphonyReverb::set_room_dimensions);
	ClassDB::bind_method(D_METHOD("get_room_dimensions"), &AudioEffectSymphonyReverb::get_room_dimensions);
	ClassDB::bind_method(D_METHOD("set_er_level", "level"), &AudioEffectSymphonyReverb::set_er_level);
	ClassDB::bind_method(D_METHOD("get_er_level"), &AudioEffectSymphonyReverb::get_er_level);
	ClassDB::bind_method(D_METHOD("set_er_reflection", "reflection"), &AudioEffectSymphonyReverb::set_er_reflection);
	ClassDB::bind_method(D_METHOD("get_er_reflection"), &AudioEffectSymphonyReverb::get_er_reflection);
	ClassDB::bind_method(D_METHOD("set_auto_predelay", "enabled"), &AudioEffectSymphonyReverb::set_auto_predelay);
	ClassDB::bind_method(D_METHOD("get_auto_predelay"), &AudioEffectSymphonyReverb::get_auto_predelay);
	ClassDB::bind_method(D_METHOD("set_predelay_ms", "msec"), &AudioEffectSymphonyReverb::set_predelay_ms);
	ClassDB::bind_method(D_METHOD("get_predelay_ms"), &AudioEffectSymphonyReverb::get_predelay_ms);
	ClassDB::bind_method(D_METHOD("set_wet", "amount"), &AudioEffectSymphonyReverb::set_wet);
	ClassDB::bind_method(D_METHOD("get_wet"), &AudioEffectSymphonyReverb::get_wet);
	ClassDB::bind_method(D_METHOD("set_dry", "amount"), &AudioEffectSymphonyReverb::set_dry);
	ClassDB::bind_method(D_METHOD("get_dry"), &AudioEffectSymphonyReverb::get_dry);
	ClassDB::bind_method(D_METHOD("set_room", "rt60", "hf_ratio", "dimensions", "er_reflection"), &AudioEffectSymphonyReverb::set_room);
	ClassDB::bind_static_method("AudioEffectSymphonyReverb", D_METHOD("compute_first_reflection_ms", "dimensions"), &AudioEffectSymphonyReverb::compute_first_reflection_ms);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rt60", PROPERTY_HINT_RANGE, "0.1,20,0.01,or_greater"), "set_rt60", "get_rt60");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "hf_rt60_ratio", PROPERTY_HINT_RANGE, "0.05,1,0.01"), "set_hf_rt60_ratio", "get_hf_rt60_ratio");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "room_dimensions"), "set_room_dimensions", "get_room_dimensions");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "er_level", PROPERTY_HINT_RANGE, "0,2,0.01"), "set_er_level", "get_er_level");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "er_reflection", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_er_reflection", "get_er_reflection");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_predelay"), "set_auto_predelay", "get_auto_predelay");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "predelay_ms", PROPERTY_HINT_RANGE, "0,200,0.1"), "set_predelay_ms", "get_predelay_ms");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wet", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_wet", "get_wet");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "dry", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_dry", "get_dry");
}
