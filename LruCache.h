#pragma once

#include <mutex>
#include <unordered_map>
#include <cstring>
#include <list>
#include <memory>
#include <vector>

#include "CachePolicy.h"

namespace Cache {

template<typename Key, typename Value> class LruCache;

template<typename Key, typename Value>
class LruNode {
public:
    friend class LruCache<Key, Value>;

    LruNode(Key key, Value value)
        : key_(key)
        , value_(value)
        , accessCount_(1)
        , prev_(nullptr)
        , next_(nullptr) {}

    //提供必要的访问器，因为其它类需要访问这个类的私有成员时，
    //可以用公共接口来访问。
    Key getKey() const {return key_; }
    Value getValue() const {return value_; }
    void setValue(const Value &value) {value_ = value; }
    size_t getAccessCount() const {return accessCount_; }
    void incAccessCount() {++accessCount_; }

private:
    Key key_;
    Value value_;
    size_t accessCount_;
    std::shared_ptr<LruNode<Key, Value>> prev_;
    std::shared_ptr<LruNode<Key, Value>> next_;
};

template<typename Key, typename Value>
class LruCache : public CachePolicy<Key, Value> {
public:
    using LruNodeType = LruNode<Key, Value>;
    using NodePtr = std::shared_ptr<LruNodeType>;
    using NodeMap = std::unordered_map<Key, NodePtr>;

    LruCache(int capacity) : capacity_(capacity) {
        //私有成员中只是定义了头尾节点的指针，还没有分配内存，所以
        //构造时要替它们分配内存，并将它们串成一个环，因为是双向链表。
        dummyHead_ = std::make_shared<LruNodeType>(Key(), Value());
        dummyTail_ = std::make_shared<LruNodeType>(Key(), Value());
        dummyHead_->next_ = dummyTail_;
        dummyTail_->prev_ = dummyHead_;
    }

    ~LruCache() override = default;

    void put(Key key, Value value) override {
        if(capacity_ <= 0) return ;

        std::lock_guard<std::mutex> lock(mutex_);
        auto it = nodeMap_.find(key);
        if(it != nodeMap_.end()) {
            //如果已经存在这个节点，则更新它的value和将它移到表尾
            it->second->setValue(value);
            //将节点移到最新的位置
            removeNode(it->second);
            insertNode(it->second);
            return ;
        }

        //如果不存在这个节点，则创建它，并插入到最近访问的位置，即链尾。
        addNewNode(key, value);
    }

    bool get(Key key, Value &value) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = nodeMap_.find(key);
        if(it != nodeMap_.end()) {
            //如果找到了这个节点，则要更新它的位置，移到表尾。表示刚刚
            //被访问过，所以应该排在表尾。
            removeNode(it->second);
            insertNode(it->second);
            //返回它的value。
            value = it->second->getValue();
            return true;
        }
        //没有找到。
        return false;
    }

    Value get(Key key) override {
        Value value{};
        get(key, value);
        return value;
    }

    void remove(Key key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = nodeMap_.find(key);
        if(it != nodeMap_.end()) {
            removeNode(it->second);
            nodeMap_.erase(it);
        }
    }

private:
    void removeNode(NodePtr node) {
        node->prev_->next_ = node->next_;
        node->next_->prev_ = node->prev_;
    }

    void insertNode(NodePtr node) {
        node->next_ = dummyTail_;
        node->prev_ = dummyTail_->prev_;
        dummyTail_->prev_->next_ = node;
        dummyTail_->prev_ = node;
    }

    void addNewNode(const Key &key, const Value &value) {
        if(nodeMap_.size() >= capacity_) {
            //如果缓存已满，则先删除最近最少访问的节点，即链表节点。
            removeNode(dummyHead_->next_);
            //也要从哈希表中删除对应的键值对。
            nodeMap_.erase(dummyHead_->next_->getKey());
        }
        //创建新节点，并插入到链尾。
        NodePtr newNode = std::make_shared<LruNodeType>(key, value);
        insertNode(newNode);
        nodeMap_[key] = newNode;
    }

private:
    int capacity_;
    //哈希表<key, Node>
    NodeMap nodeMap_;
    //访问缓存要互斥
    std::mutex mutex_;
    //虚拟头节点和虚拟尾节点
    NodePtr dummyHead_;
    NodePtr dummyTail_;
};

