#pragma once
#include <algorithm>
#include <vector>
#include <numeric>
#include <cmath>

template<typename T, size_t N>
class RingBuffer {
public:
    RingBuffer() : head_(0), size_(0) {
        buffer_.reserve(N);
    }

    void push(const T& value) {
        if (size_ < N) {
            buffer_.push_back(value);
            size_++;
        } else {
            buffer_[head_] = value;
            head_ = (head_ + 1) % N;
        }
    }

    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    bool full() const { return size_ == N; }

    void clear() {
        buffer_.clear();
        head_ = 0;
        size_ = 0;
    }

    T min() const {
        if (empty()) return T();
        return *std::min_element(buffer_.begin(), buffer_.end());
    }

    T max() const {
        if (empty()) return T();
        return *std::max_element(buffer_.begin(), buffer_.end());
    }

    T average() const {
        if (empty()) return T();
        T sum = std::accumulate(buffer_.begin(), buffer_.end(), T(0));
        return sum / static_cast<T>(size_);
    }

    T percentile(double p) const {
        if (empty()) return T();

        std::vector<T> sorted(buffer_);
        std::sort(sorted.begin(), sorted.end());

        if (p <= 0.0) return sorted.front();
        if (p >= 1.0) return sorted.back();

        size_t idx = static_cast<size_t>(std::ceil(p * sorted.size())) - 1;
        if (idx >= sorted.size()) idx = sorted.size() - 1;

        return sorted[idx];
    }

    const std::vector<T>& data() const { return buffer_; }

private:
    std::vector<T> buffer_;
    size_t head_;
    size_t size_;
};
