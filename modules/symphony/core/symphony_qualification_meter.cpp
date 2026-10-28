/**************************************************************************/
/*  symphony_qualification_meter.cpp                                      */
/**************************************************************************/

#include "symphony_qualification_meter.h"

#include "core/string/ustring.h"
#include "core/variant/variant.h"

#include <cstring>

SymphonyQualificationMeter &symphony_qualification_meter() {
	static SymphonyQualificationMeter meter;
	return meter;
}

static void _atomic_max(std::atomic<uint32_t> &p_slot, uint32_t p_value) {
	uint32_t prev = p_slot.load(std::memory_order_relaxed);
	while (p_value > prev && !p_slot.compare_exchange_weak(prev, p_value, std::memory_order_relaxed)) {
	}
}

void symphony_meter_add_mix_us(uint64_t p_us) {
	const uint32_t add = p_us > 0xffffffffull ? 0xffffffffu : (uint32_t)p_us;
	if (add == 0) {
		return;
	}
	symphony_qualification_meter().mix_us.fetch_add(add, std::memory_order_relaxed);
}

void symphony_meter_note_signal_milli(uint32_t p_milli) {
	if (p_milli == 0) {
		return;
	}
	_atomic_max(symphony_qualification_meter().symphony_peak_milli, p_milli);
}

void symphony_meter_note_expired(uint32_t p_count) {
	if (p_count == 0) {
		return;
	}
	symphony_qualification_meter().expired_catchup.fetch_add(p_count, std::memory_order_relaxed);
}

void symphony_meter_publish_resume(uint32_t p_pending, uint32_t p_queue, uint32_t p_notes) {
	SymphonyQualificationMeter &meter = symphony_qualification_meter();
	meter.pending_phrases.store(p_pending, std::memory_order_relaxed);
	meter.queue_occupancy.store(p_queue, std::memory_order_relaxed);
	meter.active_notes.store(p_notes, std::memory_order_relaxed);
	meter.resume_known.store(1, std::memory_order_release);
}

static void _add_bucket(std::atomic<uint32_t> *p_buckets, std::atomic<uint32_t> &p_samples, uint64_t p_us) {
	p_buckets[symphony_meter_bucket(p_us)].fetch_add(1, std::memory_order_relaxed);
	p_samples.fetch_add(1, std::memory_order_relaxed);
}

static void _note_callback_size(SymphonyQualificationMeter &p_meter, uint32_t p_frames) {
	if (p_frames == 0) {
		return;
	}
	for (int i = 0; i < SYMPHONY_METER_SIZES; i++) {
		const uint32_t cur = p_meter.size_frames[i].load(std::memory_order_relaxed);
		if (cur == p_frames) {
			p_meter.size_counts[i].fetch_add(1, std::memory_order_relaxed);
			return;
		}
		if (cur != 0) {
			continue;
		}
		uint32_t expected = 0;
		if (p_meter.size_frames[i].compare_exchange_strong(expected, p_frames, std::memory_order_acq_rel, std::memory_order_relaxed)) {
			p_meter.size_counts[i].fetch_add(1, std::memory_order_relaxed);
			return;
		}
		if (expected == p_frames) {
			p_meter.size_counts[i].fetch_add(1, std::memory_order_relaxed);
			return;
		}
	}
	p_meter.size_overflow.fetch_add(1, std::memory_order_relaxed);
}

static void _reset_audio_timing(SymphonyQualificationMeter &p_meter) {
	for (int i = 0; i < SYMPHONY_METER_BUCKETS; i++) {
		p_meter.driver_buckets[i].store(0, std::memory_order_relaxed);
		p_meter.symphony_buckets[i].store(0, std::memory_order_relaxed);
	}
	p_meter.driver_samples.store(0, std::memory_order_relaxed);
	p_meter.symphony_samples.store(0, std::memory_order_relaxed);
	p_meter.driver_misses.store(0, std::memory_order_relaxed);
	p_meter.symphony_misses.store(0, std::memory_order_relaxed);
	for (int i = 0; i < SYMPHONY_METER_SIZES; i++) {
		p_meter.size_frames[i].store(0, std::memory_order_relaxed);
		p_meter.size_counts[i].store(0, std::memory_order_relaxed);
	}
	p_meter.size_overflow.store(0, std::memory_order_relaxed);
	p_meter.output_peak.store(0, std::memory_order_relaxed);
	p_meter.symphony_peak_milli.store(0, std::memory_order_relaxed);
	p_meter.mix_us.store(0, std::memory_order_relaxed);
}

