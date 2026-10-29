// SPDX-License-Identifier: GPL-3.0-or-later
//
// Bounded single-producer single-consumer ring of trivially copyable items, preallocated: push and
// pop never lock, allocate or wait. "Single producer" means one push at a time; several threads may
// push if something else (a mutex they all hold) serialises them, which also orders their writes.

#ifndef SPSCRING_H
#define SPSCRING_H

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

template <typename T, std::size_t Capacity> class SpscRing {
    static_assert(std::is_trivially_copyable_v<T>, "items are copied by value across threads");
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "capacity must be a power of two");

public:
    // Producer. False when full; the item is not queued.
    bool push(const T &item) {
        const std::size_t head = m_head.load(std::memory_order_relaxed);
        if (head - m_tail.load(std::memory_order_acquire) == Capacity)
            return false;
        m_items[head & (Capacity - 1)] = item;
        m_head.store(head + 1, std::memory_order_release);
        return true;
    }

    // Consumer. False when empty.
    bool pop(T &out) {
        const std::size_t tail = m_tail.load(std::memory_order_relaxed);
        if (tail == m_head.load(std::memory_order_acquire))
            return false;
        out = m_items[tail & (Capacity - 1)];
        m_tail.store(tail + 1, std::memory_order_release);
        return true;
    }

    // Consumer. The oldest item without removing it; false when empty.
    bool peek(T &out) const {
        const std::size_t tail = m_tail.load(std::memory_order_relaxed);
        if (tail == m_head.load(std::memory_order_acquire))
            return false;
        out = m_items[tail & (Capacity - 1)];
        return true;
    }

    // Either side; a snapshot.
    bool empty() const { return m_head.load(std::memory_order_acquire) == m_tail.load(std::memory_order_acquire); }

private:
    std::array<T, Capacity> m_items{};
    alignas(64) std::atomic<std::size_t> m_head{0}; // next slot to write
    alignas(64) std::atomic<std::size_t> m_tail{0}; // next slot to read
};

#endif // SPSCRING_H
