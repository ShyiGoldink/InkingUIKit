#pragma once

// 动态档面板（G-07）：几何可变、可滚动的容器。
//
// ---------------------------------------------------------------------------
// 与静态版 `InkingStaticPanel` 的关系：**兄弟，不是父子，也不是 `Like`**
// ---------------------------------------------------------------------------
//
//   - 两档的**外观那一笔**早就在 `src/panel/PanelPaint.cpp` 共用了
//     （`PaintPanelAppearance` / `PaintSurfaceShadow` / `PaintSurfaceStroke` /
//     `PanelHitEnvelope`），所以 `onRender` / `onRenderDecoration` 只是 3~10 行接线；
//   - 两档的**两态外观与过渡**本来就在 `InkingPanelLook.h` 共用（`InkingPanelLook _look`）；
//   - 剩下的（**状态机入口 / 滚动 / 裁剪 / 容器命中**）语义不同，本类**自己实现**
//     （见 `TaskGuide-G.md` 附录 A.2：能抽的只有 11 行胶水，抽出来不划算）。
//
//   为什么不继承 `InkingStaticPanel`：那条链上带着"静态几何定死"的铁律（没有
//   几何写入口），而这一档存在的理由正是几何写入口（`Resize` / `ChangeOffset` /
//   两个锚点）。所以两档在 Widget 层就是兄弟。
//
// ---------------------------------------------------------------------------
// 关键决定一：滚动条拖动从 `onPointerStateChanged()` **自判**（这是唯一入口）
// ---------------------------------------------------------------------------
//
//   动态档收不到 `PushPointer*`（场景对动态节点直接不派发，`dispatchTarget` 返回
//   nullptr），只有"指针真的变了"那一帧的一次 `onPointerStateChanged()`。
//   所以静态版那对 `onPointerPress` + `onPointerDrag` 在这里折成一次判定：
//
//     PointerProbe.Update(hit, 两个键的按下态) →
//       primary.pressed  → `PressScrollAt(场景指针)`（判"拖滑块还是点轨道"）
//       primary.released → 结束拖动（捕获释放）
//       还按着           → `DragScrollTo(场景指针)`（滑块中心跟着指针）
//
//   与 `InkingFixedPopup` / `InkingDynamicImage` 同一套用法（`inking_detail::PointerProbe`），
//   语义与静态档逐条一致：按下之后滑出去**仍然算按着**（捕获）；在别处按下、
//   按着挪进来**不算**这次按下。
//
// ---------------------------------------------------------------------------
// 关键决定二：滚轮走**渲染帧**这条通道（0.9.1 T-03；此前是"不做滚轮"）
// ---------------------------------------------------------------------------
//
//   0.9.0 的结论是"动态档没有可靠的读取时机"，**半对**。探针
//   （`build/_wheelprobe`，60 帧 / 3 次喂入）把这句话拆成了两半：
//
//   - **逻辑步读不可靠**：喂 30.0，`onTick` 只读到 20.0（有一帧滚轮被帧末
//     清掉了），61 帧里还有 11 帧逻辑步为 0 —— 这条**成立**，所以本类
//     **依然不在 `onTick` 里读滚轮**，别顺手改回去；
//   - **每渲染帧一次的回调完全可靠**：`onAnimationTick` 60 帧调用 60 次，
//     3 次喂入全部读到，累计 30.0 == 喂入量。它排在"指针派发之后、帧末清账
//     之前"（`InkingWindow.cpp` 的主循环顺序），所以读到的正是本帧那一份。
//
//   于是滚轮在这里有**两条**入口，算术只有一份（`InkingApplyWheelToScroll`）：
//
//   | 入口                | 什么时候到                              | 谁触发 |
//   | ------------------- | --------------------------------------- | ------ |
//   | `onPointerWheel`    | 指针压在**静态子孙**上（推通道，容器优先） | 场景   |
//   | `onAnimationTick`   | 指针压在**面板自己**的空白区（拉通道）     | 本类   |
//
//   两条入口可能在同一帧对同一次滚轮都开火 ⇒ **双滚**（"滚得快一倍"，最难查）。
//   所以拉通道必须先问归属，四道门一道都不能少（顺序也不能反）：
//
//     ① `scroll.enabled` 为假 → 返回（最便宜的判断放最前）
//     ② 指针不在窗口里 → 返回（滚轮不该在窗口外生效）
//     ③ 本帧增量为 0 → 返回
//     ④ `GetPointerWheelTarget() != nullptr` → 返回。**这一条必须先于自判**：
//        指针底下压着静态子孙时，推通道已经交给"最近的声明者"了（若那个声明者
//        就是本面板，`onPointerWheel` 刚刚执行过），再拉一次就是双滚；
//     ⑤ 指针下最上面的动态节点不是本面板（也不是本面板的子孙）→ 返回
//        （两个动态面板重叠时只有上面那个滚）。
//
//   `HandlesSubtreeWheel()` 现在**必须**重写（0.9.0 那句"没有意义"已经过时）：
//   它是"指针压在静态子孙上"那一档的唯一入口，不重写就是"滚到有按钮的地方就停"。
//   返回值跟着 `scroll.enabled` 走——不能滚的面板不该截滚轮，否则它上面的一切
//   滚轮都静默消失。
//
//   **已知限制（本轮不解决）**：嵌套两层滚动容器时外层生效（见任务书 §7）。
//   要修得按指针路径逐层仲裁，而不是"最近的声明者"。本类只保证一层是对的。
//
// ---------------------------------------------------------------------------
// 容器契约（与静态版同一条语义，实现自己写）
// ---------------------------------------------------------------------------
//
//   - `OwnsSubtreeHit() = true` ⇒ **子孙不进全局命中表**，由 `HitTest` 递归解答；
//     `HitTestNode()` **不用重写**（基类那份已经是"子树优先"的容器语义，
//     静态版抄的那一份是 0.4.0 之前基类还没有它时留下的）；
//   - `GetSubtreeTransform()` = 显示变换 ∘ **减去滚动量**（内容跟着滚）；
//     `GetClipTransform()` = **只有显示变换**（裁剪窗口不跟内容跑，否则滚出去还看得见）；
//   - `GetSubtreeClipBounds()` = 内容视口（**扣掉可见的滚动条**）；
//   - 本身是动态档 ⇒ **不进命中表**，占地记成"洞"（`GetHitEnvelope()` 现算）。
//
// ---------------------------------------------------------------------------
// 包络成本（§2.1 第 3 条）：动态容器的包络是"整棵子树并集"，每帧刷洞都要算
// ---------------------------------------------------------------------------
//
//   子节点多的动态面板，这条是唯一会随子树规模变贵的地方。所以本类记一个
//   **调用计数**（`GetHitEnvelopeCallCount()`），自检断言"一帧内不超过 1 次/节点"
//   ——将来谁把它做成每帧多次，计数当场红，而不是等帧时间悄悄掉下去
//   （任务书 §2.1 的纪律：要量性能就量**计数**，不量帧时间）。

