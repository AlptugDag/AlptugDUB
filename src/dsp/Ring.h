// AlptugDUB by Alptuğ Dağ — a fixed-capacity queue that never allocates on the audio thread.
#pragma once

#include <array>
#include <cstddef>

namespace adub {

/** First-in first-out, capacity N (a power of two). When full, the oldest item is dropped. */
template <typename T, size_t N>
class Ring {
    static_assert((N & (N - 1)) == 0, "N must be a power of two");

public:
    void clear() { head = count = 0; }
    size_t size() const { return count; }
    bool empty() const { return count == 0; }

    void push_back(const T& v) {
        if (count == N) pop_front();
        items[(head + count) & (N - 1)] = v;
        ++count;
    }
    void pop_front() {
        head = (head + 1) & (N - 1);
        --count;
    }
    T& front() { return items[head]; }
    const T& front() const { return items[head]; }
    T& operator[](size_t i) { return items[(head + i) & (N - 1)]; }
    const T& operator[](size_t i) const { return items[(head + i) & (N - 1)]; }

private:
    std::array<T, N> items{};
    size_t head = 0, count = 0;
};

} // namespace adub
