#pragma once

#include <chrono>
#include <cstddef>

// Shared telemetry update cadence and live animation duration.
inline constexpr auto kTelemetryRefreshInterval = std::chrono::milliseconds(250);
inline constexpr double kTelemetryRefreshIntervalSeconds =
    static_cast<double>(kTelemetryRefreshInterval.count()) / 1000.0;

// 120 samples at 250 ms keeps 30 seconds of retained chart history.
inline constexpr std::size_t kRetainedHistorySamples = 120;

// 4 samples at 250 ms makes each throughput chart point a 1 second moving average.
inline constexpr std::size_t kThroughputHistorySmoothingSamples = 4;

// 40 samples at 250 ms spaces throughput chart time markers every 10 seconds.
inline constexpr double kThroughputTimeMarkerIntervalSeconds = 10.0;
inline constexpr double kThroughputTimeMarkerIntervalSamples =
    kThroughputTimeMarkerIntervalSeconds / kTelemetryRefreshIntervalSeconds;
