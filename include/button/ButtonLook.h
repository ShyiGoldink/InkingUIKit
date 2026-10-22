#pragma once

// 三态外观 + 状态机：**静态 Button 与动态 Button 共用的那一份**。
//
// 为什么单独放一个头，而不是让两个按钮各写一遍：
//   - 两条路的**输入方式**不同（静态由场景派发喂，动态自己从场景拉，见
//     InkingDynamicButton.h 顶上那段），但"哪个状态配哪份外观""按下 > 悬停 >
//     通常"这套规则必须只有一份。抄第二遍的时候就会分叉，而分叉的表现是
//     "静态按钮按下去和动态按钮长得不一样"——两边单独看都对，合起来才错，
//     属于最难查的一类（`InkingDraw.h` 顶上那条"抄第二遍就抽出来"同一个道理）。
//   - 它**不碰 SDL、不碰渲染器**：纯数据 + 纯逻辑，所以放在公开头里不破
//     "公开头不拖 SDL 进来"那条纪律（docs/AGENTS.md §6 第 8 条）。
//
// 渲染那一笔（形状填充 / 文字占位块）同样只有一份，但它是 SDL 那一侧的东西，
// 放在 `src/button/ButtonPaint.h`（组件内部实现，不进公开头）。

#include <button/ButtonData.h>

#include <theme/InkingTheme.h>

#include <cstdint>
#include <functional>

namespace inking {

/// 点击回调：不带参数、不返回值。
///
/// 原来写的是 `std::function<std::any()>`（任意返回值），这里收窄成 `void`：
/// "按下之后要算出一个值" 不是按钮的职责——需要返回值就直接在 lambda 里捕获
/// 外部状态，或者把它包成一个 Task 交给 TaskQueue。`std::any` 还会逼每个
/// 调用点都去 `std::any_cast` 一个它根本不关心的东西。
///
/// 放在这里而不是某一个按钮头里：静态 / 动态两块按钮**共用同一个回调类型**，
/// 谁换一套都会让"同一个点击回调挂不上另一块按钮"。
using ButtonClickCallback = std::function<void()>;

/// 按钮的三态。顺序即优先级：按下 > 悬停 > 通常。
enum class ButtonState : std::uint8_t {
    Normal = 0,  ///< 通常
    Hover,       ///< 悬停
    Pressed,     ///< 按下（配置里叫 onclicked）
};

/**
 * @brief 三态外观、状态机，以及**三态之间的颜色过渡**。
 *
 * 两个入参（指针在不在里面、按没按下）就是全部输入，没有隐藏状态；
 * 静态 / 动态按钮的差别只在"这两个入参是谁、什么时候填进去的"。
 *
 * 三态是**渲染状态**，不是几何：切换只换颜色，颜色不标脏
 * （docs/API.md「标脏」），所以静态按钮能一直待在"进命中表"那一档。
 *
 * ---------------------------------------------------------------------------
 * 过渡（transition）：只动颜色与变换，绝不碰**本地**几何
 *
 * 这是 `TaskGuide.md` 的 D-1 定下的硬约束：动画一旦改几何，静态档的前提就没了。
 * 所以这里插值的只有**颜色**（含 alpha）与**变换**（平移 / 旋转 / 缩放），
 * 形状与尺寸一个字节都不动。变换属于"变换通道"：它不改命中表，查询时把点反
 * 变换回本地空间（docs/InputDesign.md §11），所以静态档照样成立。
 *
 * 进度可以过一条**缓动曲线**（`ButtonAppearance::easing`）：曲线只改变"画到
 * 哪儿了"，不改时长、起点、终点——见 `include/dataStruct/InkingEasing.h`。
 *
 * 触发是**自动**的：`SetHovered` / `SetPressed` 让三态变了，就顺手用**目标状态
 * 自己那份** `transitionSeconds`（与 `easing`）起一段过渡（过场段各自带时长
 * 与曲线：normal→hover 用 hover 的，hover→normal 用 normal 的）。
 * 没配就是瞬间切换。
 *
 * 推进靠 `Advance(delta)`，由按钮在 `onAnimationTick` 里每**渲染帧**调一次
 * （真实 delta，和画面同拍——docs/InputDesign.md §11 要求命中与渲染同一个 t）。
 * 静态组件收不到 `InkingDynamicWidget::Tick`（那是动态档的逻辑步），
 * 所以每帧通道单独有一条：见 `InkingWidget::TickAnimation`。
 */
struct ButtonLook {
    /// 三态外观按状态展开存放，渲染时按当前状态直接取，
    /// 不在每帧去 if-else 拼一遍。
    ButtonAppearance normal{};
    ButtonAppearance hover{};
    ButtonAppearance onclicked{};

