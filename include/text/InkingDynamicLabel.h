#pragma once

// 动态档标签（G-05）：文字与几何都能在运行期改的 `InkingStaticLabel`。
//
// ---------------------------------------------------------------------------
// 与静态版的差别：几何能改 + 改尺寸的时机是**显式**的（别自己加第三条）
//
//   1. **几何能改**：继承 `InkingDynamicWidget`，四个写入口都在（`Resize` /
//      `ChangeSelfAnchor` / `ChangeTraceAnchor` / `ChangeOffset`）；静态版的
//      `InkingStaticLabel` 反过来是编译期钉死的（`src/inking.cpp` 的
//      `static_assert(!HasResize<InkingStaticLabel>)` 一族）。
//   2. **`SetText` 不改几何**（关键决定一）：静态版换文字会 `RelayoutParent()`
//      让父容器重排、无布局容器时再自测量隐式撑高；动态版把这件事故意做成
//      显式——高度什么时候变，由调用方喊 `FitToContent()`。理由与 A6 同一条
//      纪律：**框架不做隐式重排，什么时候重排始终可见**。代价照实写：调用方
//      忘了喊，文字变长而框没变 ⇒ 多出来的行被 `InkingTextOverflow::Clip` 裁掉
//      （裁行本身是 0.8.3 `text_clip_by_height` 的既定语义，不是这里新出的错）。
//   3. **输入：没有新的输入**（与 G-03 同一个说法，不是漏做）。标签本来就不吃
//      点击（`SetHitPassThrough(true)`），所以动态档不需要 `PointerProbe`、
//      也没有内置拖动。这条写在这里，免得后来者以为复制时漏了一笔。
//
//   为什么不直接继承 `InkingStaticLabel`：它挂在 `InkingStaticWidget` 那一支，
//   整条链上带着"静态几何定死"这条铁律（没有几何写入口）。动态档要的是
//   `InkingDynamicWidget` 那一支，两者是**兄弟不是父子**——共用靠 `InkingLabelLike`
//   （G-06 已抽好：排版、取色、测量、画字只有那一份）。
//
// ---------------------------------------------------------------------------
// 关键决定二：**在布局容器里 `FitToContent()` 不生效**（会静默失效的坑）
// ---------------------------------------------------------------------------
//
//   布局通道 `InkingWidget::ApplyLayoutRect`（friend 是 `inking_detail::layout::
//   ArrangeSubtree`）会在下一次 `Relayout()` 时把 `_width/_height` **整体重写**。
//   于是在带 `InkingLayoutSpec` 的容器里自己量出来的尺寸，下一帧就被父容器的排布覆盖
//   ——写进去等于没写，而且看不出来（画面只是回到布局给的那个尺寸）。
//
//   处理方式照 A6 的既有纪律：`FitToContent()` 先判"我是否被布局管着"
//   （父链上有有效 `InkingLayoutSpec` 的祖先，判据与 `RelayoutParent()` 完全一致），
//   是则 **返回 false + 记一条 WARN**，**不写几何**——宁可喊一声，也不要写一个
//   下一帧就消失的尺寸。这条与 `RelayoutSelfIfContentSized()` 的"布局容器内一律
//   不自测量"是同一条规矩的两个落点，别修一处忘一处。
//
// ---------------------------------------------------------------------------
// 命中表：不进表；标签本来就不吃点击
// ---------------------------------------------------------------------------
//
//   动态标签是动态档 ⇒ 不进全局命中表，占地记成"洞"（`GetHitEnvelope()` 每帧
//   现算）。但标签的 `HitTest()` 是基类那份恒 false 的实现——"占地"在这里只是
//   绘制区域，不改变遮挡策略：点落在洞里的那个区域时，动态层问一圈没人认，
//   `QueryHit` 回退到静态表的答案（`InkingScene::QueryHit` 的 `dynamicHit ==
//   nullptr` 那条），所以不会"挡住底下的按钮"。
//
// ---------------------------------------------------------------------------
// G-06：排版与取色已并入 `InkingLabelLike`（纯重构，行为零变化）
// ---------------------------------------------------------------------------
//
//   本类只剩**动态档自己的**事：四个几何写入口的构造、`SetText` 不重排、
//   `FitToContent()` / `IsManagedByLayout()`、`onRender` 一行转发。G-05 时那套
//   从静态版抄来的 `LayoutCache` / `LayoutForWidth` / `CacheMatches` /
//   `EffectiveTextColor` 已经搬走，两档不再各存一份。

#include <dataStruct/InkingLayout.h>
#include <dataStruct/InkingTextLayout.h>
#include <text/LabelData.h>
#include <text/LabelLike.h>
#include <widget/InkingDynamicWidget.h>

#include <cstdint>
#include <string>

/// 模块名常量：日志与自检要用（约定：每个模块都暴露一个）。
inline constexpr const char* InkingDynamicLabelModuleName = "InkingDynamicLabel";

/**
 * @brief 文字与几何都能在运行期改的标签（G-05）。
 *
 * 排版、主题取色、`MeasureLayout` 与静态版**同源**（同一份 `InkingLabelData`、同一个
 * 文字层 `InkingMeasureText` / `PaintText`，实现都在 `InkingLabelLike`），差别只有几何与
 * "什么时候改尺寸"。
 *
 * 默认**不吃点击**（`SetHitPassThrough(true)`，与静态版同一条）：标签常压在按钮
 * 或图片上，吃掉点击会让"看得见的按钮点不动"。
 */
