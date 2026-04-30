#include "layout_guide_sheet/layout_guide_sheet_placement.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

long long Cross(RenderPoint a, RenderPoint b, RenderPoint c) {
    return static_cast<long long>(b.x - a.x) * static_cast<long long>(c.y - a.y) -
           static_cast<long long>(b.y - a.y) * static_cast<long long>(c.x - a.x);
}

bool PointsEqual(RenderPoint lhs, RenderPoint rhs) {
    return lhs.x == rhs.x && lhs.y == rhs.y;
}

bool LeaderSegmentsIntersect(RenderPoint a, RenderPoint b, RenderPoint c, RenderPoint d) {
    if (PointsEqual(a, c) || PointsEqual(a, d) || PointsEqual(b, c) || PointsEqual(b, d)) {
        return false;
    }
    const long long abC = Cross(a, b, c);
    const long long abD = Cross(a, b, d);
    const long long cdA = Cross(c, d, a);
    const long long cdB = Cross(c, d, b);
    return ((abC > 0 && abD < 0) || (abC < 0 && abD > 0)) && ((cdA > 0 && cdB < 0) || (cdA < 0 && cdB > 0));
}

bool SegmentIntersectsRect(RenderPoint a, RenderPoint b, const RenderRect& rect) {
    if (rect.IsEmpty()) {
        return false;
    }
    const int segmentLeft = std::min(a.x, b.x);
    const int segmentRight = std::max(a.x, b.x);
    const int segmentTop = std::min(a.y, b.y);
    const int segmentBottom = std::max(a.y, b.y);
    if (segmentRight < rect.left || segmentLeft > rect.right || segmentBottom < rect.top || segmentTop > rect.bottom) {
        return false;
    }
    if (rect.Contains(a) || rect.Contains(b)) {
        return true;
    }
    const RenderPoint topLeft{rect.left, rect.top};
    const RenderPoint topRight{rect.right, rect.top};
    const RenderPoint bottomLeft{rect.left, rect.bottom};
    const RenderPoint bottomRight{rect.right, rect.bottom};
    return LeaderSegmentsIntersect(a, b, topLeft, topRight) || LeaderSegmentsIntersect(a, b, topRight, bottomRight) ||
           LeaderSegmentsIntersect(a, b, bottomRight, bottomLeft) || LeaderSegmentsIntersect(a, b, bottomLeft, topLeft);
}

RenderRect TargetSafeRect(RenderPoint target, int radius) {
    return RenderRect{target.x - radius, target.y - radius, target.x + radius + 1, target.y + radius + 1};
}

RenderPoint TransformPoint(RenderPoint point, const RenderRect& source, const RenderRect& dest) {
    const double scaleX = source.Width() == 0 ? 1.0 : static_cast<double>(dest.Width()) / source.Width();
    const double scaleY = source.Height() == 0 ? 1.0 : static_cast<double>(dest.Height()) / source.Height();
    return RenderPoint{dest.left + static_cast<int>((point.x - source.left) * scaleX + 0.5),
        dest.top + static_cast<int>((point.y - source.top) * scaleY + 0.5)};
}

