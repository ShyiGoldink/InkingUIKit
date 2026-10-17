#include <timeline/Timeline.h>

#include <algorithm>
#include <cmath>

namespace inking {

namespace {

/** 单帧最多派发多少步逻辑，纯保险（入口已经截断过 delta）。 */
constexpr int kMaxStepsPerFrame = 1000;

}  // namespace

Timeline::Timeline(double logicIntervalSeconds, double maxCatchUpSeconds)
    : _logicInterval(logicIntervalSeconds > 0.0 ? logicIntervalSeconds
                                                : 1.0 / 50.0),
      _maxCatchUp(maxCatchUpSeconds > 0.0 ? maxCatchUpSeconds : 0.25) {}

// ---------------------------------------------------------------------------
// 驱动
// ---------------------------------------------------------------------------

void Timeline::Advance(double deltaSeconds) {
    // 负的 delta（时钟回拨 / 第一帧）按 0 处理，绝不倒着走。
    if (deltaSeconds <= 0.0) {
        deltaSeconds = 0.0;
    }

    // 追不上就在**入口**截断：切后台 / 睡眠回来时 delta 可能是几秒，
    // 不截断就会一次补几百步逻辑，直接卡住一帧。
    //
    // 截断的意思是"这段时间不补了"——帧数恢复常速，游戏时间暂停过。
    // 这是所有引擎的做法：宁可让模拟里的时间少走一段，也不能卡死。
    // （注意不能放在循环后面判断：循环一定会把累加器排干，
    //   放在后面这个限制永远触发不了。）
    if (deltaSeconds > _maxCatchUp) {
        deltaSeconds = _maxCatchUp;
        ++_catchUpDrops;
    }

    _lastDelta = deltaSeconds;

    // ---- 1. 逻辑步：累加器追赶 ----
    //
    // 时间按真实流逝累加，所以 50Hz 逻辑跑在 60Hz 屏幕上必然错拍：
    // 有的帧 0 次、有的帧 1 次、偶尔 2 次。错拍只影响"这一帧算几次"，
    // 不会让逻辑时间相对墙上时钟漂移——这正是不能用"数 tick"的原因。
    _logicAccumulator += deltaSeconds;

    _lastLogicSteps = 0;
    while (_logicAccumulator >= _logicInterval) {
        // 传给回调的是**固定的** interval，不是真实 delta：
        // 物理和状态机要的就是确定的步长。
        if (_logicCallback) {
            _logicCallback(_logicInterval);
        }
        _logicAccumulator -= _logicInterval;
        ++_logicStepCount;
        ++_lastLogicSteps;

        // 保险：入口已经截断过了，正常到不了这里；真到了就停下，
        // 把余量交给下一帧，别在一帧里空转。
        if (_lastLogicSteps > kMaxStepsPerFrame) {
            ++_catchUpDrops;
            break;
        }
    }

    // ---- 2. 每帧回调：每个渲染帧恰好一次 ----
    //
    // 放在逻辑步之后：这一帧要画的东西已经按最新逻辑更新完了。
    // 传的是**截断后**的 delta，和逻辑步看到的是同一个时间尺度。
    if (_frameCallback) {
        _frameCallback(deltaSeconds);
    }

    // ---- 3. 自定义频率订阅：各自累加追赶 ----
    //
    // 按索引遍历而不是用迭代器：回调里可能退订别人（甚至退订自己，
    // 见头文件里"一次性延迟"那段的说明），handle 会被清成 0。
    for (std::size_t i = 0; i < _subscribers.size(); ++i) {
        const Handle handle = _subscribers[i].handle;
        if (handle == 0) {
            continue;  // 这一项已经在回调里被退订了
        }

        Subscriber& subscriber = _subscribers[i];
        subscriber.accumulator += deltaSeconds;

        int dispatched = 0;
        while (subscriber.accumulator >= subscriber.interval) {
            // 回调收到的是**实际步长**（= 声明的间隔），不是墙上 delta：
            // 这样动画的进度只和"播了几步"有关，慢帧也不会走偏。
            if (subscriber.callback) {
                subscriber.callback(subscriber.interval);
            }
            subscriber.accumulator -= subscriber.interval;
            ++dispatched;

            // 回调里可能把这一项退订掉，那样索引 i 处已经换了别人（或空了）。
            if (i >= _subscribers.size() || _subscribers[i].handle != handle) {
                break;
            }
            if (dispatched > 1000) {
                break;
            }
        }

        // 这一项自己累积得太多也一样要丢：按自己的间隔截断，
        // 别让一个慢订阅者攒下一大堆待补的步。
        if (i < _subscribers.size() && _subscribers[i].handle == handle
            && _subscribers[i].accumulator > _maxCatchUp) {
            _subscribers[i].accumulator =
                std::fmod(_subscribers[i].accumulator, _subscribers[i].interval);
            ++_catchUpDrops;
        }
    }
}

// ---------------------------------------------------------------------------
// 订阅
// ---------------------------------------------------------------------------

Timeline::Handle Timeline::Subscribe(double intervalSeconds,
                                     SampleCallback callback) {
    if (intervalSeconds <= 0.0 || !callback) {
        return 0;
    }

    // 顺手清掉上次退订留下的墓碑，别让它们无限堆积。
    const auto tombstone = std::remove_if(
        _subscribers.begin(), _subscribers.end(),
        [](const Subscriber& subscriber) { return subscriber.handle == 0; });
    _subscribers.erase(tombstone, _subscribers.end());

    Subscriber subscriber;
    subscriber.handle = _nextHandle++;
    subscriber.interval = intervalSeconds;
    subscriber.callback = std::move(callback);
    _subscribers.push_back(std::move(subscriber));
    return _subscribers.back().handle;
}

bool Timeline::Unsubscribe(Handle handle) {
    if (handle == 0) {
        return false;
    }

    const auto it = std::find_if(_subscribers.begin(), _subscribers.end(),
                                 [handle](const Subscriber& subscriber) {
                                     return subscriber.handle == handle;
                                 });
    if (it == _subscribers.end()) {
        return false;
    }

    // 不真的从 vector 里删掉：回调派发期间按索引遍历，删除会让后面的元素
    // 前移、当前位置换人。把 handle 清成 0 当墓碑，回调里跳过就行；
    // 真正的清理留给下一次 Subscribe 顺手做。
    it->handle = 0;
    it->callback = nullptr;
    return true;
}

std::size_t Timeline::SubscriberCount() const noexcept {
    return static_cast<std::size_t>(
        std::count_if(_subscribers.begin(), _subscribers.end(),
                      [](const Subscriber& subscriber) {
                          return subscriber.handle != 0;
                      }));
}

// ---------------------------------------------------------------------------
// 通道注册与自检
// ---------------------------------------------------------------------------

void Timeline::SetLogicCallback(LogicCallback callback) {
    _logicCallback = std::move(callback);
}

double Timeline::GetLogicInterval() const noexcept {
    return _logicInterval;
}

void Timeline::SetFrameCallback(FrameCallback callback) {
    _frameCallback = std::move(callback);
}

std::uint64_t Timeline::GetLogicStepCount() const noexcept {
    return _logicStepCount;
}

int Timeline::GetLastLogicSteps() const noexcept {
    return _lastLogicSteps;
}

double Timeline::GetLastDelta() const noexcept {
    return _lastDelta;
}

double Timeline::GetLogicAccumulator() const noexcept {
    return _logicAccumulator;
}

std::uint64_t Timeline::GetCatchUpDropCount() const noexcept {
    return _catchUpDrops;
}

}  // namespace inking