//LRU优化：LRU-K 版本，通过继承的方法进行再优化。
//这个派生类多了一个历史缓存，只有当访问次数超过 k 次时才会进入 LRU 
//的缓冲中。
template<typename Key, typename Value>
class LruKCache : public LruCache<Key, Value> {
public:
    LruKCache(int capacity, int historyCapacity, int k)
                : LruCache<Key, Value>(capacity)
                , historyList_(std::make_unique<LruCache<Key, size_t>>(historyCapacity))
                , k_(k) {}

    Value get(Key key) {
        //调用父类LRU的 get 方法获取历史缓存对应key的值，它的值就是
        //访问次数。
        int historyCount = historyList_->get(key);
        //历史缓存是按照LRU的规则进行淘汰的，所以对历史缓存的操作直接用
        //父类 LRU 的方法即可。
        historyList_->put(key, ++historyCount);

        return LruCache<Key, Value>::get(key);
    }

    void put(Key key, Value value) {
        //先判断是否存在缓存（不是指历史缓存）中，如果已经存在，则
        //直接覆盖，不用经过历史缓存。
        if(LruCache<Key, Value>::get(key) != "")
            LruCache<Key, Value>::put(key, value);
        
        //不在缓存中，则加入到历史缓存中
        int historyCount = historyList_->get(key);
        historyList_->put(key, ++historyCount);

        //判断是否要进入缓存中
        if(historyCount >= k_) {
            //先从历史缓存中移除
            historyList_->remove(key);
            //再加入到 LRU 缓存中
            LruCache<Key, Value>::put(key, value);
        }
    }

private:
    //进入缓存队列的评判标准
    int k_;
    //historyList_ 也是 LRU 缓存，不过它的 value 是访问次数。
    std::unique_ptr<LruCache<Key, size_t>> historyList_;
};

//LRU优化：对LRU进行分片，提高并发使用的性能
template<typename Key, typename Value>
class HashLruCaches {
public:
    //hardware_concurrenty这个函数将返回能同时并发在一个程序中的线程数量
    HashLruCaches(size_t capacity, int sliceNum)
                    : capacity_(capacity)
                    , sliceNum_(sliceNum > 0 ? sliceNum : std::thread::hardware_concurrenty()) {
        
        //获取每个分片的大小
        size_t sliceSize = std::ceil(capacity / static_cast<double>(sliceNum_));
        for (int i = 0; i < sliceNum_; ++i) {
            //将创建的每个分片缓存放入lruSliceCaches_中
            lruSliceCaches_.emplace_back(new LruCache<Key, Value>(sliceSize));
        }
    }

    void put(Key key, Value value) {
        //根据key的hash值，将key分配到对应的分片缓存中
        size_t sliceIndex = Hash(key) % sliceNum_;
        return lruSliceCaches_[sliceIndex]->put(key, value);
    }

    bool get(Key key, Value &value) {
        size_t sliceIndex = Hash(key) % sliceNum_;
        return lruSliceCaches_[sliceIndex]->get(key, value);
    }

    Value get(Key key) {
        Value value;
        memset(&value, 0, sizeof(value));
        get(key, value);
        return value;
    }

private:
    size_t Hash(Key key) {
        std::hash<Key> hashFunc;
        return hashFunc(key);
    }

private:
    size_t capacity_;
    //切片数量
    int sliceNum_;
    //切片LRU缓存
    //每个元素都是一个指向LRU缓存的unique_ptr指针
    std::vector<std::unique_ptr<LruCache<Key, Value>>> lruSliceCaches_;
};

}// namespace Cache
