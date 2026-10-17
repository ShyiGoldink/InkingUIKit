#pragma once

// 按钮契约：**三态外观 + 过渡状态机 + 点击事件槽**。
//
// 谁该继承它
// ----------
// 任何"长得像按钮"的组件——不只是按钮。它把这三样从 `InkingStaticButton` 的
// 公开面上摘下来，于是"可点 + 有三态"变成一条**能力线**，而不是"只有按钮才有"
// 的实现细节。
//
// 为什么会有这个文件
// ------------------
// 这三样原先长在 `InkingStaticButton` 上，想复用只有继承那个**具体类**一条路
// ——连带把形状、文字、命中、登记全带上。`InkingStaticImage` 就是受害者：
// 它在自己的头里把 `GetAppearance` / `GetDisplayColor` / `GetDisplayTransform`
// 又抄了一遍（只是"只认 Normal 一态"）。分层归位之后是：
//
//     ButtonData           数据：三态外观 / 每态尺寸 / 文字 / 形状      （ButtonData.h）
//     ButtonLook           状态机：三态 + 过渡插值（纯值类型）          （ButtonLook.h）
//     InkingButtonLike     契约：这个文件 —— clicked 事件 + 三态 + [√] 钩子
//     detail::ButtonPaint  绘制：形状 / 贴图 / 描边阴影 / 文字          （ButtonPaint.h）
//            ↓
//     InkingStaticButton   接线：把上面接起来（形状、文字、命中、登记）
//     InkingStaticImage    接线：同上，但**不吃输入**（见下）
//
// ---------------------------------------------------------------------------
// 为什么**不**做成纯接口（`IInkingButtonLike`）
// ---------------------------------------------------------------------------
// 因为它**必须带状态**：`ButtonLook` 那十几个字段、过渡的计时与起点、`clicked`
// 槽，全是实现而不是契约。抽成纯接口（只有纯虚函数、无数据、无实现体）之后，
// 每个使用者都要把状态机与二十来行转发**再写一遍**——正是这个文件要消除的东西。
// 你的原话是"允许一些其它组件**继承** button 的三态和点击事件"：要继承的是
// **行为**，而纯接口给不了行为。
//
// 本仓库另外两条契约（`input/InkingPointerHandler.h`、`input/InkingKeyboardTarget.h`）
// 也是同一个形状——头文件里的基类 + 默认实现 + `[√]` 钩子，它们的头注释里写的
// 是"契约"，不是"接口"。这里跟它们保持同一族。
//
// 还有一条更要紧的：纯接口在这里**没有消费者**。本仓库不做任何类型查询
// （见 `InkingPointerHandler.h` 顶上那条"钩子即能力发现"）——渲染与命中问的都是
// `InkingWidget` 的虚函数，所以 `IInkingButtonLike*` 这个多态手柄无处可用，
// 只会变成"同一个东西的第二个名字"。要"能指向任何一个按钮"，
// **继承来的 `InkingButtonLike*` 本身就已经是那个手柄了**。
//
// ---------------------------------------------------------------------------
// 为什么**不**继承 `InkingPointerHandler`
// ---------------------------------------------------------------------------
// `InkingWidget` 已经继承了它，而本类是给 Widget 派生类**多继承**用的：
// 再继承一次就是**菱形**——两个 `InkingPointerHandler` 子对象，
// `PushPointerHover` 直接歧义，而且**未必编译报错**（可能静默推到另一个
// 子对象上，然后"钩子看起来没生效"）。所以这里**不继承任何东西**，只提供
// `MouseHover` / `MousePress` / `MouseRelease` / `TriggerClick` 四个入口；
// 组件在自己的 `onPointer*` 钩子里转发一行（见 `InkingStaticButton.cpp`）。
//
// ---------------------------------------------------------------------------
// "吃不吃输入"不是两种类型，是**重不重写那几个钩子**
// ---------------------------------------------------------------------------
// `InkingStaticImage` 继承本类以后，同样有三态外观与 `clicked` 事件；但它一个
// `onPointer*` 都不重写，于是没人喂它、三态永远停在 Normal——**正好就是它要的**
// （它头注释里的原话："不吃输入、也没有回调"）。所以不必为"可点 / 不可点"
// 拆成两个基类。
//
// ---------------------------------------------------------------------------
// 标脏的归属（`AGENTS.md` §3 第 3 条）
// ---------------------------------------------------------------------------
// 本类**自己一个字都不标脏**。它只负责回答"状态变了没有""显示值变了没有"，
// 然后调 `[√]` 钩子把这两件事**报告**给组件，由组件决定标哪个脏——
// 因为"标脏是写入口的活"，而写入口在组件那边。
//
// 颜色与变换不属于会标脏的四件事（几何 / 可见性 / 层级 / 指针位置），所以
// "只换颜色"这条路上不该有标脏调用——这正是静态按钮能一直待在"进命中表"
// 那一档的原因（`docs/InputDesign.md` §11 的变换通道）。