RenderRect TransformRect(const RenderRect& rect, const RenderRect& source, const RenderRect& dest) {
    const RenderPoint topLeft = TransformPoint(RenderPoint{rect.left, rect.top}, source, dest);
    const RenderPoint bottomRight = TransformPoint(RenderPoint{rect.right, rect.bottom}, source, dest);
    return RenderRect{topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
}

RenderRect OffsetRenderRect(RenderRect rect, int dx, int dy) {
    rect.left += dx;
    rect.right += dx;
    rect.top += dy;
    rect.bottom += dy;
    return rect;
}

RenderPoint ClosestEllipseBoundaryPoint(const RenderRect& rect, RenderPoint reference) {
    const RenderPoint center = rect.Center();
    const double radiusX = static_cast<double>(std::max(1, rect.Width())) / 2.0;
    const double radiusY = static_cast<double>(std::max(1, rect.Height())) / 2.0;
    const double dx = static_cast<double>(reference.x - center.x);
    const double dy = static_cast<double>(reference.y - center.y);
    const double normalizedLength = std::sqrt((dx * dx) / (radiusX * radiusX) + (dy * dy) / (radiusY * radiusY));
    if (normalizedLength <= 0.0) {
        return RenderPoint{center.x, rect.top};
    }
    return RenderPoint{center.x + static_cast<int>(std::lround(dx / normalizedLength)),
        center.y + static_cast<int>(std::lround(dy / normalizedLength))};
}

bool LooksLikeGaugeHalfRingRect(const RenderRect& rect) {
    if (rect.Width() <= 0 || rect.Height() <= 0) {
        return false;
    }
    const int expectedHeight = rect.Width() * 2;
    return std::abs(rect.Height() - expectedHeight) <= std::max(2, rect.Height() / 8);
}

std::optional<RenderPoint> GaugeRingColorAttachmentPoint(
    const RenderRect& rect, std::optional<LayoutEditParameter> parameter, int ringThickness) {
    if (!parameter.has_value() || !LooksLikeGaugeHalfRingRect(rect)) {
        return std::nullopt;
    }
    if (*parameter != LayoutEditParameter::ColorAccent && *parameter != LayoutEditParameter::ColorTrack) {
        return std::nullopt;
    }

    const int outerRadius = std::min(rect.Width(), std::max(1, rect.Height() / 2));
    const int ringCenterRadius = std::max(0, outerRadius - std::max(1, ringThickness) / 2);
    const int centerY = rect.Center().y;
    if (*parameter == LayoutEditParameter::ColorAccent) {
        return RenderPoint{rect.right - ringCenterRadius, centerY};
    }
    return RenderPoint{rect.left + ringCenterRadius, centerY};
}

struct PlannedCallout {
    size_t calloutIndex = 0;
    size_t cardIndex = 0;
    RenderRect target{};
};

struct CardCalloutColumns {
    std::vector<size_t> top;
    std::vector<size_t> left;
    std::vector<size_t> right;
    std::vector<size_t> bottom;
};

struct BlockLayout {
    int width = 0;
    int height = 0;
    int advanceHeight = 0;
    int itemWidth = 0;
    int itemHeight = 0;
    int itemX = 0;
    int itemY = 0;
    int leftWidth = 0;
    int rightWidth = 0;
};

struct TrialLeader {
    RenderPoint target{};
    RenderPoint bubble{};
    RenderRect targetSafeRect{};
};

struct OptimizedColumns {
    CardCalloutColumns columns;
    int intersections = 0;
    int tieBreak = 0;
};

inline constexpr size_t kMaxAdjacentOrderPasses = 20;
inline constexpr size_t kMaxSideRepairCallouts = 64;
inline constexpr size_t kMaxExactOrderCallouts = 8;

RenderPoint TargetAttachmentForCallout(const LayoutGuideSheetPlacementCallout& callout,
    const RenderRect& targetRect,
    RenderPoint bubbleAttachment,
    int gaugeRingThickness) {
    const std::optional<RenderPoint> gaugeColorAttachment =
        GaugeRingColorAttachmentPoint(targetRect, callout.hoverColorParameter, gaugeRingThickness);
    if (gaugeColorAttachment.has_value()) {
        return *gaugeColorAttachment;
    }
    if (callout.targetAttachmentOnAnchorCircle) {
        return ClosestEllipseBoundaryPoint(targetRect, bubbleAttachment);
    }
    if (callout.hoverWidgetGuide.has_value() &&
        callout.hoverWidgetGuide->parameter == LayoutEditParameter::ThroughputAxisPadding) {
        return RenderPoint{targetRect.Center().x, targetRect.top + std::max(0, targetRect.Height()) / 4};
    }
    return targetRect.Center();
}

bool RectsOverlap(const RenderRect& lhs, const RenderRect& rhs) {
    return !lhs.IsEmpty() && !rhs.IsEmpty() && lhs.left < rhs.right && lhs.right > rhs.left && lhs.top < rhs.bottom &&
           lhs.bottom > rhs.top;
}

void ErasePlannedIndex(std::vector<size_t>& indexes, size_t index) {
    indexes.erase(std::remove(indexes.begin(), indexes.end(), index), indexes.end());
}

void AppendUnique(std::vector<size_t>& indexes, size_t index) {
    if (std::find(indexes.begin(), indexes.end(), index) == indexes.end()) {
        indexes.push_back(index);
    }
}

int OrderPenalty(const std::vector<size_t>& indexes, const std::vector<size_t>& preferredOrder) {
    int penalty = 0;
    for (size_t i = 0; i < indexes.size(); ++i) {
        const auto preferredIt = std::find(preferredOrder.begin(), preferredOrder.end(), indexes[i]);
        if (preferredIt == preferredOrder.end()) {
            continue;
        }
        const int preferredPosition = static_cast<int>(preferredIt - preferredOrder.begin());
        penalty += std::abs(static_cast<int>(i) - preferredPosition);
    }
    return penalty;
}

int SideMembershipPenalty(const CardCalloutColumns& candidate, const CardCalloutColumns& preferred) {
    int penalty = 0;
    for (const size_t index : candidate.left) {
        if (std::find(preferred.left.begin(), preferred.left.end(), index) == preferred.left.end()) {
            ++penalty;
        }
    }
    for (const size_t index : candidate.right) {
        if (std::find(preferred.right.begin(), preferred.right.end(), index) == preferred.right.end()) {
            ++penalty;
        }
    }
    return penalty;
}

}  // namespace

