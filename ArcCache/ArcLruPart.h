#pragma once

#include <unordered_map>
#include <mutex>

#include "ArcCacheNode.h"

namespace Cache {

template<typename Key, typename Value>
class ArcLruPart {
public:
    using NodeType = ArcNode<Key, Value>;
    using NodePtr = std::shared_ptr<NodeType>;
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

    void put(Key key, Value value);
    bool get(Key key, Value &value, bool &shouldTransform);
    bool checkGhost(Key key);
    void increaseCapacity() { ++capacity_; }
    bool decreaseCapacity();

private:
    void removeNode(NodePtr node);
    void insertNode(NodePtr node);
    void insertNodeToGhost(NodePtr node);
    void addNewNode(const Key &key, const Value &value);
    void removeMainToGhost(NodePtr node);

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
void ArcLruPart<Key, Value>::insertNode(NodePtr node) {
    node->next_ = mainTail_;
    node->prev_ = mainTail_->prev_;
    mainTail_->prev_->next_ = node;
    mainTail_->prev_ = node;
}

template<typename Key, typename Value>
void ArcLruPart<Key, Value>::insertNodeToGhost(NodePtr node) {
    node->next_ = ghostTail_;
    node->prev_ = ghostTail_->prev_;
    ghostTail_->prev_->next_ = node;
    ghostTail_->prev_ = node;
}

template<typename Key, typename Value>
void ArcLruPart<Key, Value>::removeNode(NodePtr node) {
    node->prev_->next_ = node->next_;
    node->next_->prev_ = node->prev_;
}

template<typename Key, typename Value>
void ArcLruPart<Key, Value>::removeMainToGhost(NodePtr node) {

    if(mainHead_->next_ == mainTail_) return ;

    //移除最久未使用的节点。即头节点后面的节点。
    removeNode(node);
    mainCache_.erase(node->getKey());

    //将移除的节点添加到ghost中。
    if(ghostCache_.size() >= ghostCapacity_) {
        //如果ghost中也满了，则按照先进先出的方式淘汰节点。
        if(ghostHead_->next_ == ghostTail_) return ;
        ghostCache_.erase(ghostHead_->next_->getKey());
        removeNode(ghostHead_->next_);
    }
    //添加到ghost中。
    insertNodeToGhost(node);
    ghostCache_[node->getKey()] = node;
}

template<typename Key, typename Value>
void ArcLruPart<Key, Value>::addNewNode(const Key &key, const Value &value) {
    if(mainCache_.size() >= capacity_) {
        removeMainToGhost(mainHead_->next_);
    }
    NodePtr newNode = std::make_shared<NodeType>(key, value);
    mainCache_[key] = newNode;
    insertNode(newNode);
}

template<typename Key, typename Value>
void ArcLruPart<Key, Value>::put(Key key, Value value) {
    if(capacity_ == 0) return ;

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = mainCache_.find(key);
    if(it != mainCache_.end()) {
        it->second->setValue(value);
        //原来就已经存在，则不需要增加新节点，只需更新它的值
        //和更换位置即可。
        //移除原来的节点。
        removeNode(it->second);
        //插入到最近访问的位置，这里最近访问的位置在表尾，所以使用
        //尾插法。
        insertNode(it->second);
        return ;
    }
    //如果不存在这个节点，则创建它，并添加进去。
    addNewNode(key, value);
}

template<typename Key, typename Value>
bool ArcLruPart<Key, Value>::get(Key key, Value &value, bool &shouldTransform) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = mainCache_.find(key);
    if(it != mainCache_.end()) {
        value = it->second->getValue();
        it->second->incrementAccessCount();
        removeNode(it->second);
        insertNode(it->second);
        if(it->second->getAccessCount() >= transformThreshold_) {
            shouldTransform = true;
        }
        return true;
    }
    return false;
}

template<typename Key, typename Value>
bool ArcLruPart<Key, Value>::checkGhost(Key key) {
    auto it = ghostCache_.find(key);
    if(it != ghostCache_.end()) {
        //如果存在这个节点，则将它从ghostCache_中移除，并添加到mainCache_中。
        //注意：在ghost中找到了，就要将它从ghost中移除，然后再添加到main中。
        removeNode(it->second);
        ghostCache_.erase(it);
        return true;
    }
    return false;
}

template<typename Key, typename Value>
bool ArcLruPart<Key, Value>::decreaseCapacity() {
    if(capacity_ <= 0) return false;

    if(mainCache_.size() == capacity_) {
        //如果mainCache是满的，则需要淘汰节点，淘汰的节点放到ghost中，
        //如果不满，则不需要淘汰，直接--capacity_即可。
        removeMainToGhost(mainHead_->next_);
    }
    --capacity_;
    return true;
}

}// namespace Cache
