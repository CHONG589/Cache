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

template<typename Key, typename Value>
bool ArcCache<Key, Value>::checkGhostCaches(Key key) {
    bool inGhost = false;
    if(lruPart_->checkGhost(key)) {
        //在LRU的ghost缓存中，则LRU缓存太小了，需要增加容量
        lruPart_->increaseCapacity();
        lfuPart_->decreaseCapacity();
        inGhost = true;
    }
    else if(lfuPart_->checkGhost(key)) {
        lruPart_->decreaseCapacity();
        lfuPart_->increaseCapacity();
        inGhost = true;
    }
    return inGhost;
}

template<typename Key, typename Value>
void ArcCache<Key, Value>::put(Key key, Value value) {
    bool inGhost = checkGhostCaches(key);
    if(inGhost) {
        //不管在哪个ghost中，都是加入到LRU缓存中。
        lruPart_->put(key, value);
    }
    else {
        //如果不在ghost中，则根据key的hash值，加入到LRU缓存中。
        lruPart_->put(key, value);
    }
}

template<typename Key, typename Value>
bool ArcCache<Key, Value>::get(Key key, Value &value) {
    //调整容量
    checkGhostCaches(key);
    bool shouldTransform = false;
    if(lruPart_->get(key, value, shouldTransform)) {
        //在lru中找到了
        if(shouldTransform) {
            //且访问次数超过了transformThreshold_，
            //则也要放进lfu缓存中。
            lfuPart_->put(key, value);
        }
        return true;
    }
    return lfuPart_->get(key, value);
}

template<typename Key, typename Value>
Value ArcCache<Key, Value>::get(Key key) {
    Value value{};
    get(key, value);
    return value;
}

}// namespace Cache