#include <dataStruct/InkingAffine.h>
#include <dataStruct/InkingLayout.h>
#include <dataStruct/InkingScroll.h>
#include <detail/InkingPointerProbe.h>
#include <panel/PanelData.h>
#include <panel/PanelLook.h>
#include <widget/InkingDynamicWidget.h>

#include <cstdint>

/// 模块名常量：日志与自检要用（约定：每个模块都暴露一个）。
inline constexpr const char* InkingDynamicPanelModuleName = "InkingDynamicPanel";

/**
 * @brief 几何可变、可滚动的容器（G-07）。
 *
 * 外观与开合过渡与静态版**同一份实现**（`InkingPanelLook` + `PanelPaint`），差别只有两条：
 * 几何有写入口（继承来的四个），以及滚动拖动从指针变化通知里自判。
 */
class InkingDynamicPanel : public InkingDynamicWidget {
public:
    /**
     * @param parent 上级节点；非拥有。
     * @param data   配置（构造时 `Normalize()` 一次；`startOpen` 在这里生效）。
     */
    InkingDynamicPanel(InkingWidget* parent, const InkingPanelData& data);

    // ---------------- 开合（与静态版同名同义） ----------------
    //
    // 开合是**内容状态**（不是指针三态），由业务喊；过渡只动颜色 / 变换 / 描边 /
    // 阴影，**本地几何一个字节都不动**（`TaskGuide.md` D-1 的硬约束）。

