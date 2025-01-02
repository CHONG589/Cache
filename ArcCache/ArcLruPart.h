#pragma once

#include <unordered_map>
#include <mutex>

#include "ArcCacheNode.h"

namespace Cache {

template<typename Key, typename Value>
class ArcLruPart {
public:
    using NodeType = ArcNode<Key, Value>;
    using NodePtr = std::shard_ptr<NodeType>;
    using NodeMap = std::unordered_map<Key, NodePtr>;

    explicit ArcLruPart(size_t capacity, size_t transformThreshold)
                        : capacity_(capacity)
                        , ghostCapacity_(capacity)
                        , transformThreshold_(transformThreshold) {

        mainHead_ = std::make_shared<NodeType>();
        mainTail_ = std::make_shared<NodeType>();
        mainHead_->next_ = mainTail_;
        mainTail_->prev_ = mainHead_;

        ghostHead_ = std::make_shared<NodeType>();
        ghostTail_ = std::make_shared<NodeType>();
        ghostHead_->next_ = ghostTail_;
        ghostTail_->prev_ = ghostHead_;
    }

    bool put(Key key, Value value);
    bool get(Key key, Value &value, bool &shouldTransform);
    bool checkGhost(Key key);
    void increaseCapacity() { ++capacity_; }
    bool decreaseCapacity();

private:
    

private:
    size_t capacity_;
    size_t ghostCapacity_;
    size_t transformThreshold_;
    std::mutex mutex_;

    // Main cache
    NodeMap mainCache_;
    NodePtr mainHead_;
    NodePtr mainTail_;

    // Ghost cache
    NodeMap ghostCache_;
    NodePtr ghostHead_;
    NodePtr ghostTail_;
};

template<typename Key, typename Value>
bool ArcLruPart<Key, Value>::put(Key key, Value value) {

}

template<typename Key, typename Value>
bool ArcLruPart<Key, Value>::get(Key key, Value &value, bool &shouldTransform) {

}

template<typename Key, typename Value>
bool ArcLruPart<Key, Value>::checkGhost(Key key) {

}

template<typename Key, typename Value>
bool ArcLruPart<Key, Value>::decreaseCapacity() {

}

}// namespace Cache
