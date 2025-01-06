#pragma once

#include <unordered_map>
#include <map>
#include <mutex>
#include <list>

#include "ArcCacheNode.h"

namespace Cache {

template<typename Key, typename Value>
class ArcLfuPart {
public:
    using NodeType = ArcNode<Key, Value>;
    using NodePtr = std::shared_ptr<NodeType>;
    using NodeMap = std::unordered_map<Key, NodePtr>;
    using FreqMap = std::map<size_t, std::list<NodePtr>>;

    explicit ArcLfuPart(size_t capacity, size_t transformThreshold)
                        : capacity_(capacity)
                        , ghostCapacity_(capacity)
                        , transformThreshold_(transformThreshold)
                        , minFreq_(1) {

        ghostHead_ = std::make_shared<NodeType>();
        ghostTail_ = std::make_shared<NodeType>();
        ghostHead_->next_ = ghostTail_;
        ghostTail_->prev_ = ghostHead_;
    }

    void put(Key key, Value value);
    bool get(Key key, Value &value);
    bool checkGhost(Key key);
    void increaseCapacity() { ++capacity_; }
    bool decreaseCapacity();

private:
    void addNewNode(const Key &key, const Value &value);
    //ghost中的节点从尾部插入，淘汰头部的节点
    void removeGhostNode(NodePtr node);
    void removeMainToGhost();
    void insertNodeToGhost(NodePtr node);
    void updateNodePosition(NodePtr node);

private:
    size_t capacity_;
    size_t ghostCapacity_;
    size_t transformThreshold_;
    //用于标记频率最小的链表节点，删除时直接定位
    //到该链表中删除。
    size_t minFreq_;
    std::mutex mutex_;

    NodeMap mainCache_;
    FreqMap freqMap_;

    NodeMap ghostCache_;
    NodePtr ghostHead_;
    NodePtr ghostTail_;
};

template<typename Key, typename Value>
void ArcLfuPart<Key, Value>::insertNodeToGhost(NodePtr node) {
    node->next_ = ghostTail_;
    node->prev_ = ghostTail_->prev_;
    ghostTail_->prev_->next_ = node;
    ghostTail_->prev_ = node;
}

template<typename Key, typename Value>
void ArcLfuPart<Key, Value>::removeGhostNode(NodePtr node) {
    node->prev_->next_ = node->next_;
    node->next_->prev_ = node->prev_;
}

template<typename Key, typename Value>
void ArcLfuPart<Key, Value>::removeMainToGhost() {
    if(freqMap_.empty()) return ;
    //获取最小频率的链表
    auto &minFreqList = freqMap_[minFreq_];
    if(minFreqList.empty()) return ;
    //保存好要删除的节点，删除的节点在头部
    NodePtr node = minFreqList.front();
    //删除
    minFreqList.pop_front();
    mainCache_.erase(node->getKey());

    if(minFreqList.empty()) {
        //如果删除后链表为空，则擦除掉这个频率
        freqMap_.erase(minFreq_);
        if(!freqMap_.empty()) {
            minFreq_ = freqMap_.begin()->first;
        }
    }

    //将移除的节点添加到ghost中。
    if(ghostCache_.size() >= ghostCapacity_) {
        //如果ghost满了，则删除最久没有访问的节点，
        //是按照先进先出的规则，这里采用尾部插入，
        //头部删除的方法。
        if(ghostHead_->next_ == ghostTail_) return ;
        ghostCache_.erase(ghostHead_->next_->getKey());
        removeGhostNode(ghostHead_->next_);
    }
    insertNodeToGhost(node);
    ghostCache_[node->getKey()] = node;
}

//注意：新来的节点放尾部，旧的节点越靠近头部，所以淘汰时是删除头部的节点
template<typename Key, typename Value>
void ArcLfuPart<Key, Value>::addNewNode(const Key &key, const Value &value) {
    if(mainCache_.size() >= capacity_) {
        removeMainToGhost(); 
    }
    //创建新节点。
    NodePtr newNode = std::make_shared<NodeType>(key, value);
    mainCache_[key] = newNode;
    //将新节点添加到频率为1的链表中
    if(freqMap_.find(1) == freqMap_.end()) {
        freqMap_[1] = std::list<NodePtr>();
    }
    freqMap_[1].push_back(newNode);
    minFreq_ = 1;
}

template<typename Key, typename Value>
void ArcLfuPart<Key, Value>::updateNodePosition(NodePtr node) {

    size_t oldFreq = node->getAccessCount();
    node->incrementAccessCount();
    size_t newFreq = node->getAccessCount();

    //获取旧频率的链表
    auto &oldList = freqMap_[oldFreq];
    //从这个链表中删除指定的节点
    oldList.remove(node);
    if(oldList.empty()) {
        freqMap_.erase(oldFreq);
        if(minFreq_ == oldFreq) {
            minFreq_ = newFreq;
        }
    }
    //添加到新位置
    if(freqMap_.find(newFreq) == freqMap_.end()) {
        //如果新频率的链表不存在，则创建
        freqMap_[newFreq] = std::list<NodePtr>();
    }
    freqMap_[newFreq].push_back(node);
}

template<typename Key, typename Value>
void ArcLfuPart<Key, Value>::put(Key key, Value value) {
    if(capacity_ == 0) return ;

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = mainCache_.find(key);
    if(it != mainCache_.end()) {
        //原来已经有了，就不用创建新的，直接更改value和更新位置即可。
        it->second->setValue(value);
        //更新节点位置
        updateNodePosition(it->second);
        return ;
    }
    //不存在，则创建新节点添加进去。
    addNewNode(key, value);
}

template<typename Key, typename Value>
bool ArcLfuPart<Key, Value>::get(Key key, Value &value) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = mainCache_.find(key);
    if(it != mainCache_.end()) {
        value = it->second->getValue();
        //更新节点位置
        updateNodePosition(it->second);
        return true;
    }
    return false;
}

template<typename Key, typename Value>
bool ArcLfuPart<Key, Value>::checkGhost(Key key) {
    auto it = ghostCache_.find(key);
    if(it != ghostCache_.end()) {
        //在ghost中，则从ghost中移除，添加到LRU中。
        removeGhostNode(it->second);
        ghostCache_.erase(it);
        return true;
    }
    return false;
}

template<typename Key, typename Value>
bool ArcLfuPart<Key, Value>::decreaseCapacity() {
    if(capacity_ == 0) return false;

    if(mainCache_.size() == capacity_) {
        //因为mainCache中满了，所以在减少容量时，
        //需要将节点从main中淘汰，移到ghost中，
        //而如果容量没满，是可以不用做啥操作的，
        //直接--capacity_即可。因为在增加节点时，
        //会通过满没满来判断是否需要淘汰。以达到
        //不超过容量
        removeMainToGhost();
    }
    --capacity_;
    return true;
}

}// namespace Cache