void symphony_meter_begin_callback() {
	SymphonyQualificationMeter &meter = symphony_qualification_meter();
	if (meter.reset_timing.exchange(0, std::memory_order_acq_rel) == 1) {
		_reset_audio_timing(meter);
	}
	meter.mix_us.store(0, std::memory_order_relaxed);
}

void symphony_meter_end_callback(int p_frames, int p_rate, uint64_t p_driver_us, uint32_t p_output_peak) {
	if (p_frames <= 0 || p_rate <= 0) {
		return;
	}
	SymphonyQualificationMeter &meter = symphony_qualification_meter();
	const uint32_t frames = (uint32_t)p_frames;
	const uint64_t deadline_us = (uint64_t)frames * 1000000ull / (uint64_t)p_rate;
	const uint32_t symphony_us = meter.mix_us.load(std::memory_order_relaxed);
	_add_bucket(meter.driver_buckets, meter.driver_samples, p_driver_us);
	_add_bucket(meter.symphony_buckets, meter.symphony_samples, symphony_us);
	if (p_driver_us > deadline_us) {
		meter.driver_misses.fetch_add(1, std::memory_order_relaxed);
	}
	if ((uint64_t)symphony_us > deadline_us) {
		meter.symphony_misses.fetch_add(1, std::memory_order_relaxed);
	}
	_note_callback_size(meter, frames);
	meter.sample_rate.store((uint32_t)p_rate, std::memory_order_relaxed);
	_atomic_max(meter.output_peak, p_output_peak);
}

void symphony_meter_note_main_update(uint64_t p_us) {
	SymphonyQualificationMeter &meter = symphony_qualification_meter();
	if (meter.reset_timing.load(std::memory_order_acquire) == 1) {
		for (int i = 0; i < SYMPHONY_METER_BUCKETS; i++) {
			meter.main_buckets[i].store(0, std::memory_order_relaxed);
		}
		meter.main_samples.store(0, std::memory_order_relaxed);
	}
	_add_bucket(meter.main_buckets, meter.main_samples, p_us);
}

void symphony_meter_set_prepare_us(uint32_t p_us) {
	SymphonyQualificationMeter &meter = symphony_qualification_meter();
	meter.prepare_us.store(p_us, std::memory_order_relaxed);
	meter.prepare_known.store(1, std::memory_order_release);
}

void symphony_meter_set_generation_map_size(int p_size) {
	SymphonyQualificationMeter &meter = symphony_qualification_meter();
	if (p_size < 0) {
		meter.generation_known.store(0, std::memory_order_release);
		return;
	}
	meter.generation_map_size.store((uint32_t)p_size, std::memory_order_relaxed);
	meter.generation_known.store(1, std::memory_order_release);
}

void symphony_meter_set_workload(const char *p_name, uint32_t p_sfx, uint32_t p_spatial, uint32_t p_notes, uint32_t p_stretches, uint32_t p_reverbs, uint32_t p_phase_vocoders, uint32_t p_spectral_gates) {
	SymphonyQualificationMeter &meter = symphony_qualification_meter();
	char name[32] = {};
	if (p_name != nullptr) {
		const size_t n = strlen(p_name);
		memcpy(name, p_name, n < 31 ? n : 31);
	}
	memcpy(meter.workload_name, name, 32);
	meter.admitted_sfx.store(p_sfx, std::memory_order_relaxed);
	meter.admitted_spatial.store(p_spatial, std::memory_order_relaxed);
	meter.admitted_notes.store(p_notes, std::memory_order_relaxed);
	meter.admitted_stretches.store(p_stretches, std::memory_order_relaxed);
	meter.admitted_reverbs.store(p_reverbs, std::memory_order_relaxed);
	meter.phase_vocoders.store(p_phase_vocoders, std::memory_order_relaxed);
	meter.spectral_gates.store(p_spectral_gates, std::memory_order_relaxed);
	meter.workload_known.store(1, std::memory_order_release);
	for (int i = 0; i < SYMPHONY_METER_BUCKETS; i++) {
		meter.main_buckets[i].store(0, std::memory_order_relaxed);
	}
	meter.main_samples.store(0, std::memory_order_relaxed);
	meter.reset_timing.store(1, std::memory_order_release);
}

