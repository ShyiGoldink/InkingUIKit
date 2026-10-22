#pragma once

// 这里是 InkingStaticButton —— 项目里第一个**完整组件**，也是"组件该怎么写"
// 这套框架的样板。
//
// 作为静态 Button，全部可配置内容写在 CXXCSS/Button/<名字>.json 里
// （模板见 CXXCSS/Button/button.example.json）：名称、设计尺寸、形状层、
// 自身锚点与上级锚点、三态外观、文字。
// 代码生成器会把**每个独立名字**的 Button 展开成一个独立的新类并替换掉泛型
// 实现，所以名字必须与配置文件、代码里的用法三处一致。生成器落地之前，
// 手写这条路走 `ButtonData`（见下"两种构造"）。
//
// 用法：
//     inking::InkingStaticButton button(parent, data);      // 手写数据
//     inking::InkingStaticButton button(parent, "name");    // 按名字从 ButtonLibrary 取
//     button.SetOnClicked([] { ... });
//
// ---------------------------------------------------------------------------
// 为什么是**静态**组件（这个选择不能回头改，所以把理由写在这里）
//
// 判据只有一条：几何会不会在构造之后自己变（InkingAnchor.h 末尾那段）。
//
//   - 常 / 悬 / 按三态**只换颜色**，而颜色不标脏（docs/API.md「标脏」）；
//   - 按下如果要做"下沉"效果，那是**绘制偏移**（onRender 里把 y 挪几像素），
//     不是几何变化——命中区域不动，所以也不需要动态组件；
//   - 静态组件没有几何写入口，拿到 `InkingAnchor*` 也改不了它的几何。
//
// 结论：三态外观 + 点击回调全部落在静态档，进命中表那条路就一直是通的。
// 真要一个"按下会改变尺寸、会撑开布局"的按钮，那是另一个类型——它已经存在了：
// `InkingDynamicButton`（include/button/InkingDynamicButton.h），几何可以运行期改，
// 代价是不进命中表、指针由它自己在 Tick 里拉。**不要在这里加几何写入口**
// 把静态按钮变回动态：`src/inking.cpp` 里有编译期断言钉着这条。
//
// ---------------------------------------------------------------------------
// 三态 / 过渡 / 点击事件**不在这个类里**：它们住在 `InkingButtonLike.h`
// ---------------------------------------------------------------------------
// 原先这三样长在本类的公开面上，于是"别的组件也想有三态 + 点击"只有一条路：
// 继承这个**具体类**——连带把形状、文字、命中、登记全带上（`InkingStaticImage`
// 当年就是各抄了一份）。现在它们是**契约基类**：
//
//     InkingStaticButton : public InkingStaticWidget, public InkingButtonLike
//
// 于是 `clicked` / `GetState` / `GetAppearance` / `GetDisplayColor` /
// `GetDisplayTransform` / `MouseHover` / `MousePress` / `MouseRelease` /
// `TriggerClick` / `SetOnClicked` 全部**继承得来**，本类一个都不用转发。
//
// 两条路的输入方式不同（静态是场景派发喂、动态是自己拉），但"哪个状态配哪份
// 外观""按下 > 悬停 > 通常"只允许有一份定义，所以规则本体在 `ButtonLook`
// ——它跟契约一起被 `InkingButtonLike` 持有，静态 / 动态共用同一份。
// 本类只负责"什么时候把两个标志填进去"——那是场景派发的事。
//
// ---------------------------------------------------------------------------
// 状态机是**契约**的事，命中与渲染才是本类的事
//
// 三态是**渲染状态**（只换颜色，不碰几何）；命中判定走形状层同一份定义
// （`ShapeContains`，见 GetShape），所以"画的是圆角、点到的是直角"不会发生。
// 本类在 `onPointer*` 三个钩子里把场景推过来的判定结果**转发一行**给
// `MouseHover` / `MousePress` / `TriggerClick`——仅此而已，三态怎么变、过渡
// 怎么跑、点击怎么派发全在基类。
//
// 转发**不标脏是有意的**：三态切换的画面变化由 `ButtonLook` 记一笔待报告，
// 下一帧 `onAnimationTick` 里报出来（见 `ButtonLook::BeginTransition`）。
//
// 动态按钮（`InkingDynamicButton`）**没有**这三个推的入口：它的输入是自己从场景
// 拉的（`JudgePointer`）。所以"输入与静态版不同"那句话，落到接口上就是
// "一边有这三个推的入口、另一边只有一个拉的入口"。

