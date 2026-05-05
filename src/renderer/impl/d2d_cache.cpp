#include "renderer/impl/d2d_cache.h"

size_t D2DCache::BrushCacheKeyHash::operator()(const BrushCacheKey& key) const {
    return std::hash<std::uint32_t>{}(key.packedRgba);
}

void D2DCache::Clear() {
    solidBrushes_.clear();
}

void D2DCache::ResetTarget() {
    ownerTarget_ = nullptr;
    Clear();
}

void D2DCache::AttachTarget(ID2D1RenderTarget* target) {
    if (ownerTarget_ == target) {
        return;
    }
    Clear();
    ownerTarget_ = target;
}

ID2D1SolidColorBrush* D2DCache::SolidBrush(ID2D1RenderTarget* target, RenderColor color) {
    if (target == nullptr) {
        return nullptr;
    }
    const BrushCacheKey key{color.PackedRgba()};
    if (const auto it = solidBrushes_.find(key); it != solidBrushes_.end()) {
        return it->second.Get();
    }

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(target->CreateSolidColorBrush(color.ToD2DColorF(), brush.GetAddressOf())) || brush == nullptr) {
        return nullptr;
    }
    return solidBrushes_.emplace(key, std::move(brush)).first->second.Get();
}