    /// 当前三态中的一个。初始是 Normal。
    ButtonState state = ButtonState::Normal;

    /// 指针现在在不在按钮里。它一直更新（即使正在按下）：
    /// 抬起时"该回 Hover 还是 Normal"问的就是它。
    bool hovered = false;

    /// 现在按着没。与 `hovered` 分开存：拖拽时"按着"和"在里面"是两件事。
    bool pressed = false;

    /**
     * 现在**实际画出来**的颜色。
     *
     * 没有过渡在跑时它恒等于当前状态的颜色；有过渡时它是插值出来的中间色。
     * **渲染取的是它**，而不是 `Current().color`（那是目标状态的颜色）。
     */
    std::uint32_t displayColor = 0xFF000000u;

    /**
     * 现在**实际生效**的变换（平移 / 旋转 / 缩放），与 `displayColor` 同步推进。
     *
     * 它属于**变换通道**：绘制时把顶点过一遍正变换、命中时把点过一遍逆变换。
     * 本地几何（形状 / 尺寸）完全不动，所以命中表不用重烘——这是静态组件也能
     * 有"移动 / 旋转"动画的前提（docs/InputDesign.md §11）。
     */
    TransformSpec displayTransform{};

    /**
     * 现在**实际生效**的尺寸（T-5 的布局通道），与颜色 / 变换同步推进。
     *
     * 0 表示"这个状态没写尺寸"——那时取组件自己的 `width`/`height`（`_baseWidth`
     * / `_baseHeight`）。插值用**同一个 k**：颜色、变换、尺寸是同一个进度的三个
     * 侧面，分三段算迟早对不齐。
     */
    float displayWidth = 0.0f;
    float displayHeight = 0.0f;

    /// 从配置展开三态，并把状态机与过渡都复位。构造时调一次。
    void Reset(const ButtonData& data) noexcept {
        normal = data.normal;
        hover = data.hover;
        onclicked = data.onclicked;
        _baseWidth = data.width;
        _baseHeight = data.height;
        hovered = false;
        pressed = false;
        state = ButtonState::Normal;
        // 颜色统一走 `TargetColor`：配了 token 就查运行期主题，没配就是烘好的字面量。
        displayColor = TargetColor(ButtonState::Normal);
        displayTransform = normal.transform;
        displayWidth = EffectiveWidth(ButtonState::Normal);
        displayHeight = EffectiveHeight(ButtonState::Normal);
        fromColor = displayColor;
        fromTransform = displayTransform;
        fromWidth = displayWidth;
        fromHeight = displayHeight;
        elapsed = 0.0f;
        duration = 0.0f;
        _pendingRepaint = false;
        _pendingHit = false;
        _themeVersion = ThemeVersion();
    }

    /**
     * 某个状态**真正要画**的颜色。
     *
     * 配置写了 `$token` 时 `appearance.color` 只是"默认主题下的值"，
     * 真正的颜色要在运行期按当前主题重查（`theme/InkingTheme.h`）；
     * 没写 token 就是那个字面量本身。**颜色只在这里取**——三态、过渡、收尾
     * 全走这一个入口，否则"换肤后一部分对、一部分还是旧色"。
     */
    std::uint32_t TargetColor(ButtonState which) const noexcept {
        const ButtonAppearance& appearance = For(which);
        return ResolveThemeColor(appearance.themeColor, appearance.color);
    }


    /// 某个状态下的生效尺寸：状态里写了就用它，没写就用组件自己的。
    int EffectiveWidth(ButtonState which) const noexcept {
        const float value = For(which).width;
        return value > 0.0f ? static_cast<int>(value + 0.5f) : _baseWidth;
    }

    int EffectiveHeight(ButtonState which) const noexcept {
        const float value = For(which).height;
        return value > 0.0f ? static_cast<int>(value + 0.5f) : _baseHeight;
    }

    /// 现在**画出来 / 判命中**的尺寸（四舍五入到整数像素，最小 0）。
    int DisplayWidth() const noexcept {
        // 还没 Reset（displayWidth == 0）时退回组件自己的尺寸：构造期就有人问
        // （锚点推导、场景登记），那时不能给 0。
        return displayWidth > 0.0f
                   ? static_cast<int>(displayWidth + 0.5f)
                   : _baseWidth;
    }