#include <button/InkingButtonLike.h>
#include <widget/InkingStaticWidget.h>

#include <cstdint>
#include <functional>
#include <string>

namespace inking {

/// 点击回调（`ButtonClickCallback`）与三态（`ButtonState`）都在 ButtonLook.h 里：
/// 静态 / 动态两块按钮共用同一份定义，不能各写一套。本类经 `InkingButtonLike`
/// 拿到它们，不再直接包含。

class InkingStaticButton : public InkingStaticWidget,
                           public InkingButtonLike {
public:
    /**
     * 手写数据构造：不走配置，直接把一份 ButtonData 交给它。
     *
     * @param parent 上级节点；非拥有。按钮必须在某个场景的子树下才能被渲染。
     * @param data   配置数据（构造时会 Normalize 一次，补齐省略的三态）。
     */
    InkingStaticButton(InkingWidget* parent, const ButtonData& data);

    /**
     * 语义激活（D7）：等价于"用户点了一下"，不经过指针，也不改三态。
     *
     * 自检 / 快捷键 / 脚本用它代替 `dynamic_cast<InkingStaticButton*>` +
     * `TriggerClick()`——调用方从此不认识具体控件类型。
     * @return 有订阅者、真的派发了才返回 true。
     */
    bool Activate() override { return TriggerClick(); }

    /**
     * 配置驱动构造：只给 parent + 名字，配置从 `ButtonLibrary` 里按名字取。
     *
     * **它不是生成的按钮类走的路**：生成器为每个名字生成一个独立的类，
     * 数据直接烘在类里（`inking::cxxcss::NormalButton` 之类），构造时不查任何表。
     * 这条构造留着给"名字要到运行期才知道"的少数场景用。
     *
     * 名字没登记时：写一条**错误**日志，给一个空按钮（不画东西、不响应点击），
     * 并让 `UnknownNameCount()` 加一。以前这里是"静默退回一份默认外观"——
     * 按钮长得不对却没有任何报错，只能靠肉眼发现；现在它是错的、也是可断言的。
     *
     * 注意构造完之后**再去登记是不会生效的**：数据在构造时就拷进来了。
     * 登记必须在所有按钮构造之前完成（生成代码里的启动期登记天然满足）。
     */
    InkingStaticButton(InkingWidget* parent, const std::string& name);

    /**
     * 按名字从库里取配置。取不到返回 nullptr。
     *
     * 给"我想先看看配置里写了什么"或者"我要自己判断该不该建"这类场景用；
     * 按名字构造内部也走它，所以两者看到的永远是同一份数据。
     */
    static const ButtonData* FindData(const std::string& name);

    /**
     * 发生过多少次"按名字构造却查不到配置"。
     *
     * 每次发生都会写一条错误日志并给出一个空按钮。留这个计数是为了让这件事
     * **可断言**——静默失败才有藏身处，能被断言的失败没有。
     */
    static int UnknownNameCount() noexcept;

    /**
     * 指针在这个位置上吗。坐标是**世界坐标（设计空间）**。
     *
     * 重写的是基类的 `[√]` 钩子：世界坐标 → 本地坐标，再问形状层
     * （`ShapeContains`）——**和渲染用的是同一份形状定义**，所以不会出现
     * "画的是圆角、点到的是直角"。隐藏的按钮不参与命中（父链上任何一层
     * 不可见都算隐藏）。
     *
     * z 序不在这里判：那是调用方的事（`InkingScene::DispatchPointer`
     * 从绘制列表尾部往前问）。
     */
    bool HitTest(float worldX, float worldY) const override;

    // ---------------- 属性（运行期可改） ----------------

    /// 形状定义：渲染 / 命中 / 包围盒三者都从它派生（见 InkingShapeSpec.h）。
    const ShapeSpec& GetShape() const noexcept;

    /**
     * 命中索引用的**保守包络**：三态变换的并集（`docs/InputDesign.md` §11）。
     *
     * 命中表的格子登记是**冻结**的，而按钮的变换会随三态变（悬停平移+放大、
     * 按下下沉）——所以登记时必须盖住"所有可能的样子"，精判才用当前变换。
     * 只按当前变换登记会漏命中：悬停那一帧按钮挪出去一点，边缘的点就落在表外了。
     */
    ShapeBounds GetHitEnvelope() const override;

    /**
     * 当前**画出来 / 判命中**的尺寸（T-5 布局通道）。
     *
     * 状态里没写尺寸时就是配置里那份；写了就按同一个过渡进度插值——
     * 绘制与命中都问这里，所以"画多大就点多大"。
     *
     * 重写的是 `InkingAnchor` 的虚函数（`GetWidth` / `GetHeight` 是锚点层的
     * 尺寸入口），值取自 `InkingButtonLike::GetDisplayWidth/Height`——
     * 所以**不能**把基类那两个观测口命名成 `GetWidth`，会撞名。
     */
    int GetWidth() const noexcept override;
    int GetHeight() const noexcept override;

    /// 三个状态里有尺寸差异吗（静态档也能有"布局通道"，见 `ButtonData.h`）。
    bool HasLayoutAnimation() const noexcept override;

    /// T-7：静止 / 动态按钮的变换**同时作用于子节点**（抽屉 / Toast 那一档）。
    TransformSpec GetSubtreeTransform() const noexcept override;

    /// 构造时用的那份配置（已经 Normalize 过）。
    const ButtonData& GetData() const noexcept;

    /// 按钮上的文字。当前只认 `text.content.normal`（生成器会把它填进 label）。
    const std::string& GetText() const noexcept;

    /**
     * 设置文字。
     *
     * 与"不要在按钮里配文字"那条建议不冲突：**文字是运行期数据**，
     * 多语言就该在这里换（配置里的文字会被烘进纹理，换语言就废了）。
     *
     * 标**重绘脏**（画面变了——文字真的画出来了，见 `include/render/InkingText.h`），
     * 但**不**标命中脏：文字不参与命中，"点到哪儿"没变。
     */
    void SetText(const std::string& text);

    /**
     * 设置文字颜色（0xAARRGGBB）。
     *
     * 配置里没有这个字段是刻意的：文字色属于**主题 / 运行期**（换肤、多语言、
     * 高对比模式），烘进 json 就又成了"换一次要重新构建"。同样只标重绘脏。
     * 纹理层把颜色做成**调制**，所以换色不会重烘纹理。
     */
    void SetTextColor(std::uint32_t color);

    // ---------------- 继承来的（`InkingButtonLike`） ----------------
    //
    // 这里**故意一个都不重写**，也不写"转发"：
    //   clicked / HasOnClicked / SetOnClicked / TriggerClick
    //   GetState / GetAppearance / GetDisplayColor / GetDisplayTransform
    //   GetDisplayWidth / GetDisplayHeight / IsTransitioning
    //   MouseHover / MousePress / MouseRelease / AdvanceButtonLook
    //
    // 它们都是 `InkingButtonLike` 的公开面，继承即得。想找定义去
    // `include/button/InkingButtonLike.h`。

protected:
    /**
     * 尺寸被外部改了（布局 / 自测量）⇒ 让 `ButtonLook` 跟上（0.4.0 A4/B1）。
     *
     * 少了这一步，"布局把宽度写成 300、`GetWidth()` 还是 100"——`fillMain` /
     * `Stretch` 因此全都不收敛，而且**不报错**。
     */
    void onSizeChanged() override {
        // 调用方是基类的尺寸写入口（`ApplyLayoutRect` / `ApplySelfSize`），
        // 此时 `_width` / `_height` 已经是新值。
        SyncButtonLookSize(_width, _height);
    }

    /// 按状态选外观、按形状填色，再画上文字。
    ///
    /// 重写的是**钩子**（[√] onRender），不是写入口：基类给"一块实心矩形"
    /// 的默认实现，这里只换成"按形状的一笔"。
    void onRender(float pixelX, float pixelY) const override;

    /**
     * 每**渲染帧**一次：推进三态之间的颜色过渡。
     *
     * 静态按钮收不到 `InkingDynamicWidget::Tick`（那是动态档的逻辑步），
     * 所以颜色过渡走这条独立通道（`InkingWidget::TickAnimation` →
     * `InkingScene::TickFrame` 分发）。**它只改颜色，不碰几何**——这正是
     * 静态按钮能一边有动画、一边继续待在"进命中表"那一档的原因。
     */
    void onAnimationTick(float deltaSeconds) override;

    // ---------------- [√] 通用指针钩子（A1） ----------------
    //
    // 场景**不再**把节点 `static_cast` 成 `InkingStaticButton*`，而是走基类的
    // `PushPointerHover/Press/Click` → 这里三个钩子。按钮要做的只是把它们转发
    // 给自己那三个幂等写入口——三态、过渡、点击回调全都在原来的地方，一个字没变。
    //
    // 转发**不标脏**是有意的：三态切换的画面变化由 `ButtonLook` 记一笔待报告，
    // 下一帧 `onAnimationTick` 里报出去（见 `ButtonLook::BeginTransition`）。
    // 在这里额外标一次会把它标重，而且"没配过渡"那条路依旧只走一个出口。

    /// 指针进出（由基类的 `PushPointerHover` 转来）。
    void onPointerHover(bool hover) override;

    /// 按下 / 抬起（由基类的 `PushPointerPress` 转来）。
    void onPointerPress(bool press) override;

    /// 一次完整点击（由基类的 `PushPointerClick` 转来）。
    void onPointerClick() override;

    /// 三态真的变了（由 `InkingButtonLike::MouseHover` / `MousePress` 同步调来）。
    /// 本类挂的副作用只有一个：把**当前显示**的颜色同步到基类那份 color。
    void onButtonStateChanged(ButtonState from, ButtonState to) override;

    /// 过渡推进了一帧、**显示值**变了。标脏在这里发生（基类自己不标脏）。
    void onButtonDisplayAdvanced(bool repaintDirty, bool hitDirty) override;

private:
    /// 把 data 里的字段搬到基类与自己的成员上；两个构造函数共用。
    void applyData(const ButtonData& data);

    /// 把 ButtonLook 里**当前显示**的颜色同步到基类那份 color：
    /// 从基类窗口看颜色时读到的得是同一件事（本类重写了 onRender，用不到它，
    /// 但一致性不能破）。过渡进行中同步的是插值出来的中间色。
    void syncBaseColor() noexcept;

    /**
     * 画文字。
     *
     * 真的走**纹理层**了：字形图集 + `render/InkingText.h` 的 `PaintText`，
     * 实现在 `src/button/ButtonPaint.cpp`，静态 / 动态按钮共用同一份。
     * 拿不到字体时纹理层退化成占位方块——所以它**永远画得出东西**，
     * 不会有"字没了但也没报错"这种死角。
     */
    void renderText(float pixelX, float pixelY) const;

    /// 构造完成后的公共收尾：Normalize、按需打日志。
    void finishConstruction(bool foundInLibrary);

    /// 构造时用的那份配置（已经 Normalize 过）。三态与基准尺寸都展开了
    /// 一份进 `InkingButtonLike` 的状态机，但它仍然要留着：形状、描边阴影、
    /// 文字、布局字段都从它取。
    ButtonData _data{};
};

}  // namespace inking
