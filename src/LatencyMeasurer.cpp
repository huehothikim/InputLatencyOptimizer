#include "../include/LatencyMeasurer.h"

LatencyMeasurer::LatencyMeasurer() {
    QueryPerformanceFrequency(&frequency_);
}

void LatencyMeasurer::StartMeasurement() {
    QueryPerformanceCounter(&start_time_);
}

void LatencyMeasurer::EndMeasurement() {
    LARGE_INTEGER end_time{};
    QueryPerformanceCounter(&end_time);

    LONGLONG elapsed = end_time.QuadPart - start_time_.QuadPart;
    double latency_us = (elapsed * 1000000.0) / static_cast<double>(frequency_.QuadPart);

    latencies_.push(latency_us);
}

double LatencyMeasurer::GetCurrentTimeUs() {
    static LARGE_INTEGER freq{};
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);

    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);

    return (counter.QuadPart * 1000000.0) / static_cast<double>(freq.QuadPart);
}
