#pragma once

#include <cstddef>
#include <string>

// Public widget animation identity only. State values and primitive geometry stay package-private.
struct AnimationDataKey {
    std::string subject;
    std::string lane;

    bool operator==(const AnimationDataKey& other) const;
};

struct AnimationDataKeyHash {
    size_t operator()(const AnimationDataKey& key) const;
};