#include <button/ButtonData.h>
#include <button/ButtonLook.h>
#include <event/InkingEvent.h>

#include <cstdint>
#include <utility>

namespace inking {

/**
 * @brief 按钮契约：三态外观 + 过渡 + 点击事件。
 *
 * 组件**多继承**它（`class MyButton : public InkingStaticWidget,
 * public InkingButtonLike`），于是 `clicked` / `GetState` / `MouseHover` /
 * `TriggerClick` 这些**继承得来**，一个都不用转发。
 *
 * 它自己不是 Widget：**不继承 `InkingWidget`、也不继承任何输入契约**
 * （理由见文件顶上"为什么不一继承 InkingPointerHandler"那段）。
 */
class InkingButtonLike {
public:
    InkingButtonLike() = default;
    virtual ~InkingButtonLike() = default;

    // 事件槽不可拷贝（拷贝会把订阅者复制成两份），而它只该跟着一个组件走
    // ——组件本身也禁拷贝（`InkingAnchor` 那条规矩），跟着走即可。
    InkingButtonLike(const InkingButtonLike&) = delete;
    InkingButtonLike& operator=(const InkingButtonLike&) = delete;

    /**
     * **点击事件槽**：`clicked.Subscribe(...)` 就能挂回调，而且能挂**多个**、
     * 能按令牌退订。声明成公开成员而不是"再来一套 SetXxx"，因为事件是
     * **组件的对外接口**（就像它的写入口），不是内部状态。
     *
     * `SetOnClicked` 是它的便利写法（"只挂一个、再设一次就是换掉"的旧语义），
     * 两条路落到同一份实现上。
     */
    InkingEvent<> clicked;

    // ---------------- 观测口 ----------------
    //
    // 渲染、命中、自检都问这几个，不必知道具体是哪个组件。
    //
    // 注意 `GetDisplayWidth` / `GetDisplayHeight` 的命名：**不能**叫
    // `GetWidth` / `GetHeight`——那两个是 `InkingAnchor` 的虚函数，组件必须
    // 自己重写它们（`return GetDisplayWidth();`）。同名会变成"多继承来的
    // 两个同名声明"，查找歧义。

    /// 当前三态中的一个。初始是 Normal。
    ButtonState GetState() const noexcept { return _look.state; }

    /**
     * 三态外观（按当前状态）。注意它是**目标**那一份配置（json 里写的），
     * 过渡进行中它与屏幕上正在显示的颜色不是一回事——要那个用 `GetDisplayColor()`。
     */
    const ButtonAppearance& GetAppearance() const noexcept {
        return _look.Current();
    }

    /// 指定某一个状态的外观。默认值来自配置里的 normal / hover / onclicked。
    const ButtonAppearance& GetAppearance(ButtonState state) const noexcept {
        return _look.For(state);
    }

    /// 现在**实际画出来**的颜色（0xAARRGGBB）。过渡进行中是插值出来的中间色。
    std::uint32_t GetDisplayColor() const noexcept { return _look.displayColor; }

