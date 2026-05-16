#pragma once

#include <optional>
#include <string>
#include <string_view>

inline constexpr std::string_view kGpuTemperatureMetricId = "gpu.temp";
inline constexpr std::string_view kGpuFanMetricId = "gpu.fan";

enum class BoardMetricBindingKind {
    Temperature,
    Fan,
};

struct BoardMetricBindingTarget {
    BoardMetricBindingKind kind = BoardMetricBindingKind::Temperature;
    std::string logicalName;
};

std::optional<BoardMetricBindingTarget> ResolveMetricBoardBindingTarget(std::string_view metricId);
