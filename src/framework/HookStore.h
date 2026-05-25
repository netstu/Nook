#ifndef JAVAHOOK_HOOKSTORE_H
#define JAVAHOOK_HOOKSTORE_H

#include <vector>
#include <mutex>
#include <unordered_map>
#include <shared_mutex>

// 线程安全的 Hook 信息存储
// 使用 deque 而不是 vector，避免元素移动导致引用失效
template <typename T>
class HookStore {
public:
    static HookStore<T>& Instance() {
        static HookStore<T> instance;
        return instance;
    }

    void Add(const T& item, size_t index) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (index >= data_.size()) {
            data_.resize(index + 1);
        }
        data_[index] = item;
    }

    std::vector<T> GetAll() {
        std::unique_lock<std::mutex> lock(mutex_);
        return data_;
    }

    // 危险：返回引用，调用者必须在持有锁期间使用完
    // 一般情况应该使用 CopyByIndex() 代替
    T& Get(size_t index) {
        // 注意：此函数返回引用，但锁在函数返回后立即释放
        // 如果调用者保存这个引用，后续可能失效
        return data_.at(index);
    }

    // 线程安全的值拷贝 - 推荐使用
    T CopyByIndex(size_t index) {
        std::unique_lock<std::mutex> lock(mutex_);
        return data_.at(index);
    }

    size_t Size() {
        std::unique_lock<std::mutex> lock(mutex_);
        return data_.size();
    }

    // 外部已经持有 mutex_ 时使用，避免同线程重复加锁。
    size_t SizeUnsafe() const {
        return data_.size();
    }

    void Clear() {
        std::unique_lock<std::mutex> lock(mutex_);
        data_.clear();
    }

    // 直接获取引用（需要外部持有 store_mutex_）
    // 用于需要长时间持有锁的场景
    T& GetUnsafe(size_t index) {
        return data_.at(index);
    }

    // 获取内部锁的引用（用于复杂的原子操作）
    std::mutex& GetMutex() {
        return mutex_;
    }

private:
    std::vector<T> data_;
    std::mutex mutex_;

    HookStore() = default;
    HookStore(const HookStore&) = delete;
    HookStore& operator=(const HookStore&) = delete;
};

// Hook ID 锁管理器
class HookIdLockManager {
public:
    static HookIdLockManager& Instance() {
        static HookIdLockManager instance;
        return instance;
    }

    // 获取指定 hookID 的锁（必须尽快使用，不要长时间持有）
    std::mutex& GetMutex(uint32_t hookID) {
        // 注意：这里有一个潜在问题
        // mutex_map_[hookID] 可能会重新分配，导致返回的引用失效
        // 但由于 mutex_map_ 是 map 类型，insert/erase 不会使其他元素的引用失效
        // 所以这是安全的
        std::lock_guard<std::mutex> lock(map_mutex_);
        return mutex_map_[hookID];
    }

private:
    std::unordered_map<uint32_t, std::mutex> mutex_map_;
    std::mutex map_mutex_;

    HookIdLockManager() = default;
    HookIdLockManager(const HookIdLockManager&) = delete;
    HookIdLockManager& operator=(const HookIdLockManager&) = delete;
};

#endif // JAVAHOOK_HOOKSTORE_H
