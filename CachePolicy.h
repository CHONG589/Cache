#pragma once

namespace Cache {

template <typename Key, typename Value>
class CachePolicy {
public:
    // 析构时，根据具体的对象类型，用它们自己的析构函数，所以这里要定义
    // 一个接口，让子类实现自己的析构函数。
    virtual ~CachePolicy() {};

    /**
     * 下面的 =0 表示纯虚函数
     * 1. 纯虚函数没有函数体；
     * 2. 这个类就是一个抽象类
     * 3. 抽象类不能具体实例化（不能创建它的对象），而只能由它去派生子类。
     *    但是可以用这个抽象类的指针去接收派生类的对象。
     * 4. 在派生类中对此函数提供定义后，它才能具备函数的功能，可被调用。
     */

    virtual void put(Key key, Value value) = 0;

    //获取到的值用 value 接收，所以要用到引用。而不是用 Value value 接收，
    //这样就是在用临时对象 value 接收，会接收不到。
    virtual bool get(Key key, Value &value) = 0;

    virtual Value get(Key key) = 0;
};

} // namespace Cache
