#pragma once

// 勾选三件套（`docs/zh/plans/2026-11-03-uikit-completeness-tasks.md` T-04）：
// `Checkbox` / `Radio` / `Switch`，共用一个基类 `ToggleLike` + 一个显式的 `RadioGroup`。
//
// ---------------------------------------------------------------------------
// 为什么三个控件放一个头
//
// 它们共用同一套东西：选中态翻转、`clicked` / `cancelled` 两个槽、`SetChecked`
// 的幂等规则、`GetParts()` 的两块矩形。拆成三个头 + 三份实现的结果是"同一条规则
// 写三遍"（`AGENTS.md` §6 第 94 条），而差别只有"mark 那一块长什么样、怎么摆"。
// 所以：**基类定契约，三个子类只覆盖"形"与"触发时机"。**
//
// ---------------------------------------------------------------------------
// 事件只有一对槽（§2.5.1），没有 `SetOnClicked`
//
//   `clicked`   ← 变成选中 / 开
//   `cancelled` ← 变成未选中 / 关
//
// 三个控件的差异只在**谁触发**：Checkbox / Switch 是用户点它；**Radio 是"同组
// 另一个被选中"**（不是用户点它）。所以 Radio 不需要第三个槽，也不需要调用方自己
// 去遍历兄弟——那是 `RadioGroup` 的事。
//
// ---------------------------------------------------------------------------
// 四条要记住的语义
//
//   1. **`SetChecked` 是程序入口，也会发事件**（不是"静默改值"）。订阅者因此可以
//      只盯槽、不必区分"用户点的"还是"代码设的"；
//   2. **`RadioGroup::Select` 一次发 1 + N 条**：目标发 `clicked`，组内其它被清掉的
//      各发一条 `cancelled`。这是**设计**（"一组变化各发各的"），不是副作用：
//      业务上一个 bool 表达不了"从谁换到谁"；
//   3. **点已选中的 Radio 不翻转、不发事件**（单选不可反选）——`AllowsCheckedChange()`
//      唯一一处返回假的场合；
//   4. **`SetChecked` 只标重绘脏**：几何（组件框）没变，所以命中表不重烘、父容器不重排；
//   5. **mark 只在选中时画**（Checkbox 的勾、Radio 的圆点）——"状态"就在这一笔的有无上，
//      所以 `onRender` 必须读 `_checked`。`Switch` 是唯一例外：滑块关着也在
//      （由 `PaintsMarkWhenUnchecked()` 表达），位置本身就是状态。
//
// 第一版**不做**（与 Slider / ProgressBar 同一条边界）：`SetPainter` 第三档
// （`InkingCanvasContext` 构造是 private）、三态 Checkbox（半勾）、键盘空格切换、
// 手柄。

#include <widget/InkingStaticWidget.h>
#include <event/InkingEvent.h>
#include <button/ButtonData.h>
#include <dataStruct/InkingAnchorData.h>
#include <dataStruct/InkingDecoration.h>
#include <dataStruct/InkingShapeSpec.h>

#include <cstddef>
#include <string>
#include <vector>

// 只前向声明：绘制钩子只按指针传渲染器（同 `button/ButtonPaint.h` 的做法）。
struct SDL_Renderer;

namespace inking_prefab {

class RadioGroup;

/**
 * @brief 三件套共用的数据（构造时给一次）。
 *
 * 定位 / 尺寸 / 层级复用 `InkingAnchorData`（与 `SliderData` 同一口径）。
 * 两块外观都是 `InkingButtonAppearance`（§2.5.2），所以"换主题色 / 贴图 / 换矢量"
 * 不用改库。
 *
 * `base` / `mark` 的语义按控件不同（§2.5.5）：
 *
 * | 控件 | base | mark |
 * | - | - | - |
 * | Checkbox | 方框底 | 勾（不指定形状时画内置勾） |
 * | Radio | 圆底 | 圆点 |
 * | Switch | 轨道 | 滑块 |
 */
struct ToggleData : public InkingAnchorData {
    /// 默认 18×18（`MakeSwitchData()` 会改成 40×22）。
    ToggleData() noexcept;

    /// 拼进子节点名 / 日志用（`Checkbox` 再补一层类型前缀）。
    std::string name = "toggle";

    /// 初始是否选中。
    bool checked = false;

    InkingButtonAppearance base{};  ///< 框底 / 圆底 / 轨道
    InkingButtonAppearance mark{};  ///< 勾 / 圆点 / 滑块
    /// 外框描边（亮色场景里白底压在浅底上要靠它站住）。宽度 0 = 不画。
    InkingStrokeSpec baseStroke{};

