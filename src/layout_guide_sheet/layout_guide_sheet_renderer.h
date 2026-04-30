#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "layout_guide_sheet/layout_guide_sheet_types.h"

class DashboardRenderer;
struct SystemSnapshot;

class LayoutGuideSheetRenderer {
public:
    explicit LayoutGuideSheetRenderer(DashboardRenderer& dashboardRenderer);

    bool SavePng(const std::filesystem::path& imagePath,
        const SystemSnapshot& snapshot,
        const std::vector<LayoutGuideSheetCalloutRequest>& calloutRequests,
        const std::vector<std::string>& selectedCardIds,
        std::vector<std::string>* traceDetails = nullptr,
        std::string* errorText = nullptr);
    bool RenderOffscreen(const SystemSnapshot& snapshot,
        const std::vector<LayoutGuideSheetCalloutRequest>& calloutRequests,
        const std::vector<std::string>& selectedCardIds,
        std::vector<std::string>* traceDetails = nullptr,
        std::string* errorText = nullptr);

private:
    using SurfaceDrawCallback = std::function<void()>;
    using SurfaceRenderer = std::function<bool(int width, int height, SurfaceDrawCallback draw)>;

    bool Render(const SystemSnapshot& snapshot,
        const std::vector<LayoutGuideSheetCalloutRequest>& calloutRequests,
        const std::vector<std::string>& selectedCardIds,
        const SurfaceRenderer& renderSurface,
        std::vector<std::string>* traceDetails,
        std::string* errorText);

    DashboardRenderer& dashboardRenderer_;
};
