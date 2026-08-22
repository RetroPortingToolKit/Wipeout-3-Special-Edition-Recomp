#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace clean_aot::host {

// A single-producer/single-consumer queue with one deliberately unused slot.
// Capacity is the number of usable entries. T must be trivially copyable so
// the queue cannot hide ownership or allocation behind a frame submission.
template <typename T, std::size_t Capacity>
class FixedSpscQueue final {
    static_assert(Capacity > 0U);
    static_assert(std::is_trivially_copyable_v<T>);

public:
    [[nodiscard]] bool try_push(const T& value) noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t next = increment(tail);
        if (next == head_.load(std::memory_order_acquire)) {
            return false;
        }
        storage_[tail] = value;
        tail_.store(next, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool try_pop(T& value) noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire)) {
            return false;
        }
        value = storage_[head];
        head_.store(increment(head), std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) ==
            tail_.load(std::memory_order_acquire);
    }

    void clear() noexcept {
        T ignored{};
        while (try_pop(ignored)) {
        }
    }

private:
    [[nodiscard]] static constexpr std::size_t increment(
        std::size_t index) noexcept {
        return index == Capacity ? 0U : index + 1U;
    }

    std::array<T, Capacity + 1U> storage_{};
    std::atomic<std::size_t> head_{0U};
    std::atomic<std::size_t> tail_{0U};
};

} // namespace clean_aot::host
