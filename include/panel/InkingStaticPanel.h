#pragma once

// InkingStaticPanel —— **第一个"整块在动"的容器组件**（抽屉 / 下拉 / 弹窗那一档），
// 也是"容器该怎么写"的样板（模板讲解见 `docs/ComponentGuide.md`）。
//
// 它示范的三件事，恰好是容器与普通组件的全部差别：
//
//   1. **变换作用于整棵子树**：只要重写 `GetSubtreeTransform()` 返回当前显示
//      变换就够了。提交层（`InkingWidget::SubmitToRenderer`）会把祖先的子树
//      变换复合进每一笔绘制，所以**子节点一个字都不用改**，也不用各自知道
//      "我在动"。文字 / 图片 / 形状三条绘制路径都吃这一份（同一个槽）。
//
//   2. **命中不进表，由容器递归解答**：`OwnsSubtreeHit()` 返回 true 之后，
//      子孙不再进全局命中表（它们的绝对包络会随容器变换过期——那正是"要重烘"
//      的来源），改由本类的 `HitTest()` 递归问子树。查询落到容器上时，答案
//      与渲染同源；而**命中表的条目数一条都不变**（容器只登记一条盖住整棵
//      子树的包络），抽屉来回拉也不会重烘表（`InputDesign.md` §9 的第三档）。
//
//   3. **容器不解释指针**：开合是内容状态，由业务调 `SetOpen` / `Toggle`
//      决定；容器不重写 `onPointerHover` 那一组钩子（基类的空实现就够了）。
//      于是"点面板就把面板藏起来"这种业务规则留在场景里，
//      容器本身只管搬运。
//
// 它是**静态档**：开合期间的几何一个字节都不动（只改变换），所以它在命中表里
// 一直有位置，也不需要 `Resize` 那些写入口——`src/inking.cpp` 里有编译期断言钉着。

#include <widget/InkingStaticWidget.h>
#include <panel/PanelData.h>
#include <panel/PanelLook.h>

namespace inking {

class InkingStaticPanel : public InkingStaticWidget {
public:
    /**
     * 手写数据构造：不走配置，直接把一份 `PanelData` 交给它。
     *
     * 配置驱动那条路（`CXXCSS/Panel/<名字>.json` → 生成类）是任务 A2：
     * 生成器目前硬绑 Button，所以这一轮面板的配置在代码里手写。
     *
     * @param parent 上级锚点；非拥有。面板必须在某个场景的子树下才能被渲染。
     * @param data   配置数据（构造时会 `Normalize` 一次，补齐省略的 open 态）。
     */
    InkingStaticPanel(InkingWidget* parent, const PanelData& data);

    // ---------------- [final] 开合写入口（幂等，只改颜色 / 变换） ----------------
    //
    // 名字用 SetOpen / Toggle 而不是 "MouseXxx"：面板的开合**不是指针事件**
    // （见头文件顶上第 3 条）。返回 bool 表示状态真的变了没有——同一个值写两次
    // 什么都不做，正在跑的过渡也不会被打断重起。

    /** 设置开着 / 关着。@return 状态真的变了才返回 true。 */
    bool SetOpen(bool open) noexcept;

    /** 开合取反。@return 状态真的变了才返回 true。 */
    bool Toggle() noexcept;

    /** `SetOpen(true)` 的便利写法（抽屉拉出来）。 */
    bool Open() noexcept { return SetOpen(true); }

    /** `SetOpen(false)` 的便利写法（抽屉收回去）。 */
    bool Close() noexcept { return SetOpen(false); }

    /** 现在是开着吗（**目标**态；过渡进行中也是目标态）。 */
    bool IsOpen() const noexcept;

    /** 当前两态中的一个。 */
    PanelState GetState() const noexcept;

    // ---------------- 读 ----------------

    /// 现在**实际画出来**的颜色（过渡中是插值出来的中间色）。
    std::uint32_t GetDisplayColor() const noexcept;

    /// 现在**实际生效**的变换（过渡中是插值出来的中间值）——它同时作用于子树。
    const TransformSpec& GetDisplayTransform() const noexcept;

    // ---------------- 滚动（B12） ----------------

    /**
     * 重新量"内容多大"（子节点增删 / 尺寸变了之后喊一声）。
     *
     * 和 `Relayout()` 同一条纪律：**显式**、**幂等**，框架不做隐式扫描——
     * 否则"什么时候重算"看不见，嵌套容器还容易重入（§6 第 86 条）。
     */
    void UpdateScrollRange();