class InkingDynamicLabel : public InkingDynamicWidget, public InkingLabelLike {
public:
    /**
     * 手写数据构造（不走配置）。
     *
     * 构造末尾会补一次**自测量**，但只在"没配置"的那一维、且**不在布局容器里**
     * ——与静态版构造末尾那句 `RelayoutParent()` 等价（静态版走 `ApplySelfSize`，
     * 动态版走 `Resize`）。不补的话 `width/height` 留 0 就是一个像素都不画、
     * 也零日志的空白（`0.1.1` T-02 那条）。
     */
    InkingDynamicLabel(InkingWidget* parent, const InkingLabelData& data);

    const InkingLabelData& GetData() const noexcept { return _data; }

    /**
     * @brief 换文字。**不改几何**（关键决定一）。
     *
     * 只做两件事：排好的行作废、标重绘脏。**不标命中脏**——文字不参与命中
     * （`AGENTS.md` §6 第 47 条）。也**不**调 `RelayoutParent()`：那会走到
     * 父容器重排 / 自测量两条隐式路径上，正是动态档要排除的。
     *
     * 要跟着内容改尺寸，就显式喊 `FitToContent()`（见那里对布局容器的处理）。
     */
    bool SetText(std::string text) noexcept;

    /// 换文字色：只标重绘脏（颜色走纹理调制，不重烘纹理）。语义同静态版。
    bool SetTextColor(std::uint32_t color) noexcept;

    /**
     * @brief 排好的行（观测口 + 绘制用）。
     *
     * 按**当前**宿主的宽高排一次并缓存，尺寸没变就直接给上一次的结果。
     * **不推脏**（`AGENTS.md` §6 第 41 条那条"观测口要不要推脏"）。
     */
    const InkingTextLayoutResult& GetTextLayout() const;

    /**
     * @brief A6 的测量钩子：我这段文字要多大。
     *
     * 宽度：有约束就用约束（于是按它换行），没有就用配置宽度。
     * 高度：**排版结果的高度**——撑高就发生在这里（不是 `SetText`）。
     */
    InkingLayoutSize MeasureLayout(const InkingLayoutConstraints& constraints) const override;

    /**
     * @brief 把几何缩到**内容需要的大小**（`Resize` 到测量宽高）。G-05 新增。
     *
     * **写一次几何，量若干次**：排版交出的宽是"内容宽"（≤ 可用宽），而它写回去
     * 就成了新的换行基准 ⇒ 内部量到"在这个宽下再量还是这个宽"为止才写
     * （实测 1~2 趟，上限 4 趟只防病态输入）。一次就写的话，高度是按**旧**宽度
     * 排出来的，"事后问 `MeasureLayout()` 又是另一个数"，窄文本上差一整行。
     *
     * **代价照实写：`width` 也会被收到内容宽**。配置里的 `width` 在这里只当
     * 第一趟的换行基准——一个 `width: 200` 的标签调完可能变成 130 宽。要保住
     * 200 的那个框，就别调它，改成读 `MeasureLayout().height` 自己
     * `Resize(200, h)`（那才是"只撑高"）。
     *
     * @return 几何真的变了才 true。三种 false：
     *   1. **被布局管着**（父链上有有效 `InkingLayoutSpec`）——不写几何，记一条 WARN
     *      （关键决定二：写了下一帧也会被 `ApplyLayoutRect` 覆盖）；
     *   2. 量出来的尺寸与现在一模一样（幂等，`Resize` 返回 false）；
     *   3. 空文本（宽度不缩到 0，真空的标签保持原样）。
     */
    bool FitToContent();

    /**
     * 我是不是**被布局管着**（父链上有有效 `InkingLayoutSpec` 的祖先，不含自己）。
     *
     * `FitToContent()` 的失败原因就是这个——做成公开观测口而不是让它藏在
     * WARN 里，自检才能断言"是这条规矩拦的"，而不是"碰巧没改"。
     * 判据与 `InkingWidget::RelayoutParent()` 逐字一致（那里也是往上游找第一个
     * 有效 `InkingLayoutSpec`）。
     */
    bool IsManagedByLayout() const noexcept;

    /// 标签认**外部显示变换**（A13）：文字可以整体被挪走 / 缩放（绘制跟着走）。
    bool AcceptsExternalTransform() const noexcept override { return true; }

    /// 命中包络：整块矩形 **× 外部变换**（动态档拿它算洞；与静态版同义）。
    InkingShapeBounds GetHitEnvelope() const override;

protected:
    /// 画排好的每一行（实现在 `InkingLabelLike::paintLabelText`）。
    void onRender(float pixelX, float pixelY) const override;

    /**
     * 文字这一笔的**水平滚动量**（设计坐标，0 = 不滚），与静态版同名同义。
     *
     * 动态标签自己不用它（标签没有光标），留着有两个用处：G-05 的公开面要与
     * 静态档一致（"静态版有的这里一个不少"），而 G-06 抽 `paintLabelText` 时它
     * 正好是那个**参数**的来源——于是将来真有动态输入框，只要覆盖它。
     * 语义见 `InkingStaticLabel::TextScrollX`：只影响绘制，排版与命中用未滚动的坐标。
     */
    virtual float TextScrollX() const noexcept { return 0.0f; }

private:
    /// 构造期那一次自测量：只补"没配置"的那一维，且只在**不被布局管着**时做。
    void FitOnConstruction() noexcept;

    // ---------------- G-06：保留既有私有符号（唯一实现在 `InkingLabelLike`） ----------------
    //
    // G-05 时这三个是本类的私有成员（从静态版抄来的），`nm -g` 能看到符号。
    // 搬进 `InkingLabelLike` 之后各留一行转发，符号一个不少（与 G-02 / G-04 同一条纪律）。
    const InkingTextLayoutResult& LayoutForWidth(float available,
                                           float availableHeight) const;
    bool CacheMatches(float available, float availableHeight) const noexcept;
    std::uint32_t EffectiveTextColor() const noexcept;
};