    int DisplayHeight() const noexcept {
        return displayHeight > 0.0f
                   ? static_cast<int>(displayHeight + 0.5f)
                   : _baseHeight;
    }

    /**
     * @brief 组件**自己那份尺寸被外部改了**（布局 / 自测量）时同步一次。
     *
     * 为什么必须有它（0.4.0 A4/B1）：`GetWidth()` 走的是 `DisplayWidth()`，
     * 而后者读的是 `_baseWidth` / `displayWidth`——布局把新尺寸写进组件的
     * `_width` 之后，这里不同步就出现"布局算对了、`GetWidth()` 还是旧值"：
     * `fillMain` / `Stretch` 全都**不收敛且不报错**（核的抽屉只能整树重建，
     * 正是这个）。
     *
     * 两维都按当前状态的生效尺寸重算；**状态里写了尺寸的维度不动**
     * （`EffectiveWidth` 会自己返回配置那份，行为与以前一致）。
     */
    void SyncBaseSize(int width, int height) noexcept {
        if (_baseWidth == width && _baseHeight == height) {
            return;
        }
        _baseWidth = width;
        _baseHeight = height;
        // display 两份是"正在显示的尺寸"：只有**没有每态尺寸动画**时才跟着走
        // （有动画时它是插值出来的，下一帧 `Advance` 会自己收敛，硬写会抖）。
        if (!HasSizeAnimation()) {
            displayWidth = static_cast<float>(EffectiveWidth(state));
            displayHeight = static_cast<float>(EffectiveHeight(state));
            fromWidth = displayWidth;
            fromHeight = displayHeight;
        }
    }

    /// 三个状态里有没有哪个改了尺寸（改了才需要"每帧现算位置"与命中脏）。
    bool HasSizeAnimation() const noexcept {
        const int baseWidth = EffectiveWidth(ButtonState::Normal);
        const int baseHeight = EffectiveHeight(ButtonState::Normal);
        return EffectiveWidth(ButtonState::Hover) != baseWidth
            || EffectiveHeight(ButtonState::Hover) != baseHeight
            || EffectiveWidth(ButtonState::Pressed) != baseWidth
            || EffectiveHeight(ButtonState::Pressed) != baseHeight;
    }

    /// 某一个状态对应的外观。
    const ButtonAppearance& For(ButtonState which) const noexcept {
        switch (which) {
            case ButtonState::Hover:
                return hover;
            case ButtonState::Pressed:
                return onclicked;
            case ButtonState::Normal:
            default:
                return normal;
        }
    }

    /**
     * 当前状态那一份外观——注意它是**目标**那一份（配置里写的）。
     * 过渡进行中它和屏幕上正在显示的颜色不是一回事：要那个看 `displayColor`。
     */
    const ButtonAppearance& Current() const noexcept { return For(state); }

    /**
     * 按下 > 悬停 > 通常。
     *
     * 按下期间指针滑出去了也还是"按下"：用户按着不放往外拖，
     * 按钮不该看起来已经松开了。
     */
    static ButtonState Resolve(bool isHovered, bool isPressed) noexcept {
        if (isPressed) {
            return ButtonState::Pressed;
        }
        return isHovered ? ButtonState::Hover : ButtonState::Normal;
    }

    /// 按当前两个标志重算三态；返回**三态真的变了**没有。
    bool ResolveFromFlags() noexcept {
        const ButtonState next = Resolve(hovered, pressed);
        if (next == state) {
            return false;
        }
        state = next;
        // 三态一变就起一段过渡。时长取**新状态自己那份**：
        // 这样 normal→hover 与 hover→normal 可以各配各的（D-1「过场段各自带时长」）。
        BeginTransition(For(state).transitionSeconds);
        return true;
    }

    /**
     * 幂等写入口：指针进出。
     *
     * @return 三态真的变了才返回 true。标志变了但三态没变（例如正按着的时候
     *         指针滑出去）也返回 false——那时连颜色都不用换。
     */
    bool SetHovered(bool value) noexcept {
        if (hovered == value) {
            return false;
        }
        hovered = value;
        return ResolveFromFlags();
    }

    /// 幂等写入口：按下 / 抬起。返回值语义同 SetHovered。
    bool SetPressed(bool value) noexcept {
        if (pressed == value) {
            return false;
        }
        pressed = value;
        return ResolveFromFlags();
    }

    /// 现在有一段过渡在跑吗。
    bool IsTransitioning() const noexcept { return duration > 0.0f; }

