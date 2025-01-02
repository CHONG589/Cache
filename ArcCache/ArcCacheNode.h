#pragma once

#include <memory>

namespace Cache {

template<typename Key, typename Value>
class ArcNode {
public:
    template<typename K, typename V> friend class ArcLruPart;
    template<typename K, typename V> friend class ArcLfuPart;

    ArcNode() : accessCount_(1), prev_(nullptr), next_(nullptr) {}

    ArcNode(Key key, Value value)
            : key_(key)
            , value_(value)
            , accessCount_(1)
            , prev_(nullptr)
            , next_(nullptr) {}

    Key getKey() const { return key_; }
    Value getValue() const { return value_; }
    size_t getAccessCount() const { return accessCount_; }

    void setValue(const Value &value) { value_ = value; }
    void incrementAccessCount() { ++accessCount_; }

private:
    Key key_;
    Value value_;
    size_t accessCount_;
    std::shared_ptr<ArcNode> prev_;
    std::shared_ptr<ArcNode> next_;
};

}//namespace Cache