    InkingShapeSpec baseShape = InkingShapeSpec::RoundedRect(4.0f);
    /**
     * mark 的形状。
     *
     * **默认 `Rect()` 是一个哨兵**：表示"用这个控件内置的 mark"（Checkbox 画勾、
     * Radio 画圆点、Switch 画圆滑块）。给它**别的**形状就按那个形状画——于是
     * "换勾的样式" = 换一个形状（`Circle` / `PolygonPath` / 自定义 `svg`），
     * 不用等库支持。
     */
    InkingShapeSpec markShape = InkingShapeSpec::Rect();

    /// mark 相对 base 的**内缩比例**（0~0.49，Checkbox / Radio 用）。
    /// 0.18 = 四周各留 18%，于是 mark 恒落在 base 之内。
    float markInset = 0.18f;

    /// Switch 的滑块与轨道边之间的间距（设计像素）。
    float knobInset = 2.0f;
};

/// 两块矩形（相对组件左上角，设计坐标）。**绘制与命中读同一份**（§2.5.3）。
struct ToggleParts {
    InkingShapeBounds base;  ///< 整块（命中认它，点框内任何位置都算点中）
    InkingShapeBounds mark;  ///< 勾 / 圆点 / 滑块，恒落在 base 之内
};

/// 18×18、白底 + 浅描边、主色勾。
ToggleData MakeCheckboxData();
/// 18×18、白底 + 浅描边、主色圆点。
ToggleData MakeRadioData();
/// 40×22、沉一档的轨道 + 白色滑块。
ToggleData MakeSwitchData();

/**
 * @brief 三件套的基类：选中态 + 一对事件槽 + 两块部件。
 *
 * 子类只覆盖两件事：`layoutMark`（mark 摆在哪）与 `paintMark`（mark 长什么样），
 * 外加 `AllowsCheckedChange`（Radio 用它表达"已选中就不能再选"）。
 */
class ToggleLike : public InkingStaticWidget {
public:
    /// 变成选中 / 开。**程序调 `SetChecked(true)` 也会发**。
    InkingEvent<> clicked;
    /// 变成未选中 / 关。Radio 的这条通常由**同组另一个被选中**触发。
    InkingEvent<> cancelled;

    bool IsChecked() const noexcept { return _checked; }

    /**
     * @brief 设选中态：相等返回 false（一个脏都不标、一条事件都不发）。
     *
     * 顺序不能反：**改字段 → 标脏 → 子类钩子 → 发事件**（`InkingEvent.h` 第 2 条
     * 契约：订阅者在回调里读到的必须是新值）。
     */
    bool SetChecked(bool checked);

    /// 两块矩形（现算，组件被布局拉伸后不会留过期几何）。
    const ToggleParts& GetParts() const noexcept;

    const ToggleData& GetData() const noexcept { return _data; }

    /// 命中：**整块 base**（点勾上、点框角都算点它），见 `ToggleParts::base`。
    bool HitTest(float worldX, float worldY) const override;

    /// 指针在勾选件上 = 手型（T-09）。基类声明一次，三件套一起继承。
    InkingCursorShape GetCursorShape() const noexcept override {
        return InkingCursorShape::Hand;
    }

    /// 没有三态变换，也不认 A13 外部变换。
    InkingTransformSpec GetDisplayTransform() const noexcept { return {}; }

protected:
    ToggleLike(InkingWidget* parent, const ToggleData& data);

    void onRender(float pixelX, float pixelY) const override;

    /// 用户点了一下（主键按下 + 抬起都在它身上）。默认 `SetChecked(!IsChecked())`。
    void onPointerClick(InkingPointerButton button, float designX,
                        float designY) override;

    /// 这一下能不能改选中态。默认能；**Radio 覆写成"已选中就不行"**（单选不可反选）。
    virtual bool AllowsCheckedChange() const noexcept { return true; }

    /// 选中态真的变了之后调（已标脏、尚未发事件）。Switch 在这里起动画。
    virtual void onCheckedChanged(bool checked);

    /// mark 摆在 base 的哪里（默认：按 `markInset` 居中内缩）。
    virtual void layoutMark(const InkingShapeBounds& base, ToggleParts& parts) const;

    /// 画 mark（默认：按 `markShape` 填一笔）。Checkbox 覆写成内置勾。
    virtual void paintMark(SDL_Renderer* renderer, const ToggleParts& parts,
                           float magnification, float pixelX, float pixelY) const;

    /**
     * @brief 未选中时**画不画 mark**。默认不画（Checkbox / Radio 的"状态"就是
     *        mark 在不在）；`Switch` 覆写成画——它的滑块关着也得看得见。
     *
     * 单拎成一个钩子而不是让子类各自覆盖 `onRender`：`onRender` 里还有底 + 描边
     * 两笔，覆盖它等于把这两笔抄三遍（`AGENTS.md` §6 第 94 条）。
     */
    virtual bool PaintsMarkWhenUnchecked() const noexcept { return false; }

    /// 把 `_parts` 按当前组件框与选中态重算一遍。
    void layoutParts() const noexcept;

    ToggleData _data{};
    bool _checked = false;

