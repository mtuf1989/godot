#ifndef SYMPHONY_SEQLOCK_H
#define SYMPHONY_SEQLOCK_H

#include <atomic>
#include <cstring>
#include <type_traits>

// Godot-compatible SeqLock for lock-free publish of small POD structs.
//
// Writer: wait-free, single writer only (main thread or audio thread).
// Readers: lock-free, unlimited concurrent readers.
//
// Source: ADC 2023 (Timur Doumler), adapted for Symphony.
// See: game-template/docs/audio/plan/notes_future_milestone.md
//
// TSan note: The data_ copy is technically a data race per C++ standard,
// but safe in practice because the seq check detects torn reads and retries.
// We suppress TSan on the reader to avoid false positives.

#if defined(__has_attribute)
#if __has_attribute(no_sanitize)
#define SYMPHONY_NO_TSAN __attribute__((no_sanitize("thread")))
#else
#define SYMPHONY_NO_TSAN
#endif
#else
#define SYMPHONY_NO_TSAN
#endif

template <typename T>
class SymphonySeqLock {
	static_assert(std::is_trivially_copyable_v<T>, "SeqLock requires trivially copyable type");
	static_assert(sizeof(T) <= 256, "SeqLock is for small structs; use SPSC queue for large data");

	alignas(64) std::atomic<uint32_t> seq_{ 0 };
	alignas(64) T data_{};

public:
	// Writer — wait-free, constant time. Single writer only.
	void store(const T &value) {
		uint32_t s = seq_.load(std::memory_order_relaxed);
		seq_.store(s + 1, std::memory_order_relaxed); // odd → writing
		std::atomic_thread_fence(std::memory_order_release);
		std::memcpy(&data_, &value, sizeof(T));
		std::atomic_thread_fence(std::memory_order_release);
		seq_.store(s + 2, std::memory_order_relaxed); // even → done
	}

	// Reader — lock-free, may retry on contention. Any thread.
	SYMPHONY_NO_TSAN T load() const {
		T result;
		while (true) {
			uint32_t s1 = seq_.load(std::memory_order_acquire);
			if (s1 & 1) {
				continue; // writer active
			}
			std::atomic_thread_fence(std::memory_order_acquire);
			std::memcpy(&result, &data_, sizeof(T));
			std::atomic_thread_fence(std::memory_order_acquire);
			uint32_t s2 = seq_.load(std::memory_order_acquire);
			if (s1 == s2) {
				break; // consistent read
			}
		}
		return result;
	}

	// Non-blocking attempt — returns false if couldn't get clean read.
	SYMPHONY_NO_TSAN bool try_load(T &result) const {
		uint32_t s1 = seq_.load(std::memory_order_acquire);
		if (s1 & 1) {
			return false;
		}
		std::atomic_thread_fence(std::memory_order_acquire);
		std::memcpy(&result, &data_, sizeof(T));
		std::atomic_thread_fence(std::memory_order_acquire);
		uint32_t s2 = seq_.load(std::memory_order_acquire);
		return (s1 == s2);
	}
};

#endif // SYMPHONY_SEQLOCK_H
