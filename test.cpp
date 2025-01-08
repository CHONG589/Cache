#include <iostream>
#include <string>
#include <chrono>
#include <vector>
#include <iomanip>
#include <random>
#include <algorithm>

#include "LruCache.h"
#include "LfuCache.h"
#include "ArcCache/ArcCache.h"

void printResults(const std::string &testName, int capacity, 
                    const std::vector<int> &get_operations, 
                    const std::vector<int> &hits) {
        
    std::cout << "缓存大小: " << capacity << std::endl;

    std::cout << "LRU - 命中率: " << std::fixed << std::setprecision(2)
              << (100.0 * hits[0] / get_operations[0]) << "%" << std::endl;
    std::cout << "HASHLRU - 命中率: " << std::fixed << std::setprecision(2)
              << (100.0 * hits[1] / get_operations[1]) << "%" << std::endl;
    std::cout << "LFU - 命中率: " << std::fixed << std::setprecision(2)
              << (100.0 * hits[2] / get_operations[2]) << "%" << std::endl;
    std::cout << "HASHLFU - 命中率: " << std::fixed << std::setprecision(2)
              << (100.0 * hits[3] / get_operations[3]) << "%" << std::endl;
    std::cout << "ARC - 命中率: " << std::fixed << std::setprecision(2)
              << (100.0 * hits[4] / get_operations[4]) << "%" << std::endl;
}

void testHotDataAccess() {
    std::cout << "\n=== 测试场景1：热点数据访问测试 ===" << std::endl;

    // 缓存容量
    const int CAPACITY = 5;             
    // 操作次数
    const int OPERATIONS = 100000;
    const int HOT_KEYS = 3;
    const int COLD_KEYS = 5000;

    /**
     * 40%的操作访问热点数据，60%的操作访问冷数据。
     */

    Cache::LruCache<int, std::string> lru(CAPACITY);
    Cache::HashLruCaches<int, std::string> hashLru(CAPACITY);
    Cache::LfuCache<int, std::string> lfu(CAPACITY);
    Cache::HashLfuCaches<int, std::string> hashLfu(CAPACITY);
    Cache::ArcCache<int, std::string> arc(CAPACITY); 

    //它提供了一个非确定性的随机数生成器。 与伪随机数
    //生成器不同，它不依赖于种子，因此每次生成的随机
    //数都是真正的随机
    std::random_device rd;
    //mt19937类是一个随机数引擎，可以生成高质量的伪随
    //机数序列，mt19937(unsigned int seed)：使用指定
    //的种子初始化随机数引擎
    std::mt19937 gen(rd());   

    //3个类型为 CachePolicy<int, std::string>* 的数据
    std::array<Cache::CachePolicy<int, std::string>*, 5> caches = {&lru, &hashLru, &lfu, &hashLfu, &arc};
    //hits被初始化为3个为0的数据
    std::vector<int> hits(5, 0);
    std::vector<int> get_operations(5, 0);

    for(size_t i = 0; i < caches.size(); ++i) {
        for(int op = 0; op < OPERATIONS; ++op) {
            int key;
            if(op % 100 < 40) {
                //40%的机会访问热点数据
                //生成热点数据的key，热点数据的key范围为[0, 2]，共3个，
                //所以无论怎么生成都是这三个。
                key = gen() % HOT_KEYS;
            }
            else {
                //冷数据的key，范围为[3, 5002]，共5000个。
                key = HOT_KEYS + (gen() % COLD_KEYS);
            }
            //将对应的key和value放入缓存中。
            std::string value = "value" + std::to_string(key);
            caches[i]->put(key, value);
        }

        //然后进行随机get操作
        for(int get_op = 0; get_op < OPERATIONS / 2; ++get_op) {
            int key;
            if(get_op % 100 < 40) {
                //40%概率访问热点
                key = gen() % HOT_KEYS;
            }
            else {
                key = HOT_KEYS + (gen() % COLD_KEYS);
            }

            std::string result;
            get_operations[i]++;
            if(caches[i]->get(key, result)) {
                hits[i]++;
            }
        }
    }
    printResults("热点数据访问测试", CAPACITY, get_operations, hits);
}

