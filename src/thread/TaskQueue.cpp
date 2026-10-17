#include <thread/TaskQueue.h>

#include <detail/InkLog.h>
#include <thread/ThreadPool.h>

#include <exception>
#include <string>
#include <utility>

namespace {

constexpr const char* kModuleName = "TaskQueue";

/** 队列为空时管家线程最多睡多久（有任务入队时会立刻被唤醒，所以这个值只是兜底）。 */
constexpr auto kIdleWait = std::chrono::hours(1);

}  // namespace

namespace inking {

TaskQueue& TaskQueue::instance() {
    // 函数内静态量 + **故意不析构**（new 出来的裸指针，永不 delete）。
    //
    // 为什么不用 `static TaskQueue inst;`：那份静态量会在**进程退出 / DLL 卸载**
    // 时被销毁，而那时可能正持着加载器锁；`~TaskQueue()` 要 `quit()` + join
    // 管家线程，而工作线程退出过程中又要拿加载器锁 ⇒ **死锁**。
    //
    // 实测症状：self-test 打印完"失败项：N"之后就不再返回，进程永远不退出，
    // 退出码也永远拿不到（只能跑 stdout 末行去读失败数）。
    //
    // 取舍与 `AudioState` 同一条（docs/AGENTS.md §6 第 88 条）：进程级的记账
    // 宁可永不析构——进程终止时 OS 会一并结束所有线程，泄漏的只是一个对象。
    // 代价：库被**中途** FreeLibrary 的场景不再安全（管家线程还在跑已卸载的
    // 代码），那不是本框架的用法。
    //
    // 本体搬出头的理由见 TaskQueue.h：动态库下 exe 与 DLL 必须共用同一个队列，
    // 否则各起一组管家线程。
    static TaskQueue* inst = new TaskQueue();
    return *inst;
}

TaskQueue::TaskQueue() {
    _threadLoop = std::make_unique<ThreadPool>();
    _threadLoop->init(
        // 谓词：有任务到期时才唤醒管家线程（只看队列非空不行，延迟任务会空转）
        [this]() { return hasDueTask(); },
        // 执行体：每次从队列中取出一个任务执行
        [this]() { executeTask(); },
        // 等待提示：睡到最近一个任务到期为止
        [this]() { return timeUntilNextDue(); });

    INK_LOG_DEBUG(kModuleName, "任务队列已随单例创建而启动");
}

TaskQueue::~TaskQueue() {
    // 注意：正常退出路径**不会**走到这里——`instance()` 用的是永不析构的裸指针
    // （理由见那里的注释）。这段收尾只在"真的 delete 了单例"时才跑，而那是本
    // 框架里不存在的用法；保留它是为了让 ThreadPool 的停止 + join 有一个明确的
    // 归属，而不是散在别处或干脆丢掉。
    if (!_threadLoop) {
        return;
    }
    // 先停止并 join 全部管家线程，再销毁成员，
    // 避免管家线程在回调中访问已析构的 this
    _threadLoop->quit();
    _threadLoop.reset();
}

void TaskQueue::addTask(Task<std::any> task) {
    addTask(std::move(task), std::chrono::milliseconds(0));
}

void TaskQueue::addTask(Task<std::any> task, std::chrono::milliseconds delay) {
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _queue.emplace(Clock::now() + delay,
                       std::make_unique<Task<std::any>>(std::move(task)));
    }

    // 只叫醒一个管家线程：一条任务只需要一个执行者。
    // 真有 N 条任务同时入队时，是 N 次入队各叫醒一个人，不会因为"只叫一个"而积压；
    // 如果需要更多人手，管家线程干完手里的活回到循环顶部会重新判断谓词，
    // 自己把队列里的下一件活取走。
    _threadLoop->wake();
}

bool TaskQueue::isEmpty() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _queue.empty();
}

bool TaskQueue::hasDueTask() const {
    std::lock_guard<std::mutex> lock(_mutex);
    // multimap 按到期时刻排序，所以只看第一个就够了
    return !_queue.empty() && _queue.begin()->first <= Clock::now();
}

std::chrono::milliseconds TaskQueue::timeUntilNextDue() const {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_queue.empty()) {
        return kIdleWait;
    }

    const auto now = Clock::now();
    const auto due = _queue.begin()->first;
    if (due <= now) {
        return std::chrono::milliseconds(0);
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(due - now);
}

void TaskQueue::executeTask() {
    std::unique_ptr<Task<std::any>> task;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_queue.empty()) {
            return;
        }
        auto earliest = _queue.begin();
        if (earliest->first > Clock::now()) {
            return;  // 还没到期的任务不能提前跑（谓词已经挡了一层，这里兜底）
        }
        task = std::move(earliest->second);
        _queue.erase(earliest);
    }

    // 在锁外执行任务：任务内部若再次 addTask()，不会造成死锁。
    // 任务作为独立入口执行，入参使用空的 any（无上游值）。
    // 防御性兜底：action 抛出的异常绝不能让管家线程逃逸（会导致进程 terminate），
    // 统一在这里捕获并记入日志，单个任务失败不影响线程池继续工作。
    try {
        task->execute(std::any{});
    } catch (const std::exception& exception) {
        INK_LOG_ERROR(kModuleName,
                      std::string("任务执行抛出异常：") + exception.what());
    } catch (...) {
        INK_LOG_ERROR(kModuleName, "任务执行抛出未知异常");
    }
}

}  // namespace inking