    /// 设置开着 / 关着（幂等：同一个值写两次什么都不做，也不重起过渡）。
    bool SetOpen(bool open) noexcept;
    bool Toggle() noexcept;
    bool Open() noexcept { return SetOpen(true); }
    bool Close() noexcept { return SetOpen(false); }

    /// 现在是开着吗（**目标**态；过渡进行中也是目标态）。
    bool IsOpen() const noexcept;
    InkingPanelState GetState() const noexcept;

    /// 有一段开合过渡在跑吗 / 它的线性进度（0~1，没有过渡时恒 1）。
    bool IsTransitioning() const noexcept;
    float TransitionProgress() const noexcept;

    // ---------------- 读（与静态版同名同义） ----------------

    /// 形状定义：渲染 / 命中 / 包围盒三者都从它派生。
    const InkingShapeSpec& GetShape() const noexcept;

    /**
     * **目标态**那份外观（配置里写的：颜色 / 过渡 / 变换 / 描边 / 阴影）。
     *
     * 过渡进行中它和屏幕上正在显示的不是一回事——要那个看
     * `GetDisplayColor()` / `GetDisplayTransform()` / `GetDisplayStroke()` /
     * `GetDisplayShadow()`（与静态版同一对概念，见 `ComponentGuide.md` §1）。
     */
    const InkingPanelAppearance& GetAppearance() const noexcept;

    /// 现在**实际画出来**的颜色（过渡中是插值出来的中间色）。
    std::uint32_t GetDisplayColor() const noexcept;

    /// 现在**实际生效**的变换（过渡中是插值出来的中间值）——它同时作用于子树。
    const InkingTransformSpec& GetDisplayTransform() const noexcept;

    const InkingStrokeSpec& GetDisplayStroke() const noexcept;
    const InkingShadowSpec& GetDisplayShadow() const noexcept;

    /// 构造时那份配置（已经 `Normalize` 过）。
    const InkingPanelData& GetData() const noexcept;

    // ---------------- 滚动（与静态版同名同义） ----------------

    /**
     * 重新量"内容多大"（子节点增删 / 尺寸变了之后喊一声）。
     *
     * 与 `Relayout()` 同一条纪律：**显式**、**幂等**，框架不做隐式扫描
     * （§6 第 86 条）。视口那一侧是活的（`ContentViewportHeight()` 直接读
     * `GetHeight()`），所以 `Resize` 之后**不用**重新量内容。
     */
    void UpdateScrollRange();

    /// 设滚动量（夹进 `[0, range]`；没变化不标脏）。
    void SetScroll(float x, float y);

    /// 一条滚动条算出来的几何（绘制与自检共用同一份：没有第二个真相）。
    InkingScrollBarMetrics GetBarMetrics(bool vertical) const noexcept;

    /**
     * 按下（**世界 / 设计坐标**）：点在滑块上就进入拖动，点在轨道上就跳过去。
     *
     * 坐标先换算成本地面：撤祖先仿射 → 减绝对位置 → 过自身显示变换的**逆**
     * （与命中同源，三步一个不少）。静态版那两行"减 `GetX()/GetY()`"只在
     * "父级在原点、且没有变换"时与这三步等价——本档两样都可能不成立
     * （它能被 `ChangeOffset` / `Resize` 搬走），所以不能照抄。
     */
    void PressScrollAt(float designX, float designY);

