/**************************************************************************/
/*  symphony_qualification_meter.h                                        */
/**************************************************************************/
/* Bounded qualification measurements.                                    */
/*                                                                        */
/* The audio callback only updates preallocated atomics. Histograms are  */
/* 64 log2 microsecond buckets. The main thread copies them into a       */
/* report. A counter that this platform cannot read is reported as       */
/* unavailable, never as zero.                                            */
/**************************************************************************/

#pragma once

#include "core/variant/dictionary.h"

#include <atomic>
#include <cstdint>

static constexpr int SYMPHONY_METER_BUCKETS = 64;
static constexpr int SYMPHONY_METER_SIZES = 8;

// Bucket 0 is [0, 1) µs. Bucket k (1..62) is [2^(k-1), 2^k) µs.
// Bucket 63 is [2^62, +inf) µs.
inline int symphony_meter_bucket(uint64_t p_us) {
	if (p_us == 0) {
		return 0;
	}
	int bucket = 1;
	uint64_t hi = 2;
	while (p_us >= hi && bucket < SYMPHONY_METER_BUCKETS - 1) {
		if (hi > (UINT64_MAX >> 1)) {
			return SYMPHONY_METER_BUCKETS - 1;
		}
		hi <<= 1;
		bucket++;
	}
	return bucket;
}

inline void symphony_meter_bucket_bounds(int p_bucket, uint64_t &r_lo, uint64_t &r_hi) {
	if (p_bucket <= 0) {
		r_lo = 0;
		r_hi = 1;
		return;
	}
	if (p_bucket >= SYMPHONY_METER_BUCKETS - 1) {
		r_lo = 1ull << (SYMPHONY_METER_BUCKETS - 2);
		r_hi = UINT64_MAX;
		return;
	}
	r_lo = 1ull << (p_bucket - 1);
	r_hi = 1ull << p_bucket;
}

// Nearest-rank percentile on a histogram snapshot. r_lo/r_hi is the bucket
// that contains the rank, so the caller can tell a sample from the bin width.
inline bool symphony_meter_percentile(const uint32_t *p_counts, int p_buckets, uint32_t p_total, double p_rank, uint64_t &r_lo, uint64_t &r_hi) {
	if (p_counts == nullptr || p_total == 0 || p_buckets <= 0) {
		return false;
	}
	const uint64_t rank = (uint64_t)((double)(p_total - 1) * p_rank);
	uint64_t acc = 0;
	for (int i = 0; i < p_buckets; i++) {
		acc += p_counts[i];
		if (acc > rank) {
			symphony_meter_bucket_bounds(i, r_lo, r_hi);
			return true;
		}
	}
	return false;
}

struct SymphonyQualificationMeter {
	std::atomic<uint32_t> mix_us{ 0 };
	std::atomic<uint32_t> reset_timing{ 0 };

	std::atomic<uint32_t> driver_buckets[SYMPHONY_METER_BUCKETS];
	std::atomic<uint32_t> symphony_buckets[SYMPHONY_METER_BUCKETS];
	std::atomic<uint32_t> main_buckets[SYMPHONY_METER_BUCKETS];
	std::atomic<uint32_t> driver_samples{ 0 };
	std::atomic<uint32_t> symphony_samples{ 0 };
	std::atomic<uint32_t> main_samples{ 0 };
	std::atomic<uint32_t> driver_misses{ 0 };
	std::atomic<uint32_t> symphony_misses{ 0 };

	std::atomic<uint32_t> size_frames[SYMPHONY_METER_SIZES];
	std::atomic<uint32_t> size_counts[SYMPHONY_METER_SIZES];
	std::atomic<uint32_t> size_overflow{ 0 };

	SymphonyQualificationMeter() {
		for (int i = 0; i < SYMPHONY_METER_BUCKETS; i++) {
			driver_buckets[i].store(0, std::memory_order_relaxed);
			symphony_buckets[i].store(0, std::memory_order_relaxed);
			main_buckets[i].store(0, std::memory_order_relaxed);
		}
		for (int i = 0; i < SYMPHONY_METER_SIZES; i++) {
			size_frames[i].store(0, std::memory_order_relaxed);
			size_counts[i].store(0, std::memory_order_relaxed);
		}
	}

	std::atomic<uint32_t> sample_rate{ 0 };
	std::atomic<uint32_t> output_peak{ 0 };
	std::atomic<uint32_t> symphony_peak_milli{ 0 };

	std::atomic<uint32_t> expired_catchup{ 0 };
	std::atomic<uint32_t> pending_phrases{ 0 };
	std::atomic<uint32_t> queue_occupancy{ 0 };
	std::atomic<uint32_t> active_notes{ 0 };
	std::atomic<uint32_t> resume_known{ 0 };

	std::atomic<uint32_t> generation_map_size{ 0 };
	std::atomic<uint32_t> generation_known{ 0 };
	std::atomic<uint32_t> prepare_us{ 0 };
	std::atomic<uint32_t> prepare_known{ 0 };

	std::atomic<uint32_t> workload_known{ 0 };
	std::atomic<uint32_t> admitted_sfx{ 0 };
	std::atomic<uint32_t> admitted_spatial{ 0 };
	std::atomic<uint32_t> admitted_notes{ 0 };
	std::atomic<uint32_t> admitted_stretches{ 0 };
	std::atomic<uint32_t> admitted_reverbs{ 0 };
	std::atomic<uint32_t> phase_vocoders{ 0 };
	std::atomic<uint32_t> spectral_gates{ 0 };
	char workload_name[32] = {};
};

SymphonyQualificationMeter &symphony_qualification_meter();

void symphony_meter_add_mix_us(uint64_t p_us);
void symphony_meter_note_signal_milli(uint32_t p_milli);
void symphony_meter_note_expired(uint32_t p_count);
void symphony_meter_publish_resume(uint32_t p_pending, uint32_t p_queue, uint32_t p_notes);

void symphony_meter_begin_callback();
void symphony_meter_end_callback(int p_frames, int p_rate, uint64_t p_driver_us, uint32_t p_output_peak);
void symphony_meter_note_main_update(uint64_t p_us);

void symphony_meter_set_prepare_us(uint32_t p_us);
void symphony_meter_set_generation_map_size(int p_size);
void symphony_meter_set_workload(const char *p_name, uint32_t p_sfx, uint32_t p_spatial, uint32_t p_notes, uint32_t p_stretches, uint32_t p_reverbs, uint32_t p_phase_vocoders, uint32_t p_spectral_gates);

// Main thread. p_worklet_underruns < 0 means that counter cannot be read.
Dictionary symphony_meter_make_report(int p_worklet_underruns);
