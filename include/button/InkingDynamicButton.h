#pragma once

// 这里是 InkingDynamicButton —— 静态 Button 的**动态版**，也是第二个完整组件。
//
// ---------------------------------------------------------------------------
// 判据只有一条：几何会不会在构造之后自己变（InkingAnchor.h 末尾那段）
//
//   静态（InkingStaticButton）：几何构造即定型，没有几何写入口 → 进命中表。
//   动态（本类）：几何随时会变 → **不进表**，自己维护命中状态，每帧收一次 Tick。
//
// 所以本类比静态版多的只有几何写入口（Resize / ChangeSelfAnchor /
// ChangeTraceAnchor / ChangeOffset，继承自 InkingDynamicWidget），少的只有
// "能被命中表回答"。三态外观与状态机是两边**共用**的一份（ButtonLook.h），
// 渲染那一笔也是（src/button/ButtonPaint.cpp）——动态按钮不是另一个组件，
// 是同一个组件的另一档。
//
// ---------------------------------------------------------------------------
// 输入与静态 Button 不同：静态是"被喂"，动态是"自己拉"
//
//   静态：`InkingScene::DispatchPointer` 线性扫绘制列表，从最上面往下问
//         `HitTest`，然后**推**给按钮（MouseHover / MousePress / MouseRelease）。
//         z 序、遮挡、抓取（按下之后指针滑出去还算它按着）全在场景那边。
//
//   动态：`DispatchPointer` 明确跳过动态节点（动态组件不进表、也不被遍历，
//         docs/InputDesign.md §2 / §7），所以**没人会喂它**。它每帧在自己的
//         `Tick` 里**读**场景的指针状态（GetPointerX/Y / IsPointerDown /
//         IsPointerInside）、自己问自己的形状、自己推三态、抬起时自己触发点击。
//
// 「自己拉」不是实现偷懒，是这一档的前提：几何每帧都在变，谁都没法替它维护
// 一份提前算好的命中状态（docs/InputDesign.md §7 第 1 条）。代价也要说清楚：
//
//   - **自判等于"自管自"**，不做遮挡仲裁——动态组件压在谁身上由它自己说了算。
//     docs/InputDesign.md §15 第 1 条（"动态组件是否需要处理遮挡"）本来是开放
//     问题，本轮选了自管自；将来静态表用"动态组件的洞"（§10.5 第 3 条）把这块
//     区域挖出来时，本类的输入方式不用改，改的是场景那边的表。
//     一个顺带的好处：现在那条线性扫的临时实现里，动态节点**仍然参与**"谁在最
//     上面"的判断，所以它压住的那块静态组件不会被悬停 / 点击——遮挡这件事在
//     派发那一侧已经是对的，只是"事件由谁接手"由动态组件自己决定。
//   - **采样率 = 逻辑步**：Tick 走的是固定逻辑步（默认 50Hz），而指针状态是每
//     渲染帧更新的，所以"按下与抬起落在同一个逻辑步里"（短于 20ms 的点击）
//     看不出来。人手的点击是 80ms 起步，够用；要更稳得等输入机制那一轮把
//     帧计数落下来（docs/InputDesign.md §5），那时这里也不必改。
//
// ---------------------------------------------------------------------------
// 三态 / 过渡 / 点击事件**不在这个类里**：它们住在 `InkingButtonLike.h`
// ---------------------------------------------------------------------------
// 和静态版同一条规矩：本类继承那个**契约基类**，于是 `clicked` / `GetState` /
// `GetAppearance` / `GetDisplayColor` / `GetDisplayTransform` / `HasOnClicked` /
// `SetOnClicked` / `TriggerClick` 全部**继承得来**，本类一个都不用转发。
// 本类与静态版唯一的差别是"谁来喂那两个标志"——静态版由场景派发推过来，
// 本类在 `JudgePointer()` 里自己从场景拉，再调继承来的 `MouseHover` / `MousePress`。
//
// 用法：
//     inking::InkingDynamicButton button(parent, data);     // 手写数据
//     inking::InkingDynamicButton button(parent, "name");   // 按名字从 ButtonLibrary 取
//     button.SetOnClicked([] { ... });
// 配置驱动那条路走生成器：json 里写 `"dynamic": true`，生成出来的类就继承本类
// （CXXCSS.md §4）。生成类的用法与静态版一样：`inking::cxxcss::DynamicTestButton b{parent};`