void testLoopPattern() {
    std::cout << "\n=== 测试场景2：循环扫描测试 ===" << std::endl;
    
    const int CAPACITY = 3;            
    const int LOOP_SIZE = 200;         
    const int OPERATIONS = 50000;      
    
    Cache::LruCache<int, std::string> lru(CAPACITY);
    Cache::HashLruCaches<int, std::string> hashLru(CAPACITY);
    Cache::LfuCache<int, std::string> lfu(CAPACITY);
    Cache::HashLfuCaches<int, std::string> hashLfu(CAPACITY);
    Cache::ArcCache<int, std::string> arc(CAPACITY);

    std::array<Cache::CachePolicy<int, std::string>*, 5> caches = {&lru, &hashLru, &lfu, &hashLfu, &arc};
    std::vector<int> hits(5, 0);
    std::vector<int> get_operations(5, 0);

    std::random_device rd;
    std::mt19937 gen(rd());

    // 先填充数据
    for (size_t i = 0; i < caches.size(); ++i) {
        for (int key = 0; key < LOOP_SIZE * 2; ++key) {
            std::string value = "loop" + std::to_string(key);
            caches[i]->put(key, value);
        }
        
        // 然后进行访问测试
        int current_pos = 0;
        for (int op = 0; op < OPERATIONS; ++op) {
            int key;
            if (op % 100 < 70) {  // 70%顺序扫描
                key = current_pos;
                current_pos = (current_pos + 1) % LOOP_SIZE;
            } else if (op % 100 < 85) {  // 15%随机跳跃
                key = gen() % LOOP_SIZE;
            } else {  // 15%访问范围外数据
                key = LOOP_SIZE + (gen() % LOOP_SIZE);
            }
            
            std::string result;
            get_operations[i]++;
            if (caches[i]->get(key, result)) {
                hits[i]++;
            }
        }
    }

    printResults("循环扫描测试", CAPACITY, get_operations, hits);
}

void testWorkLoadShift() {
    std::cout << "\n=== 测试场景3：工作负载剧烈变化测试 ===" << std::endl;
    
    const int CAPACITY = 4;            
    const int OPERATIONS = 80000;      
    const int PHASE_LENGTH = OPERATIONS / 5;
    
    Cache::LruCache<int, std::string> lru(CAPACITY);
    Cache::HashLruCaches<int, std::string> hashLru(CAPACITY);
    Cache::LfuCache<int, std::string> lfu(CAPACITY);
    Cache::HashLfuCaches<int, std::string> hashLfu(CAPACITY);
    Cache::ArcCache<int, std::string> arc(CAPACITY);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::array<Cache::CachePolicy<int, std::string>*, 5> caches = {&lru, &hashLru, &lfu, &hashLfu, &arc};
    std::vector<int> hits(5, 0);
    std::vector<int> get_operations(5, 0);

    // 先填充一些初始数据
    for (size_t i = 0; i < caches.size(); ++i) {
        for (int key = 0; key < 1000; ++key) {
            std::string value = "init" + std::to_string(key);
            caches[i]->put(key, value);
        }
        
        // 然后进行多阶段测试
        for (int op = 0; op < OPERATIONS; ++op) {
            int key;
            // 根据不同阶段选择不同的访问模式
            if (op < PHASE_LENGTH) {  // 热点访问
                key = gen() % 5;
            } else if (op < PHASE_LENGTH * 2) {  // 大范围随机
                key = gen() % 1000;
            } else if (op < PHASE_LENGTH * 3) {  // 顺序扫描
                key = (op - PHASE_LENGTH * 2) % 100;
            } else if (op < PHASE_LENGTH * 4) {  // 局部性随机
                int locality = (op / 1000) % 10;
                key = locality * 20 + (gen() % 20);
            } else {  // 混合访问
                int r = gen() % 100;
                if (r < 30) {
                    key = gen() % 5;
                } else if (r < 60) {
                    key = 5 + (gen() % 95);
                } else {
                    key = 100 + (gen() % 900);
                }
            }
            
            std::string result;
            get_operations[i]++;
            if (caches[i]->get(key, result)) {
                hits[i]++;
            }
            
            // 随机进行put操作，更新缓存内容
            if (gen() % 100 < 30) {  // 30%概率进行put
                std::string value = "new" + std::to_string(key);
                caches[i]->put(key, value);
            }
        }
    }

    printResults("工作负载剧烈变化测试", CAPACITY, get_operations, hits);
}

int main() {

    testHotDataAccess();
    testLoopPattern();
    testWorkLoadShift();

    return 0;
}