    /// 设滚动量（夹进 `[0, range]`；没变化不标脏）。
    void SetScroll(float x, float y);

    /// 滚轮滚一下（转发给 `onPointerWheel`）。钩子是 protected，自检 / 脚本调不到；
    /// 这个公开转发让"一格滚多少"也能被断言（和 B7 的 `TickCaretBlink` 同类）。
    void WheelScroll(float deltaX, float deltaY);

    /// 一条滚动条算出来的几何（绘制与自检共用同一份：没有第二个真相）。
    ScrollBarMetrics GetBarMetrics(bool vertical) const noexcept;

    /// 按下（设计坐标）：点在**滑块**上就进入拖动，点在轨道上就跳过去。
    void PressScrollAt(float designX, float designY);

    /// 拖到某处（设计坐标）：把滑块中心对到指针上（和"点轨道跳过去"同一套算术）。
    void DragScrollTo(float designX, float designY);

    /// 现在正在拖哪条（0 = 没拖，1 = 竖直，2 = 水平）。
    int DraggingAxis() const noexcept { return _dragAxis; }

    float GetScrollX() const noexcept { return _scrollX; }
    float GetScrollY() const noexcept { return _scrollY; }

    /// 可滚范围（内容 − 视口，负数夹到 0）。
    float GetScrollRangeX() const noexcept;
    float GetScrollRangeY() const noexcept;

    /// 量出来的内容尺寸（观测口：自检靠它断言"内容比视口大多少"）。
    float GetContentWidth() const noexcept { return _contentWidth; }
    float GetContentHeight() const noexcept { return _contentHeight; }

    /// 形状定义：渲染 / 命中 / 包围盒三者都从它派生。
    const ShapeSpec& GetShape() const noexcept;

    /// 现在有一段开合过渡在跑吗。自检与"画面稳了没有"用得上。
    bool IsTransitioning() const noexcept;

    /// 过渡的线性进度（0~1）；没有过渡在跑时恒为 1。
    float TransitionProgress() const noexcept;

    /// 构造时用的那份配置（已经 `Normalize` 过）。
    const PanelData& GetData() const noexcept;

    // ---------------- [√] 命中与子树（容器的三个关键重写） ----------------

    /**
     * 指针在这个点上吗（世界坐标）。
     *
     * 两步，顺序不能换：
     *   1. **自己本体**：撤祖先变换 + 减自己的绝对位置 + 过一次自己变换的逆，
     *      再问形状（`detail::HitTestShape`，和绘制同一份定义）；
     *   2. **子树**：把**世界坐标原样**交给每个子节点问一遍。这样做是对的，
     *      因为子节点的 `AncestorAffine` 里已经含了**我这一层变换**——它自己
     *      会撤干净。反过来说，如果我先把点换算到自己的本地空间再递归，
     *      子节点会**多撤一次**我的变换，于是"画的和点的"就差一个变换。
     *
     * 注意这里只回答"几何上在不在"，**不回答"谁在最上面"**：层级由场景
     * （命中表 + `InkingWidget::IsAbove`）统一裁决，容器不自作主张。
     */
    bool HitTest(float worldX, float worldY) const override;

    /**
     * @brief 深挖子树（A8a）：命中表只登记容器一条（子孙不进表），所以"面板 /
     * 抽屉 / 弹窗里的按钮点得到"要靠这一步——场景查到容器之后，再问一次
     * "你子树里最上面那个该收事件的是谁"。
     *
     * 与 `HitTest` 的分工：`HitTest` 只回答"几何上在不在（含子树）"，
     * 这一步回答"**是谁**"。所以两者的遍历顺序不同：这里**子树优先**
     * （子节点画在容器之上），而 `HitTest` 本体重命中就短路。
     *
     * 子树里只有 `PassThrough`（标签）命中时返回 `nullptr`：让场景保留
     * "容器自己"那份答案，不去改全局三态的语义（那是局部层级的事，A8b）。
     */
    InkingWidget* HitTestNode(float worldX, float worldY) noexcept override;

    /**
     * 命中索引用的保守包络：**布局空间里整棵子树的并集**，再按两个状态的变换
     * 各转一次取并集（`detail::PanelHitEnvelope`）。
     *
     * 两件事都必须在：包络要盖住子树（否则表里登记的框比实际小，边缘漏命中），
     * 也要盖住**所有可能的状态**（抽屉开着和关着差一个身位，只按当前状态登记，
     * 拉出来之后表里就找不到它了）。
     */
    ShapeBounds GetHitEnvelope() const override;

