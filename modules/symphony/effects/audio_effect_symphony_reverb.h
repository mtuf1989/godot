/**************************************************************************/
/*  audio_effect_symphony_reverb.h                                        */
/**************************************************************************/

#pragma once

#include "core/math/vector3.h"
#include "modules/symphony/core/symphony_seqlock.h"
#include "modules/symphony/dsp/symphony_fdn_core.h"
#include "modules/symphony/dsp/symphony_shoebox_er.h"
#include "servers/audio/audio_effect.h"

struct AudioEffectSymphonyReverbParams {
	float rt60 = 1.2f;
	float hf_rt60_ratio = 0.5f;
	float dim_x = 8.0f;
	float dim_y = 3.0f;
	float dim_z = 6.0f;
	float er_level = 1.0f;
	float er_reflection = 0.7f;
	float predelay_ms = 20.0f;
	float wet = 1.0f;
	float dry = 0.0f;
	uint32_t auto_predelay = 1;
	uint32_t _pad = 0;
};
static_assert(sizeof(AudioEffectSymphonyReverbParams) % 4 == 0, "Params must be 4-byte aligned for SeqLock");
static_assert(sizeof(AudioEffectSymphonyReverbParams) <= 256, "Params must fit SeqLock");

class AudioEffectSymphonyReverb;

class AudioEffectSymphonyReverbInstance : public AudioEffectInstance {
	GDCLASS(AudioEffectSymphonyReverbInstance, AudioEffectInstance);

	friend class AudioEffectSymphonyReverb;

	Ref<AudioEffectSymphonyReverb> base;

	SymphonyFDNCore::State fdn;
	SymphonyFDNCore::Design fdn_design;
	SymphonyShoeboxER::Runtime er;

	float *fdn_memory = nullptr;
	float *er_memory = nullptr;
	float *predelay_memory = nullptr;
	int max_line = 0;
	int er_max = 0;
	int predelay_max = 0;
	int predelay_write = 0;
	float predelay_f = 0.0f;
	float predelay_target = 0.0f;
	float pre_smooth = 1.0f;
	float mix_rate = 48000.0f;

	float cached_rt60 = -1.0f;
	float cached_hf = -1.0f;
	float cached_dx = -1.0f, cached_dy = -1.0f, cached_dz = -1.0f;
	float cached_refl = -1.0f;
	float cached_pred_ms = -1.0f;
	AudioEffectSymphonyReverbParams last_params;

	void _ensure_design(float rt60, float hf, float dx, float dy, float dz, float refl, float pred_ms, bool auto_pred);

public:
	virtual void process(const AudioFrame *p_src_frames, AudioFrame *p_dst_frames, int p_frame_count) override;
	virtual bool process_silence() const override { return false; }

	~AudioEffectSymphonyReverbInstance();
};

class AudioEffectSymphonyReverb : public AudioEffect {
	GDCLASS(AudioEffectSymphonyReverb, AudioEffect);

	friend class AudioEffectSymphonyReverbInstance;

public:
	using Params = AudioEffectSymphonyReverbParams;

private:
	Params params_shadow;
	mutable SymphonySeqLock<Params> params_lock;

	void _publish();

protected:
	static void _bind_methods();

public:
	void set_rt60(float p_rt60);
	float get_rt60() const;
	void set_hf_rt60_ratio(float p_ratio);
	float get_hf_rt60_ratio() const;
	void set_room_dimensions(const Vector3 &p_dims);
	Vector3 get_room_dimensions() const;
	void set_er_level(float p_level);
	float get_er_level() const;
	void set_er_reflection(float p_refl);
	float get_er_reflection() const;
	void set_auto_predelay(bool p_auto);
	bool get_auto_predelay() const;
	void set_predelay_ms(float p_ms);
	float get_predelay_ms() const;
	void set_wet(float p_wet);
	float get_wet() const;
	void set_dry(float p_dry);
	float get_dry() const;

	void set_room(float p_rt60, float p_hf_ratio, const Vector3 &p_dims, float p_er_reflection);

	static float compute_first_reflection_ms(const Vector3 &p_dims);

	Ref<AudioEffectInstance> instantiate() override;

	AudioEffectSymphonyReverb();
};
