#pragma once

// 时间线：整个 UI 的唯一时间源，**双速驱动**。
//
// 为什么要有它：逻辑要固定步长（50Hz），动画要按需采样（24fps 的动画就该按
// 1/24s 走），而显示器是又一个频率（60/120/144Hz）。三者互不整除，
// 所以**时间不能按"多少次 tick"来数，只能按真实流逝时间累加**——
// 错拍只影响"这一帧算几次"，不影响时间本身。
//
// 三条通道（就是"三组逻辑"落到机制上的样子）：
//
//   1. **逻辑步**（固定 interval，默认 1/50s）：累加器追赶，一帧可能跑 0 次、
//      1 次或多次。回调收到的是固定的 interval，不是真实 delta——
//      物理和状态机要的就是确定的步长。
//   2. **每帧**（跟着渲染走）：每个渲染帧恰好一次，回调收到真实 delta。
//      插值、和画面同步的东西放这里。
//   3. **自定义频率订阅**：自己声明"我多久要一次"（例如 1/24s），
//      同样走累加器追赶，回调收到的是**实际步长**（= 声明的间隔），
//      所以慢帧也不会让动画走偏。
//
// 为什么驱动源是主循环而不是任务队列：
//   - 任务队列的到点是 condition_variable::wait_for 撑的，Windows 上定时器
//     粒度 1~15.6ms；50Hz 逻辑步是 20ms，抖动占比太大；
//   - 更关键的是**回调线程**：任务队列在管家线程上执行动作，而 UI 状态
//     （坐标、标脏、命中）只能在 UI 线程动，否则就是数据竞争。
//   时间可以在别处算，交付必须在 UI 线程——主循环天然满足。
//   任务队列仍然可以留作无窗口时的外部时钟源或跨线程任务，但不做节拍器。
//
// 追上限制：机器休眠 / 切后台回来时 delta 可能是几秒，不设上限就会
// 一次补几百帧逻辑，直接卡住。超过 kMaxCatchUpSeconds 的部分直接丢掉
// （丢掉的是"欠的帧"，不是时间基准——累加器清零而不是减少）。
//
// 单线程使用：所有方法都只在 UI 线程调用，所以不加锁。

#include <cstdint>
#include <functional>
#include <vector>

namespace inking {

class Timeline {
public:
    /** 订阅句柄。0 表示无效（或已经被移除了）。 */
    using Handle = std::uint64_t;

    /** 自定义频率的回调：入参是**实际步长**（等于订阅时声明的间隔）。 */
    using SampleCallback = std::function<void(double stepSeconds)>;

    /** 逻辑步的回调：入参是固定的逻辑步长。 */
    using LogicCallback = std::function<void(double fixedStepSeconds)>;

    /** 每帧回调：入参是这一帧的真实流逝时间。 */
    using FrameCallback = std::function<void(double deltaSeconds)>;

    /**
     * @param logicIntervalSeconds 逻辑步长，默认 1/50s（50Hz）。
     * @param maxCatchUpSeconds    单帧最多补多少时间，默认 0.25s。
     */
    explicit Timeline(double logicIntervalSeconds = 1.0 / 50.0,
                      double maxCatchUpSeconds = 0.25);

    // ---------------- 驱动 ----------------

    /**
     * 推进时间线。主循环每帧调一次，传**真实流逝时间**。
     *
     * 内部依次派发：逻辑步（可能 0..N 次）→ 每帧回调（恰好一次）→
     * 到期的自定义订阅（各自 0..N 次）。
     */
    void Advance(double deltaSeconds);

    // ---------------- 订阅 ----------------

    /** 订阅一个自定义频率；intervalSeconds <= 0 会被拒绝并返回 0。 */
    Handle Subscribe(double intervalSeconds, SampleCallback callback);

    /** 退订；句柄无效或已被移除时返回 false。 */
    bool Unsubscribe(Handle handle);

    /** 当前有多少个自定义频率订阅（自检用）。 */
    std::size_t SubscriberCount() const noexcept;

    // ---------------- 逻辑步通道 ----------------

    /** 注册逻辑步回调（固定步长）。 */
    void SetLogicCallback(LogicCallback callback);

    /** 逻辑步长（秒）。 */
    double GetLogicInterval() const noexcept;

    // ---------------- 每帧通道 ----------------

    /** 注册每帧回调（真实 delta）。 */
    void SetFrameCallback(FrameCallback callback);

    // ---------------- 自检 / 观测 ----------------

    /** 从构造到现在，一共派发过多少次逻辑步。 */
    std::uint64_t GetLogicStepCount() const noexcept;

    /** 上一帧实际派发了几次逻辑步（0、1 或更多）。 */
    int GetLastLogicSteps() const noexcept;

    /** 上一帧的真实 delta。 */
    double GetLastDelta() const noexcept;

    /** 当前积累了多少还没够一个逻辑步的时间。 */
    double GetLogicAccumulator() const noexcept;

    /** 丢掉过几次"追不上"的多余时间（超过 maxCatchUp 的次数）。 */
    std::uint64_t GetCatchUpDropCount() const noexcept;

private:
    struct Subscriber {
        Handle handle = 0;
        double interval = 0.0;
        double accumulator = 0.0;
        SampleCallback callback;
    };

    double _logicInterval = 1.0 / 50.0;
    double _maxCatchUp = 0.25;

    double _logicAccumulator = 0.0;
    LogicCallback _logicCallback;

    FrameCallback _frameCallback;

    std::vector<Subscriber> _subscribers;
    Handle _nextHandle = 1;

    std::uint64_t _logicStepCount = 0;
    int _lastLogicSteps = 0;
    double _lastDelta = 0.0;
    std::uint64_t _catchUpDrops = 0;
};

}  // namespace inking