    /// 当前变换**同时作用于子节点**（`InkingWidget::GetSubtreeTransform`）。
    TransformSpec GetSubtreeTransform() const noexcept override;

    /// B12：裁剪窗口是**容器本体**，不含滚动位移——不然内容滑出去还看得见
    /// （`_look.displayTransform` 只有开合动画那一份）。
    TransformSpec GetClipTransform() const noexcept override {
        return _look.displayTransform;
    }

    /// 本容器自己负责整棵子树的命中：子孙不进全局命中表，由 `HitTest` 递归解答。
    bool OwnsSubtreeHit() const noexcept override { return true; }

    /// B12：配了 `scroll` 的容器**优先拿滚轮**——指针在子节点上滚也滚得动
    /// （场景沿父链找最近的声明者，见 `InkingWidget::FindSubtreeWheelOwner`）。
    bool HandlesSubtreeWheel() const noexcept override {
        return _data.scroll.enabled;
    }

    /**
     * 裁剪子孙吗（A5）？由 `PanelData::clipChildren` 决定，默认不剪。
     *
     * 这一条就是"下拉 / 滚动 / 抽屉"的窗口：声明之后，超出本体的子节点
     * **画出来被剪、点起来也被剪**（两边同源），而子节点自己一个字都不用改。
     */
    bool HasSubtreeClip() const noexcept override { return _data.clipChildren; }

    /// 裁剪区（B12）：**配了滚动就把条带扣掉**——内容视口里不该包含滚动条那一条。
    ///
    /// 为什么这样最省事：子节点按 z 序画在容器那一笔**之上**（它们本来就该压在
    /// 容器底色上），所以画在 `onRender` 里的滚动条会被文字盖住。与其再加一条
    /// "画在子孙之后"的覆盖通道，不如让**内容根本画不到条带上**：条带区域就只剩
    /// 背景 + 轨道 + 滑块。绘制与命中的裁剪用的是同一份，两边自动一致。
    ShapeBounds GetSubtreeClipBounds() const noexcept override {
        return ShapeBounds{0.0f, 0.0f, ContentViewportWidth(),
                           ContentViewportHeight()};
    }

    /// 内容视口（扣掉**可见**条带之后的可视区域）：滚动范围与裁剪都按它算。
    float ContentViewportWidth() const noexcept;
    float ContentViewportHeight() const noexcept;

protected:
    /// 按当前显示的颜色与变换画自己那一笔（形状填充，边缘抗锯齿）。
    /// 子节点由场景逐个提交，各自带上"祖先子树变换"——这里不用管它们。
    void onRender(float pixelX, float pixelY) const override;

    /// 滚轮（B12）：`deltaX/deltaY` 是**一次性的滚动增量**（A4 那套），
    /// 一格滚 `ScrollSpec::wheelStep` 设计像素。
    void onPointerWheel(float deltaX, float deltaY) override;

    /// 按下 / 抬起（B12）：按下时决定"拖滑块还是点轨道跳过去"。
    void onPointerPress(bool press) override;

    /// 拖动（B12）：把滑块中心拖到指针处。
    void onPointerDrag(float designX, float designY, float deltaX,
                       float deltaY) override;

    /**
     * 每**渲染帧**一次：推进开合过渡。
     *
     * 与按钮同一条通道（`InkingWidget::TickAnimation` → `InkingScene::TickFrame`，
     * 只有活跃场景会跑）。静态组件收不到动态档的逻辑步 `Tick`，所以过渡不能
     * 放在那里。
     */
    void onAnimationTick(float deltaSeconds) override;

private:
    /// 把 data 里的字段搬到基类与自己的成员上。
    void applyData(const PanelData& data);

    /// 把当前显示色同步到基类那份 color：从基类窗口看过去得是同一件事。
    void syncBaseColor() noexcept;

    PanelData _data{};

    // B12：滚动量（设计像素）与量出来的内容尺寸。
    // 刻意**不写进** `PanelData` 之外的地方：`GetSubtreeTransform()` 直接读它们，
    // 所以绘制 / 命中 / 子交互都是"当前值"，没有第二份快照要对账。
    float _scrollX = 0.0f;
    float _scrollY = 0.0f;
    float _contentWidth = 0.0f;
    float _contentHeight = 0.0f;

    /// 正在拖的轴（0 = 没拖，1 = 竖直，2 = 水平）。
    int _dragAxis = 0;

    /// 两态外观 + 状态机 + 过渡（静态 / 动态 Panel 共用的那一份，见 PanelLook.h）。
    PanelLook _look{};
};

}  // namespace inking
