#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include "RingBuffer.h"

class LatencyMeasurer {
public:
    LatencyMeasurer();
    ~LatencyMeasurer() = default;

    void StartMeasurement();
    void EndMeasurement();

    double GetMinLatency() const { return latencies_.min(); }
    double GetAvgLatency() const { return latencies_.average(); }
    double GetP95Latency() const { return latencies_.percentile(0.95); }
    double GetP99Latency() const { return latencies_.percentile(0.99); }
    size_t GetSampleCount() const { return latencies_.size(); }

    void Reset() { latencies_.clear(); }

    static double GetCurrentTimeUs();

private:
    LARGE_INTEGER frequency_{};
    LARGE_INTEGER start_time_{};
    RingBuffer<double, 1000> latencies_;
};
