#include <thread/ThreadPool.h>

#include <detail/InkLog.h>

#include <exception>
#include <string>
#include <utility>

namespace {

constexpr const char* kModuleName = "ThreadPool";

}  // namespace

namespace inking {

ThreadPool::~ThreadPool() {
    release();
}

void ThreadPool::init(std::function<bool()> predicate,
                      std::function<void()> execute,
                      std::function<std::chrono::milliseconds()> waitHint,
                      std::size_t threadCount) {
    // 线程对象一旦跑起来就会在析构里被 join，重复初始化会把它们覆盖掉
    // （那些线程再也没人 join，析构时直接 terminate）。这里挡掉第二次。
    if (!_threads.empty()) {
        INK_LOG_WARN(kModuleName, "线程池已经初始化过，忽略这次重复初始化");
        return;
    }

    if (threadCount == 0) {
        threadCount = kDefaultThreadCount;
    }

    if (predicate) {
        _predicateCallback = std::move(predicate);
    }
    if (execute) {
        _executeCallback = std::move(execute);
    }
    if (waitHint) {
        _waitHintCallback = std::move(waitHint);
    }

    _isQuit = false;
    _threads.reserve(threadCount);
    for (std::size_t i = 0; i < threadCount; ++i) {
        _threads.push_back(std::make_unique<std::thread>(&ThreadPool::butler, this));
    }
    _isRunning = true;

    INK_LOG_PASS(kModuleName, "线程池创建完成，管家线程数=" + std::to_string(threadCount));
}

void ThreadPool::butler() {
    if (!_predicateCallback || !_executeCallback) {
        INK_LOG_ERROR(kModuleName, "未成功传入谓词函数或执行函数，管家线程退出");
        return;
    }
    if (_isQuit) {
        return;
    }

    while (true) {
        // 每一轮都在锁内先看一次"有没有活"，再决定睡不睡。
        //
        // 这个顺序不只是为了少睡一次：谓词读的是别处的状态（比如任务队列），
        // 保护它的锁和这把 _mutex 不是同一把。通知方 wake()/wakeAll() 会先取一次
        // _mutex 再通知，于是"判断完谓词、还没阻塞"这段窗口里到达的通知，
        // 要么已经被这次判断看见，要么让本线程已经在条件变量上排好队等着被叫，
        // 两种情况都不会丢通知。
        //
        // 反过来，"先睡一觉、醒了再看"就要为每条已经到手的任务先付出一次睡眠，
        // 并且每次被叫醒都要重新判断一遍谓词，白跑一轮。
        std::unique_lock<std::mutex> lock(_mutex);
        if (_isQuit) {
            break;
        }

        // 有活就立刻干，绝不为一件已经到手的活先睡一觉。抢不到活（队列刚被
        // 别的管家线程清空）就回循环顶部重新判断，也是往下走而不是硬睡。
        if (_predicateCallback()) {
            lock.unlock();
            executeGuarded();
            continue;
        }

        if (_waitHintCallback) {
            // 带超时地等：延迟任务到点时不会有人 notify，只能靠超时醒来。
            //
            // 这里故意用不带谓词的那个重载。带谓词的 wait_for(lock, 时长, 谓词)
            // 在被通知之后会"复用同一个时长"：条件还不成立就拿着旧时长再睡一轮。
            // 那样一来，新任务入队的通知只会让线程把原来那段长觉重新睡满
            // （比如本来算出来可以睡 1 小时，通知到达后它又睡 1 小时）。
            //
            // 睡多久必须在锁内、并且在上面那次判断之后重新算：退出锁再算，
            // 算出来的可能是别人已经处理过的旧状态。
            const auto hint = _waitHintCallback();
            if (hint <= std::chrono::milliseconds::zero()) {
                // 刚判完就有活到期，或者刚算完就被别人抢空，回顶部重新判断，
                // 不在这里睡，也不在这里空转（顶部会立刻取到任务或算出新的时长）。
                continue;
            }
            _cv.wait_for(lock, hint);
        } else {
            // 没有定时需求就睡到有人唤醒；被唤醒后由循环顶部重新判断谓词，
            // 条件变量的虚假唤醒也交给这个循环消化。
            _cv.wait(lock);
        }
    }
}

void ThreadPool::quit() {
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _isQuit = true;
    }
    _isRunning = false;
    _cv.notify_all();  // 唤醒所有等待的线程
}

void ThreadPool::wake() {
    // 这里先空取一次 _mutex 再通知，不是多余的。
    // 条件变量用的是这把 _mutex，但谓词读的状态（任务队列）在 TaskQueue 手里，
    // 由 TaskQueue 自己的锁保护。如果通知方完全不碰 _mutex，就可能出现
    // "任务已经入队、通知却落在等待方判断完谓词到真正阻塞之间"的丢唤醒，
    // 管家线程会一直睡下去。取一次锁能把通知和阻塞排好序。
    {
        std::lock_guard<std::mutex> lock(_mutex);
    }
    _cv.notify_one();  // 一条任务就叫一个人，不做全员唤醒
}

void ThreadPool::wakeAll() {
    {
        std::lock_guard<std::mutex> lock(_mutex);
    }
    _cv.notify_all();
}

bool ThreadPool::isRunning() const noexcept {
    return _isRunning.load();
}

void ThreadPool::executeGuarded() {
    try {
        _executeCallback();
    } catch (const std::exception& exception) {
        // 管家线程是长驻的，异常逃逸出去会让线程被 terminate 干掉，
        // 线程池会静默地少一个工人，之后处理能力永久下降。
        INK_LOG_ERROR(kModuleName,
                      std::string("管家线程执行任务抛出异常：") + exception.what());
    } catch (...) {
        INK_LOG_ERROR(kModuleName, "管家线程执行任务抛出未知异常");
    }
}

void ThreadPool::release() {
    quit();
    for (auto& thread : _threads) {
        if (thread && thread->joinable()) {
            thread->join();
        }
    }
    _threads.clear();
}

}  // namespace inking

