#include "layout_guide_sheet/layout_guide_sheet_layout.h"

#include <algorithm>

LayoutGuideSheetCalloutGeometry PlanLayoutGuideSheetCalloutGeometry(const LayoutGuideSheetCalloutGeometryInput& input) {
    const RenderPoint target = input.targetRect.Center();
    const RenderPoint center = input.cardRect.Center();
    const LayoutGuideSheetCalloutSide side =
        target.x < center.x ? LayoutGuideSheetCalloutSide::Left : LayoutGuideSheetCalloutSide::Right;
    return LayoutGuideSheetCalloutGeometry{side, target.y};
}

std::vector<LayoutGuideSheetCalloutGeometry> PlanLayoutGuideSheetCalloutGeometry(
    const std::vector<LayoutGuideSheetCalloutGeometryInput>& inputs) {
    std::vector<LayoutGuideSheetCalloutGeometry> planned(inputs.size());
    std::vector<size_t> ordered;
    ordered.reserve(inputs.size());
    for (size_t i = 0; i < inputs.size(); ++i) {
        ordered.push_back(i);
        planned[i] = PlanLayoutGuideSheetCalloutGeometry(inputs[i]);
    }
    std::stable_sort(ordered.begin(), ordered.end(), [&](size_t lhs, size_t rhs) {
        const RenderPoint lhsCenter = inputs[lhs].targetRect.Center();
        const RenderPoint rhsCenter = inputs[rhs].targetRect.Center();
        if (lhsCenter.x != rhsCenter.x) {
            return lhsCenter.x < rhsCenter.x;
        }
        return lhsCenter.y < rhsCenter.y;
    });
    const size_t leftCount =
        ordered.size() == 1
            ? (inputs[ordered.front()].targetRect.Center().x < inputs[ordered.front()].cardRect.Center().x ? 1 : 0)
            : ordered.size() / 2;
    std::vector<size_t> left(ordered.begin(), ordered.begin() + leftCount);
    std::vector<size_t> right(ordered.begin() + leftCount, ordered.end());
    const auto sortByTargetY = [&](std::vector<size_t>& indexes) {
        std::stable_sort(indexes.begin(), indexes.end(), [&](size_t lhs, size_t rhs) {
            const RenderPoint lhsCenter = inputs[lhs].targetRect.Center();
            const RenderPoint rhsCenter = inputs[rhs].targetRect.Center();
            if (lhsCenter.y != rhsCenter.y) {
                return lhsCenter.y < rhsCenter.y;
            }
            return lhsCenter.x < rhsCenter.x;
        });
    };
    sortByTargetY(left);
    sortByTargetY(right);
    for (const size_t index : left) {
        planned[index] =
            LayoutGuideSheetCalloutGeometry{LayoutGuideSheetCalloutSide::Left, inputs[index].targetRect.Center().y};
    }
    for (const size_t index : right) {
        planned[index] =
            LayoutGuideSheetCalloutGeometry{LayoutGuideSheetCalloutSide::Right, inputs[index].targetRect.Center().y};
    }
    if (!left.empty()) {
        planned[left.back()].side = LayoutGuideSheetCalloutSide::Bottom;
    }
    if (!right.empty()) {
        planned[right.front()].side = LayoutGuideSheetCalloutSide::Top;
    }
    return planned;
}
