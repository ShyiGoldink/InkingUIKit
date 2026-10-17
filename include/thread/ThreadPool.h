#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace inking {

/** 默认管家线程数量。UI 侧的活主要是 IO/解码，开太多线程反而互相抢。 */
inline constexpr std::size_t kDefaultThreadCount = 4;

/**
 * @brief 受限线程池，结构移植自 InkingBackendFramework 的 ThreadPool。
 *
 * 与后端版本的差异：
 *   1. 不继承后端的模块自检基类（自检体系没有移植过来）；
 *   2. 线程数量从固定的编译期常量改成 init() 的参数，默认 kDefaultThreadCount。
 *
 * 用法是"注入式"的：调用方通过 init() 注入三个回调——
 *   predicate 决定"现在有没有活"，execute 决定"有活时干什么"，
 *   waitHint 决定"没活时最多睡多久"（延迟任务靠它超时醒来）。
 * 线程池本身不认识任务是什么，认识任务的是 TaskQueue。
 */
class ThreadPool {
public:
    ThreadPool() = default;
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    /**
     * @brief 初始化并启动管家线程。
     * @param predicate 返回 true 表示现在有活可干。
     * @param execute   有活时执行的函数。
     * @param waitHint  可选。返回"最多还能睡多久"，用于支持延迟任务：
     *                  延迟任务到点时没有人会来 notify，只能靠超时醒来。
     * @param threadCount 管家线程数量。
     * @note 重复调用会被忽略，避免覆盖已经在跑的线程对象导致无人 join。
     */
    void init(std::function<bool()> predicate,
              std::function<void()> execute,
              std::function<std::chrono::milliseconds()> waitHint = {},
              std::size_t threadCount = kDefaultThreadCount);

    /** 请求退出并唤醒全部管家线程；真正的 join 在析构里做。 */
    void quit();

    /**
     * @brief 唤醒一个正在等待的管家线程（例如新任务入队后调用）。
     *
     * 一条任务只需要一个执行者，所以只叫醒一个：以前用 notify_all 时，
     * 每次入队都会把全部线程叫醒、其中若干个发现没活干再睡回去，
     * 任务一密集，CPU 就烧在调度上了。
     */
    void wake();

    /** 唤醒全部管家线程：只在退出这类"必须让每个线程都重新看一眼"的场合调用。 */
    void wakeAll();

    /** 线程池是否正在运行（已 init 且未 quit）。 */
    bool isRunning() const noexcept;

private:
    /** 标记退出并 join 全部管家线程。 */
    void release();
    /** 管家线程的主循环：先判断有没有活，再决定干还是睡。 */
    void butler();
    /** 执行一次注入的执行体，并兜住异常。 */
    void executeGuarded();

    std::vector<std::unique_ptr<std::thread>> _threads; /** 管家线程 */
    mutable std::mutex _mutex;                          /** 保护条件变量与回调 */
    std::condition_variable _cv;                        /** 全池共用一个条件变量 */
    std::function<bool()> _predicateCallback;           /** 有没有活 */
    std::function<void()> _executeCallback;             /** 干什么 */
    std::function<std::chrono::milliseconds()> _waitHintCallback; /** 最多睡多久 */
    std::atomic<bool> _isQuit{false};                   /** 退出标志 */
    std::atomic<bool> _isRunning{false};                /** 是否已启动 */
};

}  // namespace inking