static void _copy_buckets(const std::atomic<uint32_t> *p_src, uint32_t *r_dst, uint32_t &r_total) {
	r_total = 0;
	for (int i = 0; i < SYMPHONY_METER_BUCKETS; i++) {
		r_dst[i] = p_src[i].load(std::memory_order_relaxed);
		r_total += r_dst[i];
	}
}

static String _bucket_text(uint64_t p_lo, uint64_t p_hi) {
	if (p_hi == UINT64_MAX) {
		return vformat("%d-inf", (int64_t)p_lo);
	}
	return vformat("%d-%d", (int64_t)p_lo, (int64_t)p_hi);
}

static void _put_percentiles(Dictionary &r_out, const char *p_prefix, const uint32_t *p_counts, uint32_t p_total) {
	const char *ranks[3] = { "p50", "p95", "p99" };
	const double ps[3] = { 0.50, 0.95, 0.99 };
	for (int i = 0; i < 3; i++) {
		const String us_key = vformat("%s_%s_us", p_prefix, ranks[i]);
		const String bucket_key = vformat("%s_%s_bucket", p_prefix, ranks[i]);
		uint64_t lo = 0;
		uint64_t hi = 0;
		if (p_total == 0 || !symphony_meter_percentile(p_counts, SYMPHONY_METER_BUCKETS, p_total, ps[i], lo, hi)) {
			r_out[us_key] = String("unavailable");
			r_out[bucket_key] = String("unavailable");
			continue;
		}
		r_out[us_key] = (int64_t)lo;
		r_out[bucket_key] = _bucket_text(lo, hi);
	}
}

static Variant _known_count(uint32_t p_known, uint32_t p_value) {
	if (p_known == 0) {
		return String("unavailable");
	}
	return (int64_t)p_value;
}