#include <button/InkingButtonLike.h>
#include <widget/InkingDynamicWidget.h>

#include <cstdint>
#include <string>

namespace inking {

class InkingDynamicButton : public InkingDynamicWidget,
                            public InkingButtonLike {
public:
    /**
     * 手写数据构造：不走配置，直接把一份 ButtonData 交给它。
     *
     * @param parent 上级节点；非拥有。按钮必须在某个场景的子树下才能被渲染，
     *               而且**必须**有场景：它的输入只有"读场景的指针状态"这一条来源。
     * @param data   配置数据（构造时会 Normalize 一次，补齐省略的三态）。
     */
    InkingDynamicButton(InkingWidget* parent, const ButtonData& data);

    /**
     * 配置驱动构造：只给 parent + 名字，配置从 `ButtonLibrary` 里按名字取。
     *
     * **它不是生成的按钮类走的路**：生成器为每个名字生成一个独立的类
     * （json 里 `"dynamic": true` 的那些继承本类），数据直接烘在类里。
     * 这条构造留着给"名字要到运行期才知道"的少数场景用。
     *
     * 名字没登记时：写一条**错误**日志，给一个空按钮，并让 `UnknownNameCount()`
     * 加一（可断言，不静默）——和静态版同一套规矩。
     */
    InkingDynamicButton(InkingWidget* parent, const std::string& name);

    /// 按名字从库里取配置。取不到返回 nullptr。
    static const ButtonData* FindData(const std::string& name);

    /// 发生过多少次"按名字构造却查不到配置"（静态 / 动态各记各的）。
    static int UnknownNameCount() noexcept;

    /**
     * 指针在这个位置上吗。世界坐标（设计空间）。
     *
     * 重写基类的 `[√]` 钩子：世界坐标 → 本地坐标，再问形状层（`ShapeContains`），
     * **和渲染用的是同一份形状定义**。隐藏的按钮不参与命中。
     *
     * 注意它只回答"我在不在这个点下面"，不回答"是不是我最上面"——动态组件
     * 自管自，z 序不由本函数负责（docs/InputDesign.md §15 第 1 条）。
     */
    bool HitTest(float worldX, float worldY) const override;

    // ---------------- 属性（运行期可改） ----------------

    /// 形状定义：渲染 / 命中 / 包围盒三者都从它派生（InkingShapeSpec.h）。
    const ShapeSpec& GetShape() const noexcept;

    /**
     * 保守包络：动态按钮不进命中表，所以它只被用来登记**动态洞**
     * （"这块地方被动态组件占了，静态层别急着下结论"）。
     * 同样取三态变换的并集——悬停旋转/放大时，洞也得跟着盖住。
     */
    ShapeBounds GetHitEnvelope() const override;

    /// 当前显示尺寸（T-5）：状态里写了尺寸就按过渡进度插值，否则就是配置那份。
    /// 动态档本来就有几何写入口，这里只是让"三态尺寸"这条**只读**的路也生效。
    int GetWidth() const noexcept override;
    int GetHeight() const noexcept override;
    bool HasLayoutAnimation() const noexcept override;

    /// 同上：变换作用于整棵子树（T-7）。
    TransformSpec GetSubtreeTransform() const noexcept override;

    /// 构造时用的那份配置（已经 Normalize 过）。
    const ButtonData& GetData() const noexcept;

    /// 按钮上的文字。
    const std::string& GetText() const noexcept;

    /// 设置文字。标重绘脏（画面变了），不标命中脏（文字不参与命中）。
    void SetText(const std::string& text);

    /// 设置文字颜色（0xAARRGGBB）。语义同静态版：只标重绘脏。
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

    // ---------------- 输入：自己拉（和静态按钮那三个推的入口对应） ----------------

    /**
     * 自己判一次指针：悬停进出、按下、抬起触发点击。
     *
     * **这就是"输入与静态 Button 不同"的落点**。静态版的对接口子是
     * `MouseHover` / `MousePress` / `MouseRelease`——由场景派发推过来；
     * 本类没有那三个推的入口（它们继承来了，但**没人会调**），只有这一个
     * "拉"的入口：它从所属场景读指针状态，自己判命中、自己推三态
     * （调继承来的 `MouseHover` / `MousePress`）。`onTick` 里每步调一次，
     * 自检也可以手动调。
     *
     * 幂等：同一份指针状态连着调两次，第二次什么都不做。
     *
     * @return 三态真的变了才返回 true。
     */
    bool JudgePointer() noexcept;

    /// 上一次判定里"指针在按钮上"吗。自检观测口。
    bool IsPointerOver() const noexcept;

    /// 上一次判定里"这次按下算在按钮上"吗（按下之后滑出去仍为 true）。
    bool IsPressCaptured() const noexcept;

protected:
    /// **每帧一次的逻辑钩子**：输入在这里发生。
    ///
    /// 动态组件的 Tick 由 `InkingScene::TickLogic` 按固定逻辑步派发
    /// （静态组件压根没有这个入口）。本类只做一件事：`JudgePointer()`。
    void onTick(float deltaSeconds) override;

    /**
     * 每**渲染帧**一次：推进三态之间的颜色过渡（和静态版同一套）。
     *
     * 注意它和上面的 `onTick` 是**两条通道**：`onTick` 是固定逻辑步（输入、
     * 几何推进），`onAnimationTick` 是渲染帧（真实 delta，颜色过渡）。
     * 动态按钮两条都收得到；静态按钮只收后一条。
     */
    void onAnimationTick(float deltaSeconds) override;

    /// 按状态选外观、按形状填色，再画上文字——和静态版同一份实现
    /// （src/button/ButtonPaint.cpp）。
    void onRender(float pixelX, float pixelY) const override;

    /// 三态真的变了（继承自 `InkingButtonLike` 的 `[√]` 钩子）：把**当前显示**
    /// 的颜色同步到基类那份 color（和静态版同一件事）。
    void onButtonStateChanged(ButtonState from, ButtonState to) override;

    /// 过渡推进了一帧、**显示值**变了。标脏在这里发生（契约基类自己不标脏）。
    void onButtonDisplayAdvanced(bool repaintDirty, bool hitDirty) override;

private:
    /// 把 data 里的字段搬到基类与自己的成员上；两个构造函数共用。
    void applyData(const ButtonData& data);

    /// 把 `ButtonLook` 里**当前显示**的颜色同步到基类那份 color。
    void syncBaseColor() noexcept;

    /// 构造完成后的收尾：尺寸合法性提醒。
    void finishConstruction();

    ButtonData _data{};

    /// 上一次判定时"左键按着没"。按下 / 抬起是**边沿**，动态组件自己记：
    /// 静态版这份历史在场景那边（`_pointerWasDown`）。
    bool _wasDown = false;

    /// 这次按下是**在按钮上**按下去的吗。按下之后指针滑出去仍然算它按着
    /// （"抓取"语义，和静态版的 `_pressedNode` 对应），所以这份状态也得自己存。
    bool _pressedOnButton = false;

    /// 上一次判定里"指针在按钮上"（自检观测口，也用来判抬起是否算点击）。
    bool _pointerOver = false;
};

}  // namespace inking