    /// 拖到某处（世界坐标）：把滑块中心对到指针上（与"点轨道跳过去"同一套算术）。
    void DragScrollTo(float designX, float designY);

    /// 现在正在拖哪条（0 = 没拖，1 = 竖直，2 = 水平）。
    int DraggingAxis() const noexcept { return _dragAxis; }

    float GetScrollX() const noexcept { return _scrollX; }
    float GetScrollY() const noexcept { return _scrollY; }

    /// 可滚范围（内容 − 视口，负数夹到 0）。
    float GetScrollRangeX() const noexcept;
    float GetScrollRangeY() const noexcept;

    /// 量出来的内容尺寸（观测口）。
    float GetContentWidth() const noexcept { return _contentWidth; }
    float GetContentHeight() const noexcept { return _contentHeight; }

    /// 内容视口（扣掉**可见**条带之后的可视区域）：滚动范围与裁剪都按它算。
    float ContentViewportWidth() const noexcept;
    float ContentViewportHeight() const noexcept;

    // ---------------- 包络成本观测口（§2.1 第 3 条） ----------------

    /// `GetHitEnvelope()` 一共被调用过几次（动态容器这条是"整棵子树并集"）。
    std::uint64_t GetHitEnvelopeCallCount() const noexcept;
    void ResetHitEnvelopeCallCount() noexcept;

    // ---------------- [√] 容器契约 ----------------

    /**
     * 指针在这个点上吗（世界坐标）。与静态版同一条三步半：
     * ① 自己本体（`inking_detail::HitTestShape`，与绘制同一份形状定义）；
     * ①.5 裁剪：落在裁剪区之外 ⇒ 整棵子树都不命中（绘制与命中同源）；
     * ② 子树：**世界坐标原样**交给每个子节点（它自己会撤干净我这一层变换）。
     */
    bool HitTest(float worldX, float worldY) const override;

    /**
     * 命中索引用的**保守包络**（动态档拿它算洞）。
     *
     * 整棵子树在布局空间的并集 × 两个状态的变换，**再加上滚动范围**：内容可以
     * 滚到"上半截露出去"的位置，洞要盖住所有滚动位置，否则滚过之后那一片
     * 点不到（T-19 的静默失效形态）。
     */
    InkingShapeBounds GetHitEnvelope() const override;

    /// 当前变换**同时作用于子节点**，并**减去滚动量**（内容跟着滚）。
    InkingTransformSpec GetSubtreeTransform() const noexcept override;

    /// 裁剪窗口只吃显示变换：滚动位移不算，否则滚出去的内容还看得见（B12 的坑）。
    InkingTransformSpec GetClipTransform() const noexcept override {
        return _look.displayTransform;
    }

    /// 裁剪区：内容视口（扣掉可见条带）——与静态版同一条口径。
    InkingShapeBounds GetSubtreeClipBounds() const noexcept override;

    /// 本容器自己负责整棵子树的命中：子孙不进全局命中表。
    bool OwnsSubtreeHit() const noexcept override { return true; }

    /**
     * B12 的"容器优先"：配了滚动的面板**优先拿滚轮**（0.9.1 T-03）。
     *
     * 声明之后，指针压在**面板里的静态子孙**上时，场景沿父链找到本面板并把
     * 滚轮交给它——否则那段区域的滚轮会被子控件链吞掉，手感上就是
     * "滚到有按钮的地方就停"。
     *
     * 返回值跟着 `scroll.enabled` 走，**不是恒 true**：不能滚的面板声明了
     * 也消费不了，等于把指针下所有滚轮静默吃掉（那比不滚更糟——连里面
     * 真正能滚的子容器都没得滚了）。
     */
    bool HandlesSubtreeWheel() const noexcept override {
        return _data.scroll.enabled;
    }