Dictionary symphony_meter_make_report(int p_worklet_underruns) {
	SymphonyQualificationMeter &meter = symphony_qualification_meter();
	Dictionary out;
	out["boundaries"] = String("AudioDriver::audio_server_process;AudioStreamPlaybackSymphony::mix+AudioEffectSymphonyReverbInstance::process;AudioServer::update;qualification_prepare");
	out["histogram"] = String("log2_microseconds");
	out["percentile"] = String("nearest_rank_bucket_low");
	out["driver_underruns"] = String("unavailable");
	if (p_worklet_underruns < 0) {
		out["worklet_underruns"] = String("unavailable");
	} else {
		out["worklet_underruns"] = p_worklet_underruns;
	}

	const bool workload_known = meter.workload_known.load(std::memory_order_acquire) != 0;
	if (!workload_known || meter.workload_name[0] == '\0') {
		out["workload"] = String("unavailable");
	} else {
		out["workload"] = String(meter.workload_name);
	}
	out["admitted_sfx"] = _known_count(workload_known ? 1 : 0, meter.admitted_sfx.load(std::memory_order_relaxed));
	out["admitted_spatial"] = _known_count(workload_known ? 1 : 0, meter.admitted_spatial.load(std::memory_order_relaxed));
	out["admitted_notes"] = _known_count(workload_known ? 1 : 0, meter.admitted_notes.load(std::memory_order_relaxed));
	out["admitted_stretches"] = _known_count(workload_known ? 1 : 0, meter.admitted_stretches.load(std::memory_order_relaxed));
	out["admitted_reverbs"] = _known_count(workload_known ? 1 : 0, meter.admitted_reverbs.load(std::memory_order_relaxed));
	out["phase_vocoders"] = _known_count(workload_known ? 1 : 0, meter.phase_vocoders.load(std::memory_order_relaxed));
	out["spectral_gates"] = _known_count(workload_known ? 1 : 0, meter.spectral_gates.load(std::memory_order_relaxed));

	uint32_t driver_counts[SYMPHONY_METER_BUCKETS];
	uint32_t symphony_counts[SYMPHONY_METER_BUCKETS];
	uint32_t main_counts[SYMPHONY_METER_BUCKETS];
	uint32_t n_driver = 0;
	uint32_t n_symphony = 0;
	uint32_t n_main = 0;
	_copy_buckets(meter.driver_buckets, driver_counts, n_driver);
	_copy_buckets(meter.symphony_buckets, symphony_counts, n_symphony);
	_copy_buckets(meter.main_buckets, main_counts, n_main);
	out["n_driver"] = (int64_t)n_driver;
	out["n_symphony"] = (int64_t)n_symphony;
	out["n_main_update"] = (int64_t)n_main;
	_put_percentiles(out, "driver", driver_counts, n_driver);
	_put_percentiles(out, "symphony", symphony_counts, n_symphony);
	_put_percentiles(out, "main_update", main_counts, n_main);

	const uint32_t rate = meter.sample_rate.load(std::memory_order_relaxed);
	if (n_driver == 0 || rate == 0) {
		out["sample_rate"] = String("unavailable");
		out["callback_frames"] = String("unavailable");
		out["deadline_us"] = String("unavailable");
		out["output_peak"] = String("unavailable");
	} else {
		out["sample_rate"] = (int64_t)rate;
		out["output_peak"] = (int64_t)meter.output_peak.load(std::memory_order_relaxed);
		String sizes;
		uint32_t modal_frames = 0;
		uint32_t modal_count = 0;
		for (int i = 0; i < SYMPHONY_METER_SIZES; i++) {
			const uint32_t frames = meter.size_frames[i].load(std::memory_order_relaxed);
			const uint32_t count = meter.size_counts[i].load(std::memory_order_relaxed);
			if (frames == 0 || count == 0) {
				continue;
			}
			if (!sizes.is_empty()) {
				sizes += ",";
			}
			sizes += vformat("%dx%d", frames, count);
			if (count > modal_count) {
				modal_count = count;
				modal_frames = frames;
			}
		}
		const uint32_t overflow = meter.size_overflow.load(std::memory_order_relaxed);
		if (overflow > 0) {
			if (!sizes.is_empty()) {
				sizes += ",";
			}
			sizes += vformat("otherx%d", overflow);
		}
		if (sizes.is_empty()) {
			out["callback_frames"] = String("unavailable");
			out["deadline_us"] = String("unavailable");
		} else {
			out["callback_frames"] = sizes;
			out["deadline_us"] = (int64_t)((uint64_t)modal_frames * 1000000ull / (uint64_t)rate);
		}
	}

	if (n_symphony == 0) {
		out["symphony_peak_milli"] = String("unavailable");
	} else {
		out["symphony_peak_milli"] = (int64_t)meter.symphony_peak_milli.load(std::memory_order_relaxed);
	}
	out["driver_deadline_misses"] = n_driver == 0 ? Variant(String("unavailable")) : Variant((int64_t)meter.driver_misses.load(std::memory_order_relaxed));
	out["symphony_deadline_misses"] = n_symphony == 0 ? Variant(String("unavailable")) : Variant((int64_t)meter.symphony_misses.load(std::memory_order_relaxed));
	out["prepare_us"] = _known_count(meter.prepare_known.load(std::memory_order_acquire), meter.prepare_us.load(std::memory_order_relaxed));
	out["expired_event_catchup"] = (int64_t)meter.expired_catchup.load(std::memory_order_relaxed);
	const uint32_t resume_known = meter.resume_known.load(std::memory_order_acquire);
	out["pending_phrases"] = _known_count(resume_known, meter.pending_phrases.load(std::memory_order_relaxed));
	out["queue_occupancy"] = _known_count(resume_known, meter.queue_occupancy.load(std::memory_order_relaxed));
	out["active_notes"] = _known_count(resume_known, meter.active_notes.load(std::memory_order_relaxed));
	out["generation_map_size"] = _known_count(meter.generation_known.load(std::memory_order_acquire), meter.generation_map_size.load(std::memory_order_relaxed));
	return out;
}
