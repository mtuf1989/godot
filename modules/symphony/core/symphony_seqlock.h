#ifndef SYMPHONY_SEQLOCK_H
#define SYMPHONY_SEQLOCK_H

#include <atomic>
#include <cstdint>
#include <cstring>
#include <type_traits>

// Seqlock for a small trivially-copyable POD.
// Single writer, any number of readers. The payload is an array of atomic
// words, so the copy is not a data race and TSan does not need to be suppressed.
//
// Writer: seq = s+1 (relaxed), release fence, relaxed word stores, seq = s+2 (release).
// Reader: s1 = seq (acquire), relaxed word loads, acquire fence, s2 = seq (relaxed).

template <typename T>
class SymphonySeqLock {
	static_assert(std::is_trivially_copyable_v<T>, "SeqLock requires trivially copyable type");
	static_assert(sizeof(T) <= 256, "SeqLock is for small structs; use SPSC queue for large data");
	static_assert(sizeof(T) % 4 == 0, "SeqLock payload must be a multiple of 4 bytes; pad the struct");

	static constexpr size_t kWordCount = sizeof(T) / 4;

	alignas(64) std::atomic<uint32_t> seq_{ 0 };
	alignas(64) std::atomic<uint32_t> words_[kWordCount];

	void store_words(const T &p_value) {
		uint32_t tmp[kWordCount];
		std::memcpy(tmp, &p_value, sizeof(T));
		for (size_t i = 0; i < kWordCount; i++) {
			words_[i].store(tmp[i], std::memory_order_relaxed);
		}
	}

	void load_words(T &r_out) const {
		uint32_t tmp[kWordCount];
		for (size_t i = 0; i < kWordCount; i++) {
			tmp[i] = words_[i].load(std::memory_order_relaxed);
		}
		std::memcpy(&r_out, tmp, sizeof(T));
	}

public:
	// Writer — wait-free. Single writer only.
	void store(const T &p_value) {
		const uint32_t s = seq_.load(std::memory_order_relaxed);
		seq_.store(s + 1, std::memory_order_relaxed); // odd → writing
		std::atomic_thread_fence(std::memory_order_release);
		store_words(p_value);
		seq_.store(s + 2, std::memory_order_release); // even → done
	}

	// Reader — lock-free, retries while a write is in progress.
	T load() const {
		T result;
		while (true) {
			const uint32_t s1 = seq_.load(std::memory_order_acquire);
			if (s1 & 1u) {
				continue;
			}
			load_words(result);
			std::atomic_thread_fence(std::memory_order_acquire);
			const uint32_t s2 = seq_.load(std::memory_order_relaxed);
			if (s1 == s2) {
				return result;
			}
		}
	}

	// One attempt. Returns false when a write overlapped the read.
	bool try_load(T &r_result) const {
		const uint32_t s1 = seq_.load(std::memory_order_acquire);
		if (s1 & 1u) {
			return false;
		}
		load_words(r_result);
		std::atomic_thread_fence(std::memory_order_acquire);
		const uint32_t s2 = seq_.load(std::memory_order_relaxed);
		return s1 == s2;
	}
};

#endif // SYMPHONY_SEQLOCK_H