    /// 现在**实际生效**的变换。绘制用它过正变换、命中用它过逆变换。
    const TransformSpec& GetDisplayTransform() const noexcept {
        return _look.displayTransform;
    }

    /// 当前**画出来 / 判命中**的尺寸（T-5 布局通道）。
    int GetDisplayWidth() const noexcept { return _look.DisplayWidth(); }
    int GetDisplayHeight() const noexcept { return _look.DisplayHeight(); }

    /// 现在有一段过渡在跑吗。
    bool IsTransitioning() const noexcept { return _look.IsTransitioning(); }

    /// 过渡的**线性**进度：0 = 刚开始，1 = 已到位。没有过渡时恒为 1。
    float GetTransitionProgress() const noexcept {
        return _look.TransitionProgress();
    }

    /// 三个状态里有尺寸差异吗。组件拿它回答 `InkingWidget::HasLayoutAnimation()`。
    bool HasButtonLayoutAnimation() const noexcept {
        return _look.HasSizeAnimation();
    }

    // ---------------- 事件（便利写法） ----------------

    /// 点击回调挂上了吗（等价 `clicked.HasSubscribers()`）。
    bool HasOnClicked() const noexcept { return clicked.HasSubscribers(); }

    /**
     * 设置点击回调。
     *
     * **语义是"替换"**：它会先把 `clicked` 上已有的订阅者全部清掉，再挂这一个
     * ——也就是以前那个 `_onClicked = callback` 的行为，别指望它和
     * `clicked.Subscribe` 叠加。要挂多个就用事件槽那条路。
     */
    void SetOnClicked(ButtonClickCallback callback) {
        clicked.Clear();
        if (callback) {
            clicked.Subscribe(std::move(callback));
        }
    }

    /**
     * 触发一次点击。
     *
     * 由组件在"抬起且指针仍在按钮内"时调；也可以直接调来模拟一次点击
     * （自检就是这么用的）。没有订阅者时是空操作，返回 false。
     *
     * 回调里可能把按钮析构掉（例如"点一下就关掉这个场景"）：`Emit` 的**快照
     * 派发**正好兜住这件事——名单在派发开始时就拷好了（`AGENTS.md` §6 第 63 条）。
     *
     * @return 真的派发了才返回 true。
     */
    bool TriggerClick() {
        if (!clicked.HasSubscribers()) {
            return false;
        }
        clicked.Emit();
        return true;
    }

    // ---------------- 喂入口（幂等，只换颜色） ----------------
    //
    // 名字用 MouseXxx 而不是 SetHover：一眼能看出"这是鼠标事件推过来的"，
    // 而不是某个业务状态。谁拿到判定结果谁调——静态档由场景派发喂，
    // 动态档自己从场景拉。三个都不标脏、不通知场景：颜色不属于会标脏的
    // 四件事（几何 / 可见性 / 层级 / 指针移动位置）。

    /**
     * 指针进出。
     *
     * 这个标志一直更新（即使正在按下）：它同时是"抬起时该回哪一态"的依据，
     * 而拖拽时指针滑出按钮再松开，本来就该回到 Normal 而不是 Hover。
     *
     * @return 三态真的变了才返回 true（标志变了但三态没变——例如正按着的时候
     *         指针滑出去——也返回 false，那时连颜色都不用换）。
     */
    bool MouseHover(bool hover) noexcept {
        const ButtonState before = _look.state;
        if (!_look.SetHovered(hover)) {
            return false;
        }
        OnStateChanged(before);
        return true;
    }

    /**
     * 按下 / 抬起。
     *
     * `press(true)` 进入 Pressed；`press(false)` 按**当前**指针位置回到
     * Hover（还在里面）或 Normal（已经出去了）。所以框架层不需要自己记住
     * "抬起时该回哪一态"——那份状态只有按钮自己知道。
     */
    bool MousePress(bool press) noexcept {
        const ButtonState before = _look.state;
        if (!_look.SetPressed(press)) {
            return false;
        }
        OnStateChanged(before);
        return true;
    }

