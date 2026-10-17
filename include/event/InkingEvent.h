#pragma once

// 通用事件槽（A9）：组件**声明事件**，使用者**挂回调**——不必再改底层。
//
// 为什么不是"每个组件自己写一个 std::function 成员 + SetXxx"：
// 按钮的 `onClicked` 原来就是那样写的——只能挂**一个**回调，也没有"退订"
// 这个概念（再挂一次就是把前一个换掉）。到了 Slider / Toggle / List 这一档，
// "值变了""提交了""拖动了"往往要通知好几处，一个回调不够用。
//
// 三条契约（写在类型上，不靠约定）：
//
//   1. **声明即订阅点**：组件把 `InkingEvent<...>` 声明成一个公开成员
//      （例如 `InkingEvent<float, float> valueChanged;`），使用者 Subscribe
//      就完了。框架层**一个字都不用认识这个组件**——"新组件不再依赖项目
//      基础部分"这条在这里又落实一处（跟 A1 的指针钩子、A3 的键盘钩子同源：
//      钩子是"组件收输入"的落点，事件槽是"组件往外说"的落点）。
//
//   2. **事件只通知、不标脏**：标脏始终是写入口的活（`AGENTS.md` §3 第 3 条）。
//      组件要在**值已经改完、脏已经标了**之后再 `Emit`，这样订阅者拿到的
//      状态是一致的：回调里问 `IsRepaintDirty()` / 读新值都不会看到半截状态。
//
//   3. **派发用快照**：`Emit` 先把当前订阅者拷一份再逐个调。于是"回调里
//      订阅 / 退订 / 清空 / 再 Emit"都不会把这次派发搞乱——语义是
//      "**本次派发开始时**已经挂上的订阅者都会收到这一趟"。代价是每次 Emit
//      一次分配（UI 事件每秒几次，可以忽略）；换来的是可预测的重入行为，
//      这种地方"省一次分配"不值当。
//
// 生命周期：事件槽是**组件的一部分**，组件析构它就没了。`Unsubscribe` 用的
// 令牌只是一串数字，所以"组件先死、订阅者还拿着令牌去退订"是悬垂——和按钮
// `SetOnClicked` 一样，订阅者要保证自己不比组件活得长。组件**不可拷贝**
// （`InkingAnchor` 本来就禁拷贝），事件槽跟着这条规矩走。
//
// 它不碰 SDL、不碰渲染，所以进公开头不破 `AGENTS.md` §6 第 8 条。

#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace inking {

/**
 * @brief 一个可订阅的事件槽：组件的公开成员，使用者 `Subscribe` 挂回调。
 *
 * ```cpp
 * class Slider : public inking::InkingStaticWidget {
 * public:
 *     inking::InkingEvent<float, float> valueChanged;  // ← 声明事件（新值, 旧值）
 *     inking::InkingEvent<> committed;                 // ← 无参事件
 *
 *     bool SetValue(float v) {          // 写入口：先改、再标脏、最后发事件
 *         if (v == _value) return false;
 *         const float previous = _value;
 *         _value = v;
 *         MarkRepaintDirty();
 *         valueChanged.Emit(_value, previous);
 *         return true;
 *     }
 * };
 *
 * // 使用者这边：
 * const auto token = slider.valueChanged.Subscribe([](float now, float before) {
 *     // now / before 就是"事件发生时需要的上下文"
 * });
 * slider.valueChanged.Unsubscribe(token);   // 退订
 * ```
 *
 * `Args...` 由组件自己定：值变化就带值，拖动就带位置，什么都不用带就写成
 * `InkingEvent<>`（无参）。回调是 `std::function`，可以挂 lambda / 成员函数 /
 * 函数对象；**它按参数类型原样传值**，所以别在事件里传大对象（要传就传引用
 * 或指针，并自己保证那份数据的生命周期）。
 */
template <typename... Args>
class InkingEvent {
public:
    /// 回调类型：参数就是事件声明的那些。
    using Handler = std::function<void(Args...)>;

    /// 订阅令牌，`Unsubscribe` 用它。**0 永远不是合法令牌**（`Subscribe` 失败
    /// 时也返回它）。
    using Token = std::uint64_t;

    InkingEvent() = default;

    // 事件槽属于它所在的那个组件：组件不可拷贝，槽也不该被搬来搬去
    // （拷贝会把"订阅者"复制成两份，而它只该跟着一个组件走）。
    InkingEvent(const InkingEvent&) = delete;
    InkingEvent& operator=(const InkingEvent&) = delete;

    /**
     * 挂一个回调。@return 退订用的令牌；传空回调时返回 0（表示没挂上）。
     *
     * 同一个回调挂两次会收到两次——它就是两个订阅者，不做去重（去重要比
     * `std::function` 的地址，靠不住）。
     */
    Token Subscribe(Handler handler) {
        if (!handler) {
            return 0;
        }
        const Token token = _nextToken++;
        _slots.push_back(Slot{token, std::move(handler)});
        return token;
    }

    /** 按令牌退订。@return 真的退掉了才返回 true（令牌不存在 / 为 0 都返回 false）。 */
    bool Unsubscribe(Token token) noexcept {
        if (token == 0) {
            return false;
        }
        for (auto it = _slots.begin(); it != _slots.end(); ++it) {
            if (it->token == token) {
                _slots.erase(it);
                return true;
            }
        }
        return false;
    }

    /// 还有订阅者吗（自检与"要不要干活"的短路用）。
    bool HasSubscribers() const noexcept { return !_slots.empty(); }

    /// 现在挂着几个订阅者。
    std::size_t SubscriberCount() const noexcept { return _slots.size(); }

    /// 全部退订（注意：`Emit` 的**快照语义**意味着本次在途的派发不受影响）。
    void Clear() noexcept { _slots.clear(); }

    /**
     * 发一次事件（**只由组件自己调**，不是给使用者用的）。
     *
     * 快照派发：先拷一份当前订阅者，再逐个调。所以回调里做下面这些都不会把
     * 这次派发搞乱——本次仍然按"开始时的名单"走完：
     *   - `Subscribe`（新挂上的**不**收本次）；
     *   - `Unsubscribe`（被退订的**仍**收本次，下次就不来了）；
     *   - `Clear`（同上，本次名单已经拷好了）；
     *   - 再 `Emit`（嵌套派发各自一份快照）。
     */
    void Emit(Args... args) {
        if (_slots.empty()) {
            return;
        }
        std::vector<Handler> snapshot;
        snapshot.reserve(_slots.size());
        for (const Slot& slot : _slots) {
            snapshot.push_back(slot.handler);
        }
        for (const Handler& handler : snapshot) {
            if (handler) {
                handler(args...);
            }
        }
    }

private:
    struct Slot {
        Token token = 0;
        Handler handler{};
    };

    std::vector<Slot> _slots{};
    Token _nextToken = 1;
};

}  // namespace inking