    /// 剪不剪子孙由 `InkingPanelData::clipChildren` 决定（默认不剪）。
    bool HasSubtreeClip() const noexcept override { return _data.clipChildren; }

protected:
    /// 填充 + 阴影（阴影在形状**之下**）+ 两条滚动条。
    void onRender(float pixelX, float pixelY) const override;

    /// 描边：画在**整棵子树之后**（装饰遍），否则会被子节点盖住。
    void onRenderDecoration(float pixelX, float pixelY) const override;

    /// 本帧有描边要画吗（场景靠它跳过无装饰的节点）。
    bool HasRenderDecoration() const noexcept override {
        return _look.displayStroke.Visible();
    }

    /**
     * 每**渲染帧**一次：**先消费滚轮**（拉通道），再推进开合过渡。
     *
     * 顺序不能反：本帧的滚动条位置要用新的 `_scrollY`，否则条永远慢一帧。
     * 滚轮判定见文件头"关键决定二"（四道门，顺序固定）。
     */
    void onAnimationTick(float deltaSeconds) override;

    /**
     * 滚轮（**推通道**）：指针压在面板里的静态子孙上时由场景转给本面板。
     * 见文件头"关键决定二"的表格——与拉通道共用同一份算术。
     */
    void onPointerWheel(float deltaX, float deltaY) override;

    /**
     * 指针变了（**动态档唯一的输入时机**）：滚动条拖动的全部判定都在这里。
     * 见文件头"关键决定一"。
     */
    void onPointerStateChanged() override;

private:
    /// 把 data 搬进基类与自己的成员。
    void applyData(const InkingPanelData& data);

    /// 把当前显示色同步到基类那份 color（从基类窗口看过去是同一件事）。
    void syncBaseColor() noexcept;

    /// 两条滚动条（画在容器自己的坐标里：不含滚动位移）。
    void paintScrollBars(float pixelX, float pixelY) const;

    /// 世界（设计）坐标 → 本地面：与 `inking_detail::HitTestShape` 的三步同源。
    InkingPoint2 localFromWorld(float worldX, float worldY) const noexcept;

    /**
     * 两条入口共用的**唯一一份**滚轮算术（换算 + 夹紧 + 幂等）。
     *
     * 推通道（`onPointerWheel`）与拉通道（`onAnimationTick`）都走它——
     * 写两份就是"两条入口的手感悄悄分叉"，而两边单独看都对。
     */
    void applyWheelDelta(float deltaX, float deltaY);

    /**
     * 拉通道的归属判定（文件头"关键决定二"的 ①~⑤）。
     *
     * @return 这一帧的滚轮该由本面板消费吗。**只判**，不改滚动量。
     */
    bool shouldConsumeSceneWheel() const;

    InkingPanelData _data{};

    /// 两态外观 + 状态机 + 过渡（静态 / 动态 Panel 共用的那一份，见 InkingPanelLook.h）。
    InkingPanelLook _look{};

    // 滚动量（设计像素）与量出来的内容尺寸。`GetSubtreeTransform()` 直接读它们，
    // 所以绘制 / 命中 / 子交互都是"当前值"，没有第二份快照要对账。
    float _scrollX = 0.0f;
    float _scrollY = 0.0f;
    float _contentWidth = 0.0f;
    float _contentHeight = 0.0f;

    /// 正在拖的轴（0 = 没拖，1 = 竖直，2 = 水平）。
    int _dragAxis = 0;

    /// 指针边沿判定（`detail/InkingPointerProbe.h`）：动态档没人替它记历史。
    inking_detail::PointerProbe _probe{};

    /// 包络调用计数（§2.1 第 3 条的观测口）。`GetHitEnvelope` 是 const，
    /// 所以它是 mutable——计数不是可观测的组件状态，正是 mutable 该用的地方。
    mutable std::uint64_t _envelopeCalls = 0;
};