    /// 抬起。等价于 `MousePress(false)`，写成两个名字是因为调用方那两处
    /// （按下事件 / 抬起事件）读起来更直白，也不会有人把参数写反。
    bool MouseRelease() noexcept { return MousePress(false); }

    /**
     * 每**渲染帧**推进一次过渡（真实 delta，和画面同拍——`docs/InputDesign.md`
     * §11 要求命中与渲染同一个 t）。
     *
     * 组件在 `onAnimationTick(delta)` 里调它一次即可，返回值可以不管：
     * "显示值变了什么"会经 `onButtonDisplayAdvanced` 报到组件那边。
     *
     * @return 这一帧变了什么（`repaint` = 画面变了，`hit` = 命中结果可能变了）。
     */
    ButtonLook::AdvanceResult AdvanceButtonLook(float deltaSeconds) noexcept {
        const ButtonLook::AdvanceResult advanced = _look.Advance(deltaSeconds);
        if (advanced.repaint || advanced.hit) {
            onButtonDisplayAdvanced(advanced.repaint, advanced.hit);
        }
        return advanced;
    }

protected:
    /**
     * 装配置：把 `ButtonData` 里的三态外观与基准尺寸展开进状态机（并复位）。
     *
     * 组件的构造函数里调一次，并且**要在写几何之前**：`ButtonLook` 的基准尺寸
     * 是"状态没写尺寸"时的兜底，构造期就有人问（锚点推导、场景登记）。
     */
    void ResetButtonLook(const ButtonData& data) noexcept { _look.Reset(data); }

    // ---------------- [√] 可重写钩子 ----------------
    //
    // 两个钩子都**不标脏**——标脏是组件的事（`AGENTS.md` §3 第 3 条）。
    // 默认都是空实现（写在这里，不是留声明：它们是虚函数，"不关心"是正常情况，
    // 只声明不定义会让基类 vtable 引用一个不存在的符号）。

    /**
     * [√] 三态真的变了（`from` != `to`，两个都不是"没变"的哨兵）。
     *
     * 组件的副作用挂这里：同步基类那份 color、换贴图、播声音、记一笔日志。
     * 它在 `MouseHover` / `MousePress` 里**同步**被调——不是每帧，也不是延迟的。
     *
     * 它与 `onButtonDisplayAdvanced` 的分工：这里是"状态**目标**变了"，
     * 那里是"**显示值**变了"。配了过渡时，目标当场就变，而显示值要几帧才走到。
     */
    virtual void onButtonStateChanged(ButtonState /*from*/,
                                      ButtonState /*to*/) {}

    /**
     * [√] 过渡推进了一帧，**显示值**变了。
     *
     * @param repaintDirty 颜色 / 变换 / 尺寸变了：这一帧的画面和上一帧不同
     *                     → 该标**重绘脏**。
     * @param hitDirty     变换或尺寸变了：同一个点可能落到别的组件上
     *                     → 该标**命中脏**（但不用重烘表：表按本地形状烘，
     *                     查询时把点反变换回本地即可）。
     *
     * 两项分开是为了让调用方标对脏——只用一个 bool 的话，"颜色变了"也会去标
     * 命中脏，于是每帧白重建一次绘制列表，这是动画接上标脏之后最容易付的
     * 冤枉代价（`ButtonLook::AdvanceResult` 的注释里有同一段话）。
     */
    virtual void onButtonDisplayAdvanced(bool /*repaintDirty*/,
                                         bool /*hitDirty*/) {}

private:
    /// 三态外观 + 过渡状态机（与 `ButtonData` 无关的那一半；静态 / 动态共用）。
    /// 私有：外面只能经上面那些观测口 / 喂入口碰它，免得"显示值"被绕过状态机改掉。
    ButtonLook _look{};

    /// `before` 与当前状态的比较收在一处，两个写入口共用。
    void OnStateChanged(ButtonState before) {
        onButtonStateChanged(before, _look.state);
    }
};

}  // namespace inking