    /**
     * 过渡的**线性**进度：0 = 刚开始，1 = 已到位。没有过渡在跑时恒为 1。
     *
     * 这里是**没过缓动**的那个 k——缓动只改变"画到哪儿了"，不改变"走了多久"。
     * 要"画面上的进度"看 `displayColor` / `displayTransform`（那是过了缓动的），
     * 自检里也是拿它们两个来验缓动生效没有。
     */
    float TransitionProgress() const noexcept {
        if (duration <= 0.0f) {
            return 1.0f;
        }
        const float k = elapsed / duration;
        return k < 0.0f ? 0.0f : (k > 1.0f ? 1.0f : k);
    }

    /**
     * 每**渲染帧**推进一次过渡。
     *
     * @param deltaSeconds 真实 delta（不是固定逻辑步）——过渡要跟画面同拍。
     * @return 这一帧显示颜色变了（含"这一帧刚好走完"）才返回 true。
     *         这个返回值就是将来帧末标脏 / 重绘要用的信号（`TaskGuide.md` 的 T-3）。
     *
     * 中途再切三态不需要特殊处理：`BeginTransition` 每次都把**当前显示色**当起点，
     * 所以打断过渡天然是平滑的（D-1「不做打断过渡」= 不为它加机制，
     * 而不是"打断时会跳"）。
     */
    /**
     * 一帧过渡推进的结果：**变了什么**。
     *
     * 分成两项是为了让调用方（组件的 `onAnimationTick`）标对脏——
     * 这两件事的消费点完全不同（`InkingAnchor` 里那两个标记的注释有对照表）：
     *
     *   - `repaint`：画面变了 → **重绘脏**（帧末记账，5 帧没动静就清）；
     *   - `hit`    ：变换变了 → **命中脏**（同一个点可能落到别的组件上，悬停要重算）。
     *
     * 只用一个 bool 的话，"颜色变了"也会去标命中脏，于是每帧白重建一次绘制列表——
     * 动画和标脏接上以后最容易付的冤枉代价就是这个。
     */
    struct AdvanceResult {
        bool repaint = false;  ///< 颜色或变换变了：这一帧的画面和上一帧不同
        bool hit = false;      ///< 变换变了：命中结果可能跟着变（表不用重烘）
    };

    AdvanceResult Advance(float deltaSeconds) noexcept {
        // 先结清"当场换掉显示值"留下的账（没配过渡的三态切换，见 BeginTransition）。
        // 必须在 duration 判断**之前**：那种情况 duration 就是 0，
        // 先返回的话这笔账永远报不出去——画面看着没问题（每帧全量重画），
        // 但标脏协议上就漏了一条"画面变了"，将来做顶点缓存时会拿着旧顶点不撒手。
        AdvanceResult result;
        result.repaint = _pendingRepaint;
        result.hit = _pendingHit;
        _pendingRepaint = false;
        _pendingHit = false;

        // 主题变了：三态的颜色要按新表重算，而且**当场落到当前状态**。
        // 位置必须在下面那个 `duration <= 0` 的提前返回**之前**——没配过渡的
        // 按钮 duration 恒为 0，放在后面就永远刷不到（画面会一直是旧配色，
        // 但没有任何报错）。
        //
        // 不做"跨主题的颜色过渡"：那会让人看见深色→浅色的中间灰，而主题切换
        // 在用户眼里是瞬时的。所以这里直接把 duration 清零。
        const std::uint64_t themeVersion = ThemeVersion();
        if (_themeVersion != themeVersion) {
            _themeVersion = themeVersion;
            const std::uint32_t before = displayColor;
            displayColor = TargetColor(state);
            fromColor = displayColor;
            duration = 0.0f;
            elapsed = 0.0f;
            result.repaint = result.repaint || (displayColor != before);
        }

        if (duration <= 0.0f) {
            return result;
        }

        // 先留一份"上一帧的显示值"，最后拿它比对出这一帧到底变没变。
        const std::uint32_t previousColor = displayColor;
        const TransformSpec previousTransform = displayTransform;
        const float previousWidth = displayWidth;
        const float previousHeight = displayHeight;

        elapsed += deltaSeconds > 0.0f ? deltaSeconds : 0.0f;
        const bool finished = elapsed >= duration;
        // 缓动**只改变进度**（TaskGuide.md 的 T-2）：时长、起点、终点都不动，
        // 所以这里只是把线性的 k 过一遍曲线，插值本身一个字没改。
        // 曲线取**目标状态**那份（和时长同一个来源：过场段各自带一份）。
        const float linear = finished ? 1.0f : (elapsed / duration);
        const float k = ApplyEasing(For(state).easing, linear);

        // 变换与颜色**同一段时长、同一个进度**：它们是"这个状态的样子"的两个侧面，
        // 分成两段动画只会让人配两次时长、还可能对不齐。
        if (finished) {
            // 收尾：直接落到目标值，免掉插值的最后一点浮点误差。
            displayColor = TargetColor(state);
            displayTransform = For(state).transform;
            displayWidth = static_cast<float>(EffectiveWidth(state));
            displayHeight = static_cast<float>(EffectiveHeight(state));
            duration = 0.0f;
            elapsed = 0.0f;
        } else {
            displayColor = MixColor(fromColor, TargetColor(state), k);
            displayTransform = LerpTransform(fromTransform, For(state).transform, k);
            // 尺寸与颜色/变换**同一个进度**：T-5 之后它也是"这个状态的样子"的一部分。
            displayWidth = fromWidth
                + (static_cast<float>(EffectiveWidth(state)) - fromWidth) * k;
            displayHeight = fromHeight
                + (static_cast<float>(EffectiveHeight(state)) - fromHeight) * k;
        }

        result.repaint = result.repaint || (displayColor != previousColor)
            || !SameTransform(displayTransform, previousTransform)
            || displayWidth != previousWidth || displayHeight != previousHeight;
        // 尺寸变了，命中区域就变了：同一个点可能落到别的组件上（和变换同一条理由）。
        result.hit = result.hit
            || !SameTransform(displayTransform, previousTransform)
            || displayWidth != previousWidth || displayHeight != previousHeight;
        return result;
    }

private:
    /// 过渡的起点色 / 起点变换 / 已用时长 / 总时长。
    /// `duration == 0` 表示没有过渡在跑。
    std::uint32_t fromColor = 0xFF000000u;
    TransformSpec fromTransform{};
    float fromWidth = 0.0f;
    float fromHeight = 0.0f;
    int _baseWidth = 0;   ///< 组件自己的尺寸（状态没写尺寸时用它）
    int _baseHeight = 0;
    float elapsed = 0.0f;
    float duration = 0.0f;

