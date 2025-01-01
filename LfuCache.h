#pragma once

#include <cmath>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "CachePolicy.h"

namespace Cache {

template<typename Key, typename Value> class LfuCache;

template<typename Key, typename Value>
class FreqList {
private:
    struct Node {
        int freq;
        Key key;
        Value value;
        std::shared_ptr<Node> pre;
        std::shared_ptr<Node> next;

        Node() : freq(1), pre(nullptr), next(nullptr) {}
        Node(Key key, Value value)
        : freq(1), key(key), value(value), pre(nullptr), next(nullptr) {}

        using NodePtr = std::shared_ptr<Node>;
        int freq_;
        //在对应频率下的链表中有头节点和尾节点，这样方便删除和添加节点，
        //如越靠近尾部，则越久没有被访问，如果缓存已满，只要将最小频率
        //下的链表的尾节点 tail->pre 指向的节点删除即可。
        NodePtr head_;
        NodePtr tail_;
    };

public:
    friend class LfuCache<Key, Value>;

    explicit FreqList(int n) : freq_(n) {
        head_ = std::make_shared<Node>();
        tail_ = std::make_shared<Node>();
        head_->next = tail_;
        tail->pre = head_;
    }

    bool isEmptr() const { return head_next_ == tail_; }

    //在头部增加节点
    void addNode(NodePtr node) {
        if(!node || !head_ || !tail_) return ;

        node->next = head_->next;
        node->pre = head_;
        head->next->pre = node;
        head->next = node;
    }

    //删除指定的节点
    void removeNode(NodePtr node) {
        if(!node || !head_ || !tail_) return ;
        if(!node->pre || !node->next) return ;

        node->pre->next = node->next;
        node->next->pre = node->pre;
        //由于node是shared_ptr，所以只需要将它的pre和next
        //置空即可，不用自己释放node节点，如果没有引用，
        //节点会自动被释放。
        node->pre = nullptr;
        node->next = nullptr;
    }
};

template<typename Key, typename Value>
class LfuCache : public CachePolicy<Key, Value> {
public:
    //下面这一句表示 Node 是 FreqList<Key, Value>::Node，
    //typename 是关键字，表示类型名称。
    using Node = typename FreqList<Key, Value>::Node;
    using NodePtr = std::shared_ptr<Node>;
    using NodeMap = std::unordered_map<Key, NodePtr>;

    LfuCache(int capacity, int maxAverageNum = 10)
            : capacity_(capacity)
            , minFreq_(1)
            , maxAverageNum_(maxAverageNum)
            , curAverageNum_(0)
            , curTotalNum_(0) {}

    ~LfuCache() override = default;

    void put(Key key, Value value) override {
        if(capacity_ == 0) return ;

        std::lock_guard<std::mutex> lock(mutex_);
        auto it = nodeMap_.find(key);
        if(it != nodeMap_.end()) {
            //如果缓存中已经有这个 key，则更新其 value 
            it->second->value = value;
            //因为访问了，频率变化，要调整位置
            updateNodePos(it->second);
            return ;
        }

        //如果缓存中没有这个 key，则加入这个节点
        if(nodeMap_.size() >= capacity_) {
            //缓存已满，需要淘汰一个节点，淘汰从链表尾部淘汰
            freqToFreqList_[minFreq_]->removeNode(tail->pre);
            nodeMap_.erase(tail->pre->key);

            //更新当前总访问频率和平均访问频率
            curTotalNum_ -= minFreq_;
            if(nodeMap_.empty())
                curAverageNum_ = 0;
            else
                curAverageNum_ = curTotalNum_ / nodeMap_.size();
        }

        //创建新节点，并加入到缓存中，更新最小访问频率
        NodePtr node = std::make_shared<Node>(key, value);
        nodeMap_[key] = node;
        addToFreqList(node);
        updateAveAndTotalFreqNum();
    }

    bool get(Key key, Value &value) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = nodeMap_.find(key);
        if(it != nodeMap_.end()) {
            value = it->second->value;
            updateNodePos(it->second);
            return true;
        }
        return false;
    }

    Value get(Key key) override {
        Value value;
        get(key, value);
        return value;
    }

    void removeCache() {
        nodeMap_.clear();
        freqToFreqList_.clear();
    }

private:
    void updateNodePos(NodePtr node);
    void removeFromFreqList(NodePtr node);
    void addToFreqList(NodePtr node);
    void updateAveAndTotalFreqNum();

private:
    int capacity_;
    //最小访问频次（用于找到最小访问频次节点）
    int minFreq_;
    //最大平均访问频次
    int maxAverageNum_;
    //当前平均访问频次
    int curAverageNum_;
    //当前访问所有缓存次数总数
    int curTotalNum_;
    std::mutex mutex_;
    //key 到缓存节点的映射
    NodeMap nodeMap_;
    //访问频数到该频数链表的映射
    std::unordered_map<int, FreqList<Key, Value>*> freqToFreqList_;
};

