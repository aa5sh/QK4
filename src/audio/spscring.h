#ifndef SPSCRING_H
#define SPSCRING_H

#include <array>
#include <atomic>
#include <cstddef>

// A fixed-capacity, lock-free, wait-free ring for exactly ONE producer thread and ONE consumer thread.
// Header-only and Qt-free.
//
// Slots are written and read in place, so neither side allocates or copies an element it does not
// need to: the producer fills the slot beginWrite() hands it and publishes it with commitWrite();
// the consumer reads (and may modify) front() and releases it with pop(). Nothing blocks. A full
// ring refuses the write; what to do then is the producer's decision.
//
// Memory order: the producer's release store of m_tail publishes the slot contents to the
// consumer's acquire load; the consumer's release store of m_head hands the slot back to the
// producer's acquire load. Each index is written by one side only.
//
// The contract that makes this correct is the one in the name. Two producers, or two consumers,
// corrupt it silently; ThreadSanitizer is the check (SpscRingTests runs under it).
template <typename T, std::size_t Capacity> class SpscRing {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    static constexpr std::size_t capacity() { return Capacity; }

    // Producer. The slot to fill, or nullptr when the ring is full. Calling it twice without a
    // commitWrite() returns the same slot.
    T *beginWrite() {
        const std::size_t tail = m_tail.load(std::memory_order_relaxed);
        if (tail - m_head.load(std::memory_order_acquire) >= Capacity)
            return nullptr;
        return &m_slots[tail & (Capacity - 1)];
    }

    // Producer. Publish the slot beginWrite() returned.
    void commitWrite() { m_tail.store(m_tail.load(std::memory_order_relaxed) + 1, std::memory_order_release); }

    // Consumer. The oldest published slot, or nullptr when empty.
    T *front() {
        const std::size_t head = m_head.load(std::memory_order_relaxed);
        if (head == m_tail.load(std::memory_order_acquire))
            return nullptr;
        return &m_slots[head & (Capacity - 1)];
    }

    // Consumer. The i-th published slot from the front (0 = front()), or nullptr past the end.
    // Lets the consumer look at what is queued - for the jitter policy - without popping it.
    T *peek(std::size_t i) {
        const std::size_t head = m_head.load(std::memory_order_relaxed);
        if (i >= m_tail.load(std::memory_order_acquire) - head)
            return nullptr;
        return &m_slots[(head + i) & (Capacity - 1)];
    }

    // Consumer. Release front() back to the producer. Must not be called on an empty ring.
    void pop() { m_head.store(m_head.load(std::memory_order_relaxed) + 1, std::memory_order_release); }

    // Consumer. Number of published slots. Exact on the consumer thread (only the producer can make
    // it grow meanwhile); approximate anywhere else.
    std::size_t size() const { return m_tail.load(std::memory_order_acquire) - m_head.load(std::memory_order_relaxed); }

    bool empty() const { return size() == 0; }

private:
    // Separate cache lines so the two sides do not false-share their indices.
    alignas(64) std::atomic<std::size_t> m_head{0}; // written by the consumer only
    alignas(64) std::atomic<std::size_t> m_tail{0}; // written by the producer only
    alignas(64) std::array<T, Capacity> m_slots{};
};

#endif // SPSCRING_H