    /// 现算的部件（`mutable`：`GetParts()` 与 `onRender` 都是 const）。
    mutable ToggleParts _parts{};
};

/**
 * @brief 复选框：点一下在"选中 / 未选中"之间翻。
 *
 * ```cpp
 * inking_prefab::Checkbox box(&panel);
 * box.clicked.Subscribe([]{ EnableSomething(); });
 * box.cancelled.Subscribe([]{ DisableSomething(); });
 * ```
 */
class Checkbox : public ToggleLike {
public:
    explicit Checkbox(InkingWidget* parent,
                      const ToggleData& data = MakeCheckboxData());

protected:
    /// 内置勾（两段圆头线）；`markShape` 换成别的形状时按那个画。
    void paintMark(SDL_Renderer* renderer, const ToggleParts& parts,
                   float magnification, float pixelX, float pixelY) const override;
};

/**
 * @brief 单选框：**同组只有一个为真**。互斥由 `RadioGroup` 负责。
 *
 * 不挂组时它就是"点了不能自己取消的复选框"——单用也安全，只是没有互斥。
 */
class Radio : public ToggleLike {
public:
    explicit Radio(InkingWidget* parent, const ToggleData& data = MakeRadioData());

    ~Radio() override;

    Radio(const Radio&) = delete;
    Radio& operator=(const Radio&) = delete;

    /// 挂进 / 退出一个组（`nullptr` = 不挂）。**不接管所有权**。
    void SetGroup(RadioGroup* group) noexcept;
    RadioGroup* GetGroup() const noexcept { return _group; }

protected:
    bool AllowsCheckedChange() const noexcept override { return !IsChecked(); }

    /// 点击：挂了组就走组（互斥 + 事件），没挂组就直接选中。
    void onPointerClick(InkingPointerButton button, float designX,
                        float designY) override;

private:
    RadioGroup* _group = nullptr;
};

/**
 * @brief 显式单选组：负责"组内只有一个选中"。
 *
 * **只管成员与互斥，不拥有成员**：`Radio` 析构时自己退出组，所以组比成员先死
 * 也安全（组析构时会把成员的 `_group` 清空）。
 */
class RadioGroup {
public:
    RadioGroup() = default;
    RadioGroup(const RadioGroup&) = delete;
    RadioGroup& operator=(const RadioGroup&) = delete;
    ~RadioGroup();

    /// 加一个成员（幂等；已经在组里就什么都不做）。
    void Add(Radio* radio);
    /// 摘掉一个成员（幂等）。
    void Remove(Radio* radio);

    /**
     * @brief 选中组里的某一个：**先清其它、再选它**。
     *
     * 顺序不能反——反过来会有一帧"两个同时选中"被画出来。事件是 1 + N 条
     * （目标 `clicked`、其它各 `cancelled`），见文件头第 2 条。
     *
     * `radio` 不在组里时什么都不做（不报错：调用点多，容错比拼字符串便宜）。
     */
    void Select(Radio* radio);

    /// 现在选中的那个（可能为空）。
    Radio* GetChecked() const noexcept;
    std::size_t GetCount() const noexcept { return _radios.size(); }

private:
    std::vector<Radio*> _radios{};
};

/**
 * @brief 开关：拨一下在"开 / 关"之间翻，滑块带 0.15 秒过渡。
 *
 * 过渡靠 `onAnimationTick`（每渲染帧一次）；`SetAnimated(false)` 之后**立刻跳到位**
 * 且布局一个像素不变（§6 第 110 条：动效必须有一条"关掉"的路径）。
 */
class Switch : public ToggleLike {
public:
    explicit Switch(InkingWidget* parent, const ToggleData& data = MakeSwitchData());

    /// 关掉过渡（幂等）。关掉时把滑块**立刻摆到目标位置**，不等下一帧。
    void SetAnimated(bool animated) noexcept;
    bool IsAnimated() const noexcept { return _animated; }

    /// 滑块位置：0 = 关端，1 = 开端，动画中在中间。自检与"跟手"类逻辑用它。
    float GetKnobRatio() const noexcept { return _knobRatio; }

protected:
    void layoutMark(const InkingShapeBounds& base, ToggleParts& parts) const override;
    void onCheckedChanged(bool checked) override;
    void onAnimationTick(float deltaSeconds) override;

    /// 轨道上的圆滑块**关着也在**（它不是"勾"那种"有 = 选中"的记号）。
    bool PaintsMarkWhenUnchecked() const noexcept override { return true; }

private:
    /// 过渡时长（秒）。0.15 是"能看见、但不拖沓"的那一档。
    static constexpr float kTransitionSeconds = 0.15f;

    bool _animated = true;
    bool _animating = false;
    /// `mutable`：`layoutMark` 是 const，而它要读这个值。
    mutable float _knobRatio = 0.0f;
};

}  // namespace inking_prefab