    /**
     * 起一段过渡。
     *
     * 起点取**当前显示值**（不是上一状态的配置值）：过渡走到一半再切状态时，
     * 从"眼睛现在看到的"接着走，不会先跳回去再出发。
     */
    void BeginTransition(float seconds) noexcept {
        fromColor = displayColor;
        fromTransform = displayTransform;
        fromWidth = displayWidth;
        fromHeight = displayHeight;

        const std::uint32_t targetColor = TargetColor(state);
        const TransformSpec targetTransform = For(state).transform;
        const float targetWidth = static_cast<float>(EffectiveWidth(state));
        const float targetHeight = static_cast<float>(EffectiveHeight(state));
        if (seconds <= 0.0f
            || (fromColor == targetColor
                && SameTransform(fromTransform, targetTransform)
                && fromWidth == targetWidth && fromHeight == targetHeight)) {
            // 没配过渡、或者颜色与变换本来就一样：直接到位，连计时都不用起。
            //
            // 直接到位意味着**显示值当场就变了**，而这次变化不发生在 Advance 里，
            // 所以先记一笔待报告（Advance 下一次调用时会把它报出去）。
            // 漏了这一笔，"没配过渡的三态切换"就永远标不上重绘脏。
            _pendingRepaint = _pendingRepaint || (displayColor != targetColor)
                || !SameTransform(displayTransform, targetTransform)
                || displayWidth != targetWidth || displayHeight != targetHeight;
            _pendingHit = _pendingHit
                || !SameTransform(displayTransform, targetTransform)
                || displayWidth != targetWidth || displayHeight != targetHeight;

            duration = 0.0f;
            elapsed = 0.0f;
            displayColor = targetColor;
            displayTransform = targetTransform;
            displayWidth = targetWidth;
            displayHeight = targetHeight;
            return;
        }
        duration = seconds;
        elapsed = 0.0f;
    }

    /// "显示值被当场换掉了、还没报告出去"的那笔账（见 BeginTransition）。
    bool _pendingRepaint = false;
    bool _pendingHit = false;

    /**
     * 颜色是按哪个主题版本算出来的。
     *
     * `Advance` 拿它和 `ThemeVersion()` 比：不一样就说明装了表 / 切了主题 /
     * 清了表，三态颜色要重算。**稳态成本 = 一次 uint64 比较**，不查表。
     */
    std::uint64_t _themeVersion = 0;
};

}  // namespace inking
