#include <gtest/gtest.h>

#include "config/metric_board_binding.h"

TEST(BoardMetricBinding, ParsesBoardTemperatureAndFanMetrics) {
    const auto temperature = ResolveMetricBoardBindingTarget("board.temp.cpu");
    ASSERT_TRUE(temperature.has_value());
    EXPECT_EQ(temperature->kind, BoardMetricBindingKind::Temperature);
    EXPECT_EQ(temperature->logicalName, "cpu");

    const auto fan = ResolveMetricBoardBindingTarget("board.fan.system");
    ASSERT_TRUE(fan.has_value());
    EXPECT_EQ(fan->kind, BoardMetricBindingKind::Fan);
    EXPECT_EQ(fan->logicalName, "system");
}

TEST(BoardMetricBinding, MapsGpuProviderFallbackMetricsToBoardBindings) {
    const auto temperature = ResolveMetricBoardBindingTarget("gpu.temp");
    ASSERT_TRUE(temperature.has_value());
    EXPECT_EQ(temperature->kind, BoardMetricBindingKind::Temperature);
    EXPECT_EQ(temperature->logicalName, "cpu");

    const auto target = ResolveMetricBoardBindingTarget("gpu.fan");

    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(target->kind, BoardMetricBindingKind::Fan);
    EXPECT_EQ(target->logicalName, "gpu");
}

TEST(BoardMetricBinding, IgnoresMetricsWithoutBoardBindingEditors) {
    EXPECT_FALSE(ResolveMetricBoardBindingTarget("gpu.load").has_value());
    EXPECT_FALSE(ResolveMetricBoardBindingTarget("cpu.fan").has_value());
}