template<typename Key, typename Value>
void LfuCache<Key, Value>::updateNodePos(NodePtr node) {
    //先从原来的访问频率链表中删除该节点
    removeFromFreqList(node);
    //更新访问频率
    node->freq++;
    //添加到新频率链表中
    addToFreqList(node);

    //如果在增加频率前，这个节点的频率属于最小频率，且这个节点是那个
    //链表中的最后一个节点，则需要更新最小频率
    if(node->freq - 1 == minFreq_ && freqToFreqList_[node->freq - 1]->isEmpty())
        minFreq_++;

    //总访问频率数和当前平均访问频率数都要更新。
    updateAveAndTotalFreqNum();
}

template<typename Key, typename Value>
void LfuCache<Key, Value>::removeFromFreqList(NodePtr node) {
    if(!node) return ;

    auto freq = node->freq;
    freqToFreqList_[freq]->removeNode(node);
}

template<typename Key, typename Value>
void LfuCache<Key, Value>::addToFreqList(NodePtr node) {
    if(!node) return ;

    auto freq = node->freq;
    if(freqToFreqList_.find(freq) == freqToFreqList_.end()) {
        //不存在则创建
        freqToFreqList_[freq] = new FreqList<Key, Value>(freq);
    }
    //添加到链表的头部
    freqToFreqList_[freq]->addNode(node);
}

template<typename Key, typename Value>
void LfuCache<Key, Value>::updateAveAndTotalFreqNum() {
    curTotalNum_++;
    if(nodeMap_.empty())
        curAverageNum_ = 0;
    else
        curAverageNum_ = curTotalNum_ / nodeMap_.size();
    
    if(curAverageNum_ > maxAverageNum_) {
        //当前平均访问频率大于最大平均访问频率，则需要进行老化数据处理，防止
        //频率很大的数据现在不常访问了，但是现在常访问的数据频率因为低于它导致
        //淘汰不了它，长期占据内存，所以需要对那些数据进行老化处理。
        if(nodeMap_.empty()) return ;

        //所有节点的频率减去（maxAverageNum_ / 2)
        for(auto it = nodeMap_.begin(); it != nodeMap_.end(); ++it) {
            if(!it->second) continue;

            NodePtr node = it->second;
            //先从当前频率链表中移除
            removeFromFreqList(node);
            node->freq -= (maxAverageNum_ / 2);
            if(node->freq < 1) node->freq = 1;
            addToFreqList(node);
        }

        //更新最小频率
        minFreq_ = INT8_MAX;
        for(const auto &pair : freqToFreqList_) {
            if(pair.second && ! !pair.second->isEmpty()) 
                minFreq_ = std::min(minFreq_, pair.first);
        }
        if(minFreq_ == INT8_MAX) minFreq_ = 1;
    }
}

template<typename Key, typename Value>
class HashLfuCache {
public:
    HashLfuCache(size_t capacity, int sliceNum, int maxAverageNum = 10)
                : sliceNum_(sliceNum > 0 ? sliceNum : std::thread::hardware_concurrency())
                , capacity_(capacity) {
        
        size_t sliceSize = std::ceil(capacity_ / static_cast<double>(sliceNum));
        for(int i = 0; i < sliceNum_; ++i) 
            lfuSliceCaches_.emplace_back(new LfuCache<Key, Value>(sliceSize, maxAverageNum));
    }

    void put(Key key, Value value) {
        size_t sliceIndex = Hash(key) % sliceNum_;
        return lfuSliceCaches_[sliceIndex]->put(key, value);
    }

    bool get(Key key, Value &value) {
        size_t sliceIndex = Hash(key) % sliceNum_;
        return lfuSliceCaches_[sliceIndex]->get(key, value);
    }

    Value get(Key key) {
        Value value;
        get(key, value);
        return value;
    }

    void removeCache() {
        for(auto &cache : lfuSliceCaches_)
            cache->removeCache();
    }

private:
    size_t Hash(Key key) {
        std::hash<Key> hashFunc;
        return hashFunc(key);
    }

private:
    size_t capacity_;
    int sliceNum_;
    //缓存分片容器
    std::vector<std::unique_ptr<LfuCache<Key, Value>>> lfuSliceCaches_;
};

}// namespace Cache
