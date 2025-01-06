#pragma once

#include <cmath>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>
#include <map>
#include <algorithm>

#include "CachePolicy.h"

namespace Cache {

template<typename Key, typename Value>
class LfuNode {
public:

    LfuNode() : freq_(1) {}
    LfuNode(Key key, Value value)
            : freq_(1), key_(key)
            , value_(value)  {}

    Key getKey() const { return key_; }
    Value getValue() const { return value_; }
    void setValue(const Value &value) { value_ = value;}
    int getFreq() const { return freq_; }
    void incFreq() { ++freq_; }
    void setFreq(int freq) { freq_ = freq; }

private:
    int freq_;
    Key key_;
    Value value_;
};

template<typename Key, typename Value>
class LfuCache : public CachePolicy<Key, Value> {
public:
    using NodeType = LfuNode<Key, Value>;
    using NodePtr = std::shared_ptr<NodeType>;
    using NodeMap = std::unordered_map<Key, NodePtr>;
    using FreqMap = std::map<int, std::list<NodePtr>>;

    LfuCache(int capacity, int maxAverageNum = 10)
            : capacity_(capacity)
            , minFreq_(1)
            , maxAverageNum_(maxAverageNum)
            , curAverageNum_(0)
            , curTotalNum_(0) {}

    ~LfuCache() override = default;

    void put(Key key, Value value) override;
    bool get(Key key, Value &value) override;
    Value get(Key key) override;

private:
    void updateNodePos(NodePtr node);
    void updateAveAndTotalFreqNum();
    void addNewNode(const Key &key, const Value &value);

private:
    size_t capacity_;
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
    FreqMap freqMap_;
};

template<typename Key, typename Value>
void LfuCache<Key, Value>::addNewNode(const Key &key, const Value &value) {
    if(nodeMap_.size() >= capacity_) {
        //缓存已满，需要淘汰一个节点，淘汰从表头删除
        auto node = freqMap_[minFreq_].front();
        nodeMap_.erase(node->getKey());
        freqMap_[minFreq_].pop_front();

        //更新当前总访问频率和平均访问频率
        curTotalNum_ -= minFreq_;
        if(nodeMap_.empty())
            curAverageNum_ = 0;
        else
            curAverageNum_ = curTotalNum_ / nodeMap_.size();
        
        if(freqMap_[minFreq_].empty()) {
            freqMap_.erase(minFreq_);
            //更新最小频率
            // minFreq_ = INT8_MAX;
            // for(const auto &pair : freqMap_) {
            //     if(pair.first < minFreq_)
            //         minFreq_ = pair.first;
            // }
            // if(minFreq_ == INT8_MAX) minFreq_ = 1;
            minFreq_ = freqMap_.begin()->first;
        }
    }

    //创建新节点，并加入到缓存中，更新最小访问频率
    NodePtr node = std::make_shared<NodeType>(key, value);
    nodeMap_[key] = node;
    if(freqMap_.find(1) == freqMap_.end()) {
        freqMap_[1] = std::list<NodePtr>();
    }
    freqMap_[1].push_back(node);
    minFreq_ = 1;

    updateAveAndTotalFreqNum();
}

template<typename Key, typename Value>
void LfuCache<Key, Value>::updateNodePos(NodePtr node) {

    int oldFreq = node->getFreq();
    node->incFreq();
    int newFreq = node->getFreq();

    auto &oldList = freqMap_[oldFreq];
    oldList.remove(node);
    if(oldList.empty()) {
        freqMap_.erase(oldFreq);
        if(minFreq_ == oldFreq) {
            minFreq_ = newFreq;
        }
    }

    if(freqMap_.find(newFreq) == freqMap_.end()) {
        freqMap_[newFreq] = std::list<NodePtr>();
    }
    freqMap_[newFreq].push_back(node);

    //总访问频率数和当前平均访问频率数都要更新。
    updateAveAndTotalFreqNum();
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
            int oldFreq = node->getFreq();
            int newFreq = node->getFreq() - (maxAverageNum_ / 2);
            if(newFreq < 1) newFreq = 1;
            node->setFreq(newFreq);
            auto &oldList = freqMap_[oldFreq];

            //先从当前频率链表中移除
            oldList.remove(node);
            if(oldList.empty()) {
                freqMap_.erase(oldFreq);
            }

            //添加到新位置。
            if(freqMap_.find(newFreq) == freqMap_.end()) {
                freqMap_[newFreq] = std::list<NodePtr>();
            }
            freqMap_[newFreq].push_back(node);
        }

        //更新最小频率
        // minFreq_ = INT8_MAX;
        // for(const auto &pair : freqMap_) {
        //     if(pair.first < minFreq_)
        //         minFreq_ = pair.first;
        // }
        // if(minFreq_ == INT8_MAX) minFreq_ = 1;
        minFreq_ = freqMap_.begin()->first;
    }
}

template<typename Key, typename Value>
void LfuCache<Key, Value>::put(Key key, Value value) {
    if(capacity_ == 0) return ;

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = nodeMap_.find(key);
    if(it != nodeMap_.end()) {
        //如果缓存中已经有这个 key，则更新其 value 
        it->second->setValue(value);
        //因为访问了，频率变化，要调整位置
        updateNodePos(it->second);
        return ;
    }
    addNewNode(key, value);
}

template<typename Key, typename Value>
bool LfuCache<Key, Value>::get(Key key, Value &value) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = nodeMap_.find(key);
    if(it != nodeMap_.end()) {
        value = it->second->getValue();
        updateNodePos(it->second);
        return true;
    }
    return false;
}

template<typename Key, typename Value>
Value LfuCache<Key, Value>::get(Key key) {
    Value value{};
    get(key, value);
    return value;
}

template<typename Key, typename Value>
class HashLfuCaches : public CachePolicy<Key, Value> {
public:
    HashLfuCaches(size_t capacity, int sliceNum = std::thread::hardware_concurrency(), int maxAverageNum = 10)
                : sliceNum_(sliceNum)
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
