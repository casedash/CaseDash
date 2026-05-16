#include "config/metric_board_binding.h"

namespace {

constexpr std::string_view kBoardTemperatureMetricPrefix = "board.temp.";
constexpr std::string_view kBoardFanMetricPrefix = "board.fan.";

struct MetricFallbackBoardBinding {
    std::string_view metricId;
    BoardMetricBindingKind kind = BoardMetricBindingKind::Temperature;
    std::string_view logicalName;
};

constexpr MetricFallbackBoardBinding kMetricFallbackBoardBindings[] = {
    {kGpuTemperatureMetricId, BoardMetricBindingKind::Temperature, "cpu"},
    {kGpuFanMetricId, BoardMetricBindingKind::Fan, "gpu"},
};

}  // namespace

std::optional<BoardMetricBindingTarget> ResolveMetricBoardBindingTarget(std::string_view metricId) {
    if (metricId.rfind(kBoardTemperatureMetricPrefix, 0) == 0) {
        return BoardMetricBindingTarget{
            BoardMetricBindingKind::Temperature,
            std::string(metricId.substr(kBoardTemperatureMetricPrefix.size())),
        };
    }
    if (metricId.rfind(kBoardFanMetricPrefix, 0) == 0) {
        return BoardMetricBindingTarget{
            BoardMetricBindingKind::Fan,
            std::string(metricId.substr(kBoardFanMetricPrefix.size())),
        };
    }
    for (const MetricFallbackBoardBinding& fallback : kMetricFallbackBoardBindings) {
        if (metricId == fallback.metricId) {
            return BoardMetricBindingTarget{
                fallback.kind,
                std::string(fallback.logicalName),
            };
        }
    }
    return std::nullopt;
}