LayoutGuideSheetPlacementResult PlaceLayoutGuideSheetCallouts(
    std::vector<LayoutGuideSheetCardPlacement>& cardPlacements,
    std::vector<LayoutGuideSheetPlacementCallout>& callouts,
    const LayoutGuideSheetPlacementStyle& style,
    const LayoutGuideSheetConstrainCalloutWidth& constrainCalloutWidth) {
    LayoutGuideSheetPlacementResult result;
    std::vector<PlannedCallout> plannedCallouts;
    plannedCallouts.reserve(callouts.size());
    const auto cardOrder = [&](const std::string& cardId) {
        const auto it = std::find_if(cardPlacements.begin(), cardPlacements.end(), [&](const auto& placement) {
            return placement.id == cardId;
        });
        if (it != cardPlacements.end()) {
            return static_cast<size_t>(it - cardPlacements.begin());
        }
        return cardPlacements.size();
    };
    for (size_t i = 0; i < callouts.size(); ++i) {
        const LayoutGuideSheetPlacementCallout& callout = callouts[i];
        const size_t calloutCardOrder = cardOrder(callout.sourceCardId);
        if (calloutCardOrder >= cardPlacements.size()) {
            continue;
        }
        plannedCallouts.push_back(PlannedCallout{i, calloutCardOrder, callout.targetRect});
    }

    std::vector<CardCalloutColumns> plannedByCard(cardPlacements.size());
    for (size_t cardIndex = 0; cardIndex < cardPlacements.size(); ++cardIndex) {
        std::vector<size_t> cardPlanned;
        for (size_t plannedIndex = 0; plannedIndex < plannedCallouts.size(); ++plannedIndex) {
            if (plannedCallouts[plannedIndex].cardIndex == cardIndex) {
                cardPlanned.push_back(plannedIndex);
            }
        }
        std::stable_sort(cardPlanned.begin(), cardPlanned.end(), [&](size_t lhs, size_t rhs) {
            const RenderPoint lhsCenter = plannedCallouts[lhs].target.Center();
            const RenderPoint rhsCenter = plannedCallouts[rhs].target.Center();
            if (lhsCenter.x != rhsCenter.x) {
                return lhsCenter.x < rhsCenter.x;
            }
            if (lhsCenter.y != rhsCenter.y) {
                return lhsCenter.y < rhsCenter.y;
            }
            return callouts[plannedCallouts[lhs].calloutIndex].order <
                   callouts[plannedCallouts[rhs].calloutIndex].order;
        });
        const size_t leftCount = cardPlanned.size() == 1 ? (plannedCallouts[cardPlanned.front()].target.Center().x <
                                                                       cardPlacements[cardIndex].sourceRect.Center().x
                                                                   ? 1
                                                                   : 0)
                                                         : cardPlanned.size() / 2;
        plannedByCard[cardIndex].left.assign(cardPlanned.begin(), cardPlanned.begin() + leftCount);
        plannedByCard[cardIndex].right.assign(cardPlanned.begin() + leftCount, cardPlanned.end());
        const auto sortByTargetY = [&](std::vector<size_t>& plannedIndexes) {
            std::stable_sort(plannedIndexes.begin(), plannedIndexes.end(), [&](size_t lhs, size_t rhs) {
                const RenderPoint lhsCenter = plannedCallouts[lhs].target.Center();
                const RenderPoint rhsCenter = plannedCallouts[rhs].target.Center();
                if (lhsCenter.y != rhsCenter.y) {
                    return lhsCenter.y < rhsCenter.y;
                }
                if (lhsCenter.x != rhsCenter.x) {
                    return lhsCenter.x < rhsCenter.x;
                }
                return callouts[plannedCallouts[lhs].calloutIndex].order <
                       callouts[plannedCallouts[rhs].calloutIndex].order;
            });
        };
        sortByTargetY(plannedByCard[cardIndex].left);
        sortByTargetY(plannedByCard[cardIndex].right);
    }

    const auto stackedHeight = [&](const std::vector<size_t>& plannedIndexes) {
        int height = 0;
        for (size_t i = 0; i < plannedIndexes.size(); ++i) {
            height += callouts[plannedCallouts[plannedIndexes[i]].calloutIndex].bubbleRect.Height();
            if (i + 1 < plannedIndexes.size()) {
                height += style.rowGap;
            }
        }
        return height;
    };

    const auto widestBubbleWidthFor = [&](const std::vector<size_t>& plannedIndexes) {
        int width = 0;
        for (const size_t plannedIndex : plannedIndexes) {
            width = std::max(width, callouts[plannedCallouts[plannedIndex].calloutIndex].bubbleRect.Width());
        }
        return width;
    };

    const auto computeBlockForColumns = [&](const CardCalloutColumns& columns,
                                            const LayoutGuideSheetCardPlacement& placement) {
        BlockLayout block;
        block.itemHeight = placement.sourceRect.Height();
        block.itemWidth = placement.sourceRect.Width();
        block.leftWidth = widestBubbleWidthFor(columns.left);
        block.rightWidth = widestBubbleWidthFor(columns.right);
        const int topWidth = widestBubbleWidthFor(columns.top);
        const int bottomWidth = widestBubbleWidthFor(columns.bottom);
        const int topHeight = stackedHeight(columns.top);
        const int bottomHeight = stackedHeight(columns.bottom);
        const int topProtrusion = topHeight > 0 ? topHeight + style.calloutGap : 0;
        const int bottomProtrusion = bottomHeight > 0 ? bottomHeight + style.calloutGap : 0;
        block.itemX = block.leftWidth > 0 ? block.leftWidth + style.calloutGap : 0;
        const int sideStackHeight = std::max(stackedHeight(columns.left), stackedHeight(columns.right));
        const int sideAbove = std::max(0, (sideStackHeight - block.itemHeight) / 2);
        const int sideBelow = std::max(0, sideStackHeight - block.itemHeight - sideAbove);
        block.itemY = std::max(topProtrusion, sideAbove);
        block.height = block.itemY + block.itemHeight + std::max(bottomProtrusion, sideBelow);
        block.advanceHeight = block.height;
        const int mainWidth =
            block.itemX + block.itemWidth + (block.rightWidth > 0 ? style.calloutGap + block.rightWidth : 0);
        int topX = block.itemX + (block.itemWidth - topWidth) / 2;
        int bottomX = block.itemX + (block.itemWidth - bottomWidth) / 2;
        const int minX = std::min({0, topX, bottomX});
        const int maxX = std::max({mainWidth, topX + topWidth, bottomX + bottomWidth});
        block.itemX -= minX;
        block.width = maxX - minX;
        return block;
    };

    const auto appendTrialLeaders = [&](std::vector<TrialLeader>& leaders,
                                        const std::vector<size_t>& plannedIndexes,
                                        LayoutGuideSheetExitSide side,
                                        const LayoutGuideSheetCardPlacement& placement,
                                        const BlockLayout& block) {
        const RenderRect cardRect{
            block.itemX, block.itemY, block.itemX + block.itemWidth, block.itemY + block.itemHeight};
        int y = cardRect.Center().y - stackedHeight(plannedIndexes) / 2;
        for (const size_t plannedIndex : plannedIndexes) {
            const PlannedCallout& planned = plannedCallouts[plannedIndex];
            const LayoutGuideSheetPlacementCallout& callout = callouts[planned.calloutIndex];
            const int bubbleX = side == LayoutGuideSheetExitSide::Left
                                    ? block.itemX - style.calloutGap - callout.bubbleRect.Width()
                                    : block.itemX + block.itemWidth + style.calloutGap;
            const RenderRect bubbleRect{
                bubbleX, y, bubbleX + callout.bubbleRect.Width(), y + callout.bubbleRect.Height()};
            const RenderPoint bubbleAttachment{
                side == LayoutGuideSheetExitSide::Left ? bubbleRect.right : bubbleRect.left, bubbleRect.Center().y};
            const RenderRect targetRect = placement.overview
                                              ? TransformRect(planned.target, placement.sourceRect, cardRect)
                                              : OffsetRenderRect(planned.target,
                                                    cardRect.left - placement.sourceRect.left,
                                                    cardRect.top - placement.sourceRect.top);
            const RenderPoint targetAttachment =
                TargetAttachmentForCallout(callout, targetRect, bubbleAttachment, style.gaugeRingThickness);
            leaders.push_back(TrialLeader{
                targetAttachment, bubbleAttachment, TargetSafeRect(targetAttachment, style.targetSafeRadius)});
            y = bubbleRect.bottom + style.rowGap;
        }
    };

    const auto appendTopBottomTrialLeaders = [&](std::vector<TrialLeader>& leaders,
                                                 const std::vector<size_t>& plannedIndexes,
                                                 LayoutGuideSheetExitSide side,
                                                 const LayoutGuideSheetCardPlacement& placement,
                                                 const BlockLayout& block) {
        const RenderRect cardRect{
            block.itemX, block.itemY, block.itemX + block.itemWidth, block.itemY + block.itemHeight};
        for (const size_t plannedIndex : plannedIndexes) {
            const PlannedCallout& planned = plannedCallouts[plannedIndex];
            const LayoutGuideSheetPlacementCallout& callout = callouts[planned.calloutIndex];
            const int bubbleX = block.itemX + (block.itemWidth - callout.bubbleRect.Width()) / 2;
            const int bubbleY = side == LayoutGuideSheetExitSide::Top
                                    ? block.itemY - style.calloutGap - callout.bubbleRect.Height()
                                    : block.itemY + block.itemHeight + style.calloutGap;
            const RenderRect bubbleRect{
                bubbleX, bubbleY, bubbleX + callout.bubbleRect.Width(), bubbleY + callout.bubbleRect.Height()};
            const RenderPoint bubbleAttachment{
                bubbleRect.Center().x, side == LayoutGuideSheetExitSide::Top ? bubbleRect.bottom : bubbleRect.top};
            const RenderRect targetRect = placement.overview
                                              ? TransformRect(planned.target, placement.sourceRect, cardRect)
                                              : OffsetRenderRect(planned.target,
                                                    cardRect.left - placement.sourceRect.left,
                                                    cardRect.top - placement.sourceRect.top);
            const RenderPoint targetAttachment =
                TargetAttachmentForCallout(callout, targetRect, bubbleAttachment, style.gaugeRingThickness);
            leaders.push_back(TrialLeader{
                targetAttachment, bubbleAttachment, TargetSafeRect(targetAttachment, style.targetSafeRadius)});
        }
    };

    const auto countLeaderIntersections = [&](const CardCalloutColumns& columns,
                                              const LayoutGuideSheetCardPlacement& placement,
                                              int stopAfter = (std::numeric_limits<int>::max)()) {
        const BlockLayout block = computeBlockForColumns(columns, placement);
        std::vector<TrialLeader> leaders;
        leaders.reserve(columns.top.size() + columns.left.size() + columns.right.size() + columns.bottom.size());
        appendTopBottomTrialLeaders(leaders, columns.top, LayoutGuideSheetExitSide::Top, placement, block);
        appendTrialLeaders(leaders, columns.left, LayoutGuideSheetExitSide::Left, placement, block);
        appendTrialLeaders(leaders, columns.right, LayoutGuideSheetExitSide::Right, placement, block);
        appendTopBottomTrialLeaders(leaders, columns.bottom, LayoutGuideSheetExitSide::Bottom, placement, block);

        int intersections = 0;
        for (size_t i = 0; i < leaders.size(); ++i) {
            for (size_t j = i + 1; j < leaders.size(); ++j) {
                if (LeaderSegmentsIntersect(
                        leaders[i].target, leaders[i].bubble, leaders[j].target, leaders[j].bubble)) {
                    ++intersections;
                    if (intersections > stopAfter) {
                        return intersections;
                    }
                }
                if (SegmentIntersectsRect(leaders[i].target, leaders[i].bubble, leaders[j].targetSafeRect)) {
                    ++intersections;
                    if (intersections > stopAfter) {
                        return intersections;
                    }
                }
                if (SegmentIntersectsRect(leaders[j].target, leaders[j].bubble, leaders[i].targetSafeRect)) {
                    ++intersections;
                    if (intersections > stopAfter) {
                        return intersections;
                    }
                }
            }
        }
        return intersections;
    };

    const auto sortStackByTargetY = [&](std::vector<size_t>& stack) {
        std::stable_sort(stack.begin(), stack.end(), [&](size_t lhs, size_t rhs) {
            const RenderPoint lhsCenter = plannedCallouts[lhs].target.Center();
            const RenderPoint rhsCenter = plannedCallouts[rhs].target.Center();
            if (lhsCenter.y != rhsCenter.y) {
                return lhsCenter.y < rhsCenter.y;
            }
            if (lhsCenter.x != rhsCenter.x) {
                return lhsCenter.x < rhsCenter.x;
            }
            return callouts[plannedCallouts[lhs].calloutIndex].order <
                   callouts[plannedCallouts[rhs].calloutIndex].order;
        });
    };

    const auto sortSideStacksByTargetY = [&](CardCalloutColumns& columns) {
        sortStackByTargetY(columns.left);
        sortStackByTargetY(columns.right);
    };

    const auto optimizeAdjacentStackOrder = [&](CardCalloutColumns& columns,
                                                std::vector<size_t>& stack,
                                                const std::vector<size_t>& preferredOrder,
                                                const LayoutGuideSheetCardPlacement& placement) {
        if (stack.size() < 2) {
            return;
        }
        for (size_t pass = 0; pass < std::min(kMaxAdjacentOrderPasses, stack.size()); ++pass) {
            bool improved = false;
            int bestScore = countLeaderIntersections(columns, placement);
            int bestPenalty = OrderPenalty(stack, preferredOrder);
            for (size_t i = 0; i + 1 < stack.size(); ++i) {
                std::swap(stack[i], stack[i + 1]);
                const int score = countLeaderIntersections(columns, placement, bestScore);
                const int penalty = OrderPenalty(stack, preferredOrder);
                if (score < bestScore || (score == bestScore && penalty < bestPenalty)) {
                    bestScore = score;
                    bestPenalty = penalty;
                    improved = true;
                } else {
                    std::swap(stack[i], stack[i + 1]);
                }
            }
            if (!improved || bestScore == 0) {
                return;
            }
        }
    };

    const auto optimizeExactStackOrder = [&](CardCalloutColumns& columns,
                                             std::vector<size_t>& stack,
                                             const std::vector<size_t>& preferredOrder,
                                             const LayoutGuideSheetCardPlacement& placement) {
        if (stack.size() < 2 || stack.size() > kMaxExactOrderCallouts) {
            return;
        }
        std::vector<size_t> candidate = stack;
        std::sort(candidate.begin(), candidate.end());
        std::vector<size_t> bestStack = stack;
        int bestScore = countLeaderIntersections(columns, placement);
        int bestPenalty = OrderPenalty(stack, preferredOrder);
        do {
            stack = candidate;
            const int score = countLeaderIntersections(columns, placement, bestScore);
            const int penalty = OrderPenalty(stack, preferredOrder);
            if (score < bestScore || (score == bestScore && penalty < bestPenalty)) {
                bestScore = score;
                bestPenalty = penalty;
                bestStack = stack;
                if (bestScore == 0) {
                    break;
                }
            }
        } while (std::next_permutation(candidate.begin(), candidate.end()));
        stack = std::move(bestStack);
    };

    const auto optimizePromotionsAndOrder = [&](const CardCalloutColumns& baseColumns,
                                                const std::vector<size_t>& preferredLeft,
                                                const std::vector<size_t>& preferredRight,
                                                const LayoutGuideSheetCardPlacement& placement) {
        OptimizedColumns best;
        best.columns = baseColumns;
        best.intersections = (std::numeric_limits<int>::max)();
        best.tieBreak = (std::numeric_limits<int>::max)();
        if (baseColumns.left.empty() && baseColumns.right.empty()) {
            best.intersections = 0;
            best.tieBreak = 0;
            return best;
        }
        const size_t noPromotion = (std::numeric_limits<size_t>::max)();
        std::vector<size_t> bottomCandidates = baseColumns.left;
        std::vector<size_t> topCandidates = baseColumns.right;
        if (bottomCandidates.empty()) {
            bottomCandidates.push_back(noPromotion);
        }
        if (topCandidates.empty()) {
            topCandidates.push_back(noPromotion);
        }

        const size_t defaultBottom = baseColumns.left.empty() ? noPromotion : baseColumns.left.back();
        const size_t defaultTop = baseColumns.right.empty() ? noPromotion : baseColumns.right.front();
        for (const size_t bottomCandidate : bottomCandidates) {
            for (const size_t topCandidate : topCandidates) {
                CardCalloutColumns trial = baseColumns;
                if (bottomCandidate != noPromotion) {
                    ErasePlannedIndex(trial.left, bottomCandidate);
                    trial.bottom.push_back(bottomCandidate);
                }
                if (topCandidate != noPromotion) {
                    ErasePlannedIndex(trial.right, topCandidate);
                    trial.top.push_back(topCandidate);
                }
                sortSideStacksByTargetY(trial);
                const int intersections = countLeaderIntersections(trial, placement, best.intersections);
                const int tieBreak = (bottomCandidate == defaultBottom ? 0 : 1000) +
                                     (topCandidate == defaultTop ? 0 : 1000) + OrderPenalty(trial.left, preferredLeft) +
                                     OrderPenalty(trial.right, preferredRight);
                if (intersections < best.intersections ||
                    (intersections == best.intersections && tieBreak < best.tieBreak)) {
                    best.intersections = intersections;
                    best.tieBreak = tieBreak;
                    best.columns = std::move(trial);
                }
                if (best.intersections == 0 && best.tieBreak == 0) {
                    break;
                }
            }
        }
        optimizeAdjacentStackOrder(best.columns, best.columns.left, preferredLeft, placement);
        optimizeAdjacentStackOrder(best.columns, best.columns.right, preferredRight, placement);
        best.intersections = countLeaderIntersections(best.columns, placement);
        best.tieBreak =
            OrderPenalty(best.columns.left, preferredLeft) + OrderPenalty(best.columns.right, preferredRight);
        return best;
    };

    std::vector<int> plannedIntersectionScores(cardPlacements.size(), 0);
    std::vector<int> sideRepairPasses(cardPlacements.size(), 0);
    for (size_t cardIndex = 0; cardIndex < cardPlacements.size(); ++cardIndex) {
        const CardCalloutColumns preferredSplit = plannedByCard[cardIndex];
        CardCalloutColumns split = preferredSplit;
        OptimizedColumns best =
            optimizePromotionsAndOrder(split, preferredSplit.left, preferredSplit.right, cardPlacements[cardIndex]);
        const size_t repairCallouts = split.top.size() + split.left.size() + split.right.size() + split.bottom.size();
        const size_t maxRepairPasses = repairCallouts > kMaxSideRepairCallouts ? 0 : plannedCallouts.size();
        for (size_t pass = 0; best.intersections > 0 && pass < maxRepairPasses; ++pass) {
            bool improved = false;
            CardCalloutColumns bestSplit = split;
            OptimizedColumns bestCandidate = best;
            int bestSidePenalty = SideMembershipPenalty(split, preferredSplit);
            const auto considerSplit = [&](const CardCalloutColumns& candidateSplit) {
                const OptimizedColumns candidate = optimizePromotionsAndOrder(
                    candidateSplit, preferredSplit.left, preferredSplit.right, cardPlacements[cardIndex]);
                const int sidePenalty = SideMembershipPenalty(candidateSplit, preferredSplit);
                if (candidate.intersections < bestCandidate.intersections ||
                    (candidate.intersections == bestCandidate.intersections &&
                        (sidePenalty < bestSidePenalty ||
                            (sidePenalty == bestSidePenalty && candidate.tieBreak < bestCandidate.tieBreak)))) {
                    bestCandidate = candidate;
                    bestSplit = candidateSplit;
                    bestSidePenalty = sidePenalty;
                    improved = candidate.intersections < best.intersections || candidate.tieBreak < best.tieBreak ||
                               sidePenalty < SideMembershipPenalty(split, preferredSplit);
                }
            };
            for (size_t leftIndex = 0; leftIndex < split.left.size(); ++leftIndex) {
                for (size_t rightIndex = 0; rightIndex < split.right.size(); ++rightIndex) {
                    CardCalloutColumns candidateSplit = split;
                    std::swap(candidateSplit.left[leftIndex], candidateSplit.right[rightIndex]);
                    sortSideStacksByTargetY(candidateSplit);
                    considerSplit(candidateSplit);
                }
            }
            for (const size_t index : split.left) {
                CardCalloutColumns candidateSplit = split;
                ErasePlannedIndex(candidateSplit.left, index);
                AppendUnique(candidateSplit.right, index);
                sortSideStacksByTargetY(candidateSplit);
                considerSplit(candidateSplit);
            }
            for (const size_t index : split.right) {
                CardCalloutColumns candidateSplit = split;
                ErasePlannedIndex(candidateSplit.right, index);
                AppendUnique(candidateSplit.left, index);
                sortSideStacksByTargetY(candidateSplit);
                considerSplit(candidateSplit);
            }
            if (!improved) {
                break;
            }
            split = std::move(bestSplit);
            best = std::move(bestCandidate);
            ++sideRepairPasses[cardIndex];
        }
        optimizeExactStackOrder(best.columns, best.columns.left, preferredSplit.left, cardPlacements[cardIndex]);
        optimizeExactStackOrder(best.columns, best.columns.right, preferredSplit.right, cardPlacements[cardIndex]);
        best.intersections = countLeaderIntersections(best.columns, cardPlacements[cardIndex]);
        plannedByCard[cardIndex] = std::move(best.columns);
        plannedIntersectionScores[cardIndex] = best.intersections;
    }

    const auto sideStackRect = [&](const std::vector<size_t>& plannedIndexes,
                                   LayoutGuideSheetExitSide side,
                                   const BlockLayout& block) {
        if (plannedIndexes.empty()) {
            return RenderRect{};
        }
        const int height = stackedHeight(plannedIndexes);
        const int top = block.itemY + block.itemHeight / 2 - height / 2;
        if (side == LayoutGuideSheetExitSide::Left) {
            return RenderRect{
                block.itemX - style.calloutGap - block.leftWidth, top, block.itemX - style.calloutGap, top + height};
        }
        return RenderRect{block.itemX + block.itemWidth + style.calloutGap,
            top,
            block.itemX + block.itemWidth + style.calloutGap + block.rightWidth,
            top + height};
    };

    const auto topBottomBubbleRect = [&](size_t plannedIndex, LayoutGuideSheetExitSide side, const BlockLayout& block) {
        const LayoutGuideSheetPlacementCallout& callout = callouts[plannedCallouts[plannedIndex].calloutIndex];
        const int x = block.itemX + (block.itemWidth - callout.bubbleRect.Width()) / 2;
        const int y = side == LayoutGuideSheetExitSide::Top
                          ? block.itemY - style.calloutGap - callout.bubbleRect.Height()
                          : block.itemY + block.itemHeight + style.calloutGap;
        return RenderRect{x, y, x + callout.bubbleRect.Width(), y + callout.bubbleRect.Height()};
    };

    const auto constrainTopBottomIfNeeded = [&](const std::vector<size_t>& plannedIndexes,
                                                LayoutGuideSheetExitSide side,
                                                const CardCalloutColumns& columns,
                                                const BlockLayout& block) {
        bool changed = false;
        const RenderRect leftStack = sideStackRect(columns.left, LayoutGuideSheetExitSide::Left, block);
        const RenderRect rightStack = sideStackRect(columns.right, LayoutGuideSheetExitSide::Right, block);
        for (const size_t plannedIndex : plannedIndexes) {
            LayoutGuideSheetPlacementCallout& callout = callouts[plannedCallouts[plannedIndex].calloutIndex];
            if (callout.bubbleRect.Width() <= block.itemWidth) {
                continue;
            }
            const RenderRect bubble = topBottomBubbleRect(plannedIndex, side, block);
            if (!RectsOverlap(bubble, leftStack) && !RectsOverlap(bubble, rightStack)) {
                continue;
            }
            if (constrainCalloutWidth) {
                constrainCalloutWidth(callout, std::max(1, block.itemWidth));
            }
            changed = true;
        }
        return changed;
    };

    for (size_t pass = 0; pass < 3; ++pass) {
        bool changed = false;
        for (size_t cardIndex = 0; cardIndex < cardPlacements.size(); ++cardIndex) {
            const BlockLayout block = computeBlockForColumns(plannedByCard[cardIndex], cardPlacements[cardIndex]);
            changed =
                constrainTopBottomIfNeeded(
                    plannedByCard[cardIndex].top, LayoutGuideSheetExitSide::Top, plannedByCard[cardIndex], block) ||
                changed;
            changed = constrainTopBottomIfNeeded(plannedByCard[cardIndex].bottom,
                          LayoutGuideSheetExitSide::Bottom,
                          plannedByCard[cardIndex],
                          block) ||
                      changed;
        }
        if (!changed) {
            break;
        }
    }
    for (size_t cardIndex = 0; cardIndex < cardPlacements.size(); ++cardIndex) {
        plannedIntersectionScores[cardIndex] =
            countLeaderIntersections(plannedByCard[cardIndex], cardPlacements[cardIndex]);
    }

    std::vector<BlockLayout> blocks(cardPlacements.size());
    int contentWidth = 0;
    for (size_t cardIndex = 0; cardIndex < cardPlacements.size(); ++cardIndex) {
        blocks[cardIndex] = computeBlockForColumns(plannedByCard[cardIndex], cardPlacements[cardIndex]);
        contentWidth = std::max(contentWidth, blocks[cardIndex].width);
    }

    const auto placeSide = [&](const std::vector<size_t>& plannedIndexes,
                               LayoutGuideSheetExitSide side,
                               const RenderRect& cardRect,
                               const BlockLayout& block) {
        int y = cardRect.Center().y - stackedHeight(plannedIndexes) / 2;
        for (const size_t plannedIndex : plannedIndexes) {
            const PlannedCallout& planned = plannedCallouts[plannedIndex];
            LayoutGuideSheetPlacementCallout& callout = callouts[planned.calloutIndex];
            const int x = side == LayoutGuideSheetExitSide::Left
                              ? block.itemX - style.calloutGap - callout.bubbleRect.Width()
                              : block.itemX + block.itemWidth + style.calloutGap;
            callout.bubbleRect = RenderRect{x, y, x + callout.bubbleRect.Width(), y + callout.bubbleRect.Height()};
            callout.exitSide = side;
            callout.bubbleAttachment =
                RenderPoint{side == LayoutGuideSheetExitSide::Left ? callout.bubbleRect.right : callout.bubbleRect.left,
                    callout.bubbleRect.Center().y};
            const int dx = cardRect.left - cardPlacements[planned.cardIndex].sourceRect.left;
            const int dy = cardRect.top - cardPlacements[planned.cardIndex].sourceRect.top;
            const RenderRect targetRect = cardPlacements[planned.cardIndex].overview
                                              ? TransformRect(planned.target,
                                                    cardPlacements[planned.cardIndex].sourceRect,
                                                    cardPlacements[planned.cardIndex].destRect)
                                              : OffsetRenderRect(planned.target, dx, dy);
            callout.targetAttachment =
                TargetAttachmentForCallout(callout, targetRect, callout.bubbleAttachment, style.gaugeRingThickness);
            y = callout.bubbleRect.bottom + style.rowGap;
        }
    };

    const auto placeTopBottom = [&](const std::vector<size_t>& plannedIndexes,
                                    LayoutGuideSheetExitSide side,
                                    const RenderRect& cardRect,
                                    const BlockLayout& block) {
        for (const size_t plannedIndex : plannedIndexes) {
            const PlannedCallout& planned = plannedCallouts[plannedIndex];
            LayoutGuideSheetPlacementCallout& callout = callouts[planned.calloutIndex];
            const int x = block.itemX + (block.itemWidth - callout.bubbleRect.Width()) / 2;
            const int y = side == LayoutGuideSheetExitSide::Top
                              ? block.itemY - style.calloutGap - callout.bubbleRect.Height()
                              : block.itemY + block.itemHeight + style.calloutGap;
            callout.bubbleRect = RenderRect{x, y, x + callout.bubbleRect.Width(), y + callout.bubbleRect.Height()};
            callout.exitSide = side;
            callout.bubbleAttachment = RenderPoint{callout.bubbleRect.Center().x,
                side == LayoutGuideSheetExitSide::Top ? callout.bubbleRect.bottom : callout.bubbleRect.top};
            const int dx = cardRect.left - cardPlacements[planned.cardIndex].sourceRect.left;
            const int dy = cardRect.top - cardPlacements[planned.cardIndex].sourceRect.top;
            const RenderRect targetRect = cardPlacements[planned.cardIndex].overview
                                              ? TransformRect(planned.target,
                                                    cardPlacements[planned.cardIndex].sourceRect,
                                                    cardPlacements[planned.cardIndex].destRect)
                                              : OffsetRenderRect(planned.target, dx, dy);
            callout.targetAttachment =
                TargetAttachmentForCallout(callout, targetRect, callout.bubbleAttachment, style.gaugeRingThickness);
        }
    };

    for (size_t cardIndex = 0; cardIndex < cardPlacements.size(); ++cardIndex) {
        LayoutGuideSheetCardPlacement& placement = cardPlacements[cardIndex];
        const BlockLayout& block = blocks[cardIndex];
        placement.destRect =
            RenderRect{block.itemX, block.itemY, block.itemX + block.itemWidth, block.itemY + block.itemHeight};
        placeTopBottom(plannedByCard[cardIndex].top, LayoutGuideSheetExitSide::Top, placement.destRect, block);
        placeSide(plannedByCard[cardIndex].left, LayoutGuideSheetExitSide::Left, placement.destRect, block);
        placeSide(plannedByCard[cardIndex].right, LayoutGuideSheetExitSide::Right, placement.destRect, block);
        placeTopBottom(plannedByCard[cardIndex].bottom, LayoutGuideSheetExitSide::Bottom, placement.destRect, block);
    }

    result.sheetWidth = style.sheetMargin * 2 + contentWidth;
    int blockCursorY = style.sheetMargin;
    int contentBottom = style.sheetMargin;
    for (size_t cardIndex = 0; cardIndex < cardPlacements.size(); ++cardIndex) {
        const int dx = style.sheetMargin + (contentWidth - blocks[cardIndex].width) / 2;
        const int dy = blockCursorY;
        LayoutGuideSheetCardPlacement& placement = cardPlacements[cardIndex];
        placement.destRect = OffsetRenderRect(placement.destRect, dx, dy);
        contentBottom = std::max(contentBottom, placement.destRect.bottom);
        const auto offsetCallouts = [&](const std::vector<size_t>& plannedIndexes) {
            for (size_t plannedIndex : plannedIndexes) {
                LayoutGuideSheetPlacementCallout& callout = callouts[plannedCallouts[plannedIndex].calloutIndex];
                callout.bubbleRect = OffsetRenderRect(callout.bubbleRect, dx, dy);
                contentBottom = std::max(contentBottom, callout.bubbleRect.bottom);
                callout.targetAttachment.x += dx;
                callout.targetAttachment.y += dy;
                callout.bubbleAttachment.x += dx;
                callout.bubbleAttachment.y += dy;
            }
        };
        offsetCallouts(plannedByCard[cardIndex].top);
        offsetCallouts(plannedByCard[cardIndex].left);
        offsetCallouts(plannedByCard[cardIndex].right);
        offsetCallouts(plannedByCard[cardIndex].bottom);
        blockCursorY += blocks[cardIndex].advanceHeight + style.blockGap;
    }
    result.sheetHeight = cardPlacements.empty() ? style.sheetMargin * 2 : contentBottom + style.sheetMargin;

    const auto leadersConflict = [&](const LayoutGuideSheetPlacementCallout& lhs,
                                     const LayoutGuideSheetPlacementCallout& rhs) {
        return LeaderSegmentsIntersect(
                   lhs.targetAttachment, lhs.bubbleAttachment, rhs.targetAttachment, rhs.bubbleAttachment) ||
               SegmentIntersectsRect(lhs.targetAttachment,
                   lhs.bubbleAttachment,
                   TargetSafeRect(rhs.targetAttachment, style.targetSafeRadius)) ||
               SegmentIntersectsRect(rhs.targetAttachment,
                   rhs.bubbleAttachment,
                   TargetSafeRect(lhs.targetAttachment, style.targetSafeRadius));
    };
    const auto sameSideLeaderIntersects = [&](const LayoutGuideSheetPlacementCallout& callout,
                                              const LayoutGuideSheetPlacementCallout& other) {
        if (callout.exitSide != other.exitSide || callout.sourceCardId != other.sourceCardId) {
            return false;
        }
        return leadersConflict(callout, other);
    };

    std::vector<const LayoutGuideSheetPlacementCallout*> leaders;
    for (const LayoutGuideSheetPlacementCallout& callout : callouts) {
        const auto crossingIt = std::find_if(leaders.begin(), leaders.end(), [&](const auto& leader) {
            return sameSideLeaderIntersects(callout, *leader);
        });
        if (crossingIt != leaders.end()) {
            result.warningCalloutKeys.push_back(callout.key + " <-> " + (*crossingIt)->key);
        }
        leaders.push_back(&callout);
    }

    result.blocks.reserve(cardPlacements.size());
    for (size_t cardIndex = 0; cardIndex < cardPlacements.size(); ++cardIndex) {
        const CardCalloutColumns& columns = plannedByCard[cardIndex];
        result.blocks.push_back(LayoutGuideSheetPlacementBlockTrace{cardPlacements[cardIndex].id,
            plannedIntersectionScores[cardIndex],
            sideRepairPasses[cardIndex],
            columns.left.size(),
            columns.top.size(),
            columns.right.size(),
            columns.bottom.size()});
    }
    return result;
}
