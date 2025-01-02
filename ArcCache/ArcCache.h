#pragma once

#include "../CachePolicy.h"
#include "ArcLruPart.h"
#include "ArcLfuPart.h"

#include <memory>

namespace Cache {

template<typename Key, typename Value>
class ArcCache : public CachePolicy<Key, Value> {
public:
    explicit ArcCache(size_t capacity = 10, size_t transformThreshold = 2)
                    : capacity_(capacity)
                    , transformThreshold_(transformThreshold)
                    , lruPart_(std::make_unique<ArcLruPart<Key, Value>>(capacity, transformThreshold))
                    , lfuPart_(std::make_unique<ArcLfuPart<Key, Value>>(capacity, transformThreshold)) {}

    ~ArcCache() override = default;

    void put(Key key, Value value) override;
    bool get(Key key, Value &value) override;
    Value get(Key key) override;

private:
    bool checkGhostCaches(Key key);

private:
    size_t capacity_;
    size_t transformThreshold_;
    std::unique_ptr<ArcLruPart<Key, Value>> lruPart_;
    std::unique_ptr<ArcLfuPart<Key, Value>> lfuPart_;
};

}// namespace Cache
