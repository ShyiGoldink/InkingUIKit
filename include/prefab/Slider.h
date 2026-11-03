#pragma once

// 预制件「滑块」（`docs/zh/plans/2026-11-03-uikit-completeness-tasks.md` T-02）。
//
// **它就是**：一条轨道 + 一段已填充 + 一个圆形滑块，拖动 / 点击 / 键盘都能改值。
//
// ---------------------------------------------------------------------------
// 为什么进 `prefab/` 而不是 `control/`，也不进 CXXCSS
//
// 判据见任务书 §0 取向第 1 条：配置量小（一个滑块四个值），而让生成器认一套
// 新组件类型要同时改 `Fields.h` / `Cxxcss.cpp` / `Emit.cpp` 三处并重跑 13 条
// 自检——那是给"配置量大、要在外部编辑"的东西准备的路。`MenuBar` /
// `OptionBar` / `Select` / `ListView` / `FilePicker` 都已经这么办了。
//
// ---------------------------------------------------------------------------
// 三条实现约束（每条都有仓库自己的教训作依据）
//
//   1. **拖动用"相对按下点的累计位移"**，不用本次事件的增量（§6 第 96 条）：
//      用增量的话除第一帧外每次都被算回原位，手感是"稍微拉开一点就弹回去、
//      像在对抗缩放"；
//   2. **键盘要能用就得 `WantsKeyboardFocus()` 返回真**（§6 第 57 条）：否则
//      "点得进去、按箭头没反应"，而且**不报错**；
//   3. **绘制走 `inking_detail::fillShape`**，不另写一份圆角矩形（§6 第 95 条：
//      只有一份画法）。命中与绘制读同一个 `GetParts()`（§2.5.3）。
//
// ---------------------------------------------------------------------------
// 与任务书字面不同的两处（都是任务书自身冲突，实测后按"能站住的那条"写）
//
//   ① **「thumb 落在 track 之内」只能按水平方向成立**。默认滑块直径 18、
//      轨道高 6——圆盘天生比轨道高，垂直方向必然溢出（任务书 §4 T-02 的
//      「风险与回退」自己写着"圆盘要溢出轨道矩形"）。所以不变量写成：
//      圆盘的**水平范围**落在轨道之内、**垂直中心**与轨道中心重合。把圆盘
//      硬裁进轨道会是"半个圆"，那是 bug 不是约束。
//   ② **命中认整块组件框**，不是只认那 6 像素高的轨道条。任务书说"命中只认
//      track"，但 6px 高的热区在 1080p 下几乎点不中；而"圆盘不单独进命中"
//      这条真意图由"没有第二个热区"满足。所以热区 = 组件框，x→值 的换算
//      用轨道跨度。
//
// ---------------------------------------------------------------------------
// 第一版**不做**（写清楚比含糊着强）
//
//   - **`SetPainter` 第三档自绘**：`InkingCanvasContext` 的构造是 `private`、
//     `friend` 只有 `InkingStaticCanvas`（它必须带一个 `CanvasRecorder`），
//     所以叶组件挂不了绘制器。要整块自绘请把 `InkingStaticCanvas` 叠在上面。
//     颜色 / 图 / SVG / 形状 / 圆角这些第一、二档都照常（§2.5.4）；
//   - 悬停 / 按下的外观过渡（圆盘没有第四态：手感由"跟手"体现，不由变色体现）；
//   - 竖向滑块（`layout` 那一维的活，先只做横向）；
//   - 双滑块（区间选择）。

#include <widget/InkingStaticWidget.h>
#include <event/InkingEvent.h>
#include <button/ButtonData.h>
#include <dataStruct/InkingAnchorData.h>
#include <dataStruct/InkingDecoration.h>
#include <dataStruct/InkingShapeSpec.h>

#include <string>

// 只前向声明：`onKeyDown` 的声明只要类型名（同 `prefab/MenuPopup.h` 的做法）。
struct InkingKeyEvent;

namespace inking_prefab {

/**
 * @brief 滑块的数据（构造时给一次）。
 *
 * 三份外观都是 `InkingButtonAppearance`（§2.5.2）：名字里带 Button 是历史包袱，
 * 语义早就是"一层外观：颜色 / 图 / SVG / 纹理矩形"。于是"换主题色 / 贴图 / 换
 * 矢量"三件事**不用改库**。
 *
 * 定位与尺寸复用 `InkingAnchorData`（锚点 / 偏移 / `width` / `height` / `zIndex` /
 * `visible`）——那是"静态组件初始化数据"的既有口径，新造一份只会漏字段
 * （`AGENTS.md` §6 第 83 条）。
 *
 * ⚠️ 各部件的**形状**在这份数据里（`trackShape` / `thumbShape`），因为它们决定
 * "画出来什么形"，而"形"参与命中（形状是唯一真相）。填充段的形状跟轨道走
 * ——填充是轨道的一段，两者形状必须同族，否则窄宽度下会露边。
 */
struct SliderData : public InkingAnchorData {
    /// 默认就是"能用的那一个"：组件框 200×28、值域 0~1、步长 0（连续）。
    /// 外观那一份是黑的，用 `MakeDefault()` 才拿到主题色。
    SliderData() noexcept;

    std::string name = "slider";

    // ---- 值域 ----
    float min = 0.0f;
    float max = 1.0f;
    float value = 0.0f;
    /// 量化步长；`<= 0` = 连续（不量化）。量化相对 `min` 取整。
    float step = 0.0f;

    // ---- 两个尺寸 ----
    /// 轨道高（设计坐标）。轨道横向铺满组件框、纵向居中。
    float trackHeight = 6.0f;
    /// 滑块圆盘直径。**故意大于 `trackHeight`**：圆盘压在轨道上，垂直方向溢出
    /// 是设计（见下面 ①），不是裁切 bug。
    float thumbDiameter = 18.0f;

    // ---- 外观（三部件，见 §2.5.5）----
    InkingButtonAppearance track{};  ///< 轨道底
    InkingButtonAppearance fill{};   ///< 已填充段
    InkingButtonAppearance thumb{};  ///< 滑块圆盘
    /// 圆盘的描边（亮色场景里白盘压在浅底上要靠它站住）。宽度 0 = 不画。
    InkingStrokeSpec thumbStroke{};
    InkingShapeSpec trackShape = InkingShapeSpec::Capsule();
    InkingShapeSpec thumbShape = InkingShapeSpec::Circle();

    /// 一整套默认外观（`PrefabTheme` 的紫 + 白）。
    static SliderData MakeDefault();
};

/// 滑块由哪几个面组成（**绘制与命中读同一份**，§2.5.3）。
struct SliderParts {
    InkingShapeBounds track;  ///< 轨道（横向铺满组件框、纵向居中）
    InkingShapeBounds fill;   ///< 已填充段（从轨道左端长到 `ratio` 处）
    InkingShapeBounds thumb;  ///< 滑块圆盘（**水平范围**落在轨道内）
};

/**
 * @brief 预制件：滑块。
 *
 * ```cpp
 * inking_prefab::Slider slider(&scene, inking_prefab::SliderData::MakeDefault());
 * slider.changed.Subscribe([](float now, float before) { SetVolume(now); });
 * slider.released.Subscribe([] { SaveSettings(); });   // 抬手才落盘
 * ```
 *
 * 事件只有**两个槽**（§2.5.1），没有 `SetOnChanged` 那种便利写法：槽可以挂多个
 * 订阅，而"替换式 setter"挂第二个就把第一个冲掉了（`InkingButtonLike.h:222` 自己
 * 写着这是坑）。
 */
class Slider : public InkingStaticWidget {
public:
    /**
     * 值变了。参数是 `(现在, 之前)`。
     *
     * **只在真变时发**：同一个值再设一次不 Emit（`SetValue` 返回 false）。
     * 回调里读 `GetValue()` 已经是新值——写入口的顺序是"改字段 → 标脏 → Emit"。
     */
    InkingEvent<float, float> changed;

    /**
     * 一次拖动/点击**结束**了（主键抬起时一次）。
     *
     * 业务提交用它：`changed` 在拖动过程中会来几十次，落盘 / 发网络请求该等到
     * 这一下。键盘改值不会发（没有"抬起"这回事）。
     */
    InkingEvent<> released;

    explicit Slider(InkingWidget* parent,
                    const SliderData& data = SliderData::MakeDefault());

    // ---- 值 ----

    /**
     * @brief 设值：夹到 `[min, max]` → 按 `step` 量化 → 变了才写。
     *
     * @return 真的变了才返回 true（并 `changed.Emit`）；相等返回 false、一个脏都不标。
     *
     * 顺序不能反：**先改值、再标脏、最后发事件**（`InkingEvent.h` 第 2 条契约）
     * ——订阅者在回调里读到的必须是新值。
     */
    bool SetValue(float value);

    float GetValue() const noexcept { return _value; }

    /// 现在的位置比例（0~1）。`max == min` 时恒 0（不是一个除零的 NaN）。
    float GetRatio() const noexcept;

    /// 构造时用的那份数据（已经 `Normalize` 过的范围 / 步长写在 `_value` 上，
    /// 这里返回的是**调用方给的那份**）。
    const SliderData& GetData() const noexcept { return _data; }

    /**
     * 三个部件的矩形（相对组件左上角，设计坐标）。**绘制与命中读同一份**。
     *
     * 每次调用按当前组件框现算（组件被布局拉伸之后不会留一份过期几何）。
     */
    const SliderParts& GetParts() const noexcept;

    /// 键盘焦点：滑块要（否则"点得进去、按箭头没反应"，§6 第 57 条）。
    bool WantsKeyboardFocus() const noexcept override { return true; }

    /**
     * @brief 命中：**整块组件框**（见文件头 ②）。
     *
     * 必须重写：`InkingWidget::HitTest` 基类恒返回 `false`（基类没有形状，
     * 不给"没重写的组件"开一个隐式全命中）。不写这一条的后果是**完全点不到**
     * ——不报错，只是"按下去什么都不发生"。
     */
    bool HitTest(float worldX, float worldY) const override;

    /// 显示变换：滑块没有三态变换，也不认 A13 外部变换（默认就是单位变换）。
    InkingTransformSpec GetDisplayTransform() const noexcept { return {}; }

protected:
    void onRender(float pixelX, float pixelY) const override;
    void onPointerPress(InkingPointerButton button, bool press) override;
    void onPointerDrag(float designX, float designY, float deltaX,
                       float deltaY) override;
    void onKeyDown(const InkingKeyEvent& key) override;

private:
    /// 值域兜底：`max < min` 就对调，`step < 0` 当 0。
    void normalizeRange() noexcept;

    /// 夹取 + 量化（纯函数，不改状态）。
    float clampQuantize(float value) const noexcept;

    /// 本地横坐标 → 值（轨道跨度线性映射）。
    float valueAtLocalX(float localX) const noexcept;

    /// 把 `_parts` 按当前组件框与 `_value` 重算一遍。
    void layoutParts() const noexcept;

    SliderData _data{};
    float _value = 0.0f;

    /// 拖动锚点：按下点的**绝对**设计坐标 + 那一刻的值。
    ///
    /// 存绝对坐标而不是本地坐标：拖动期间组件自己不会动（静态档），但"按下那一刻
    /// 的指针"是唯一能把"当前点 − 按下点"算成位移的基准（`AGENTS.md` §6 第 96 条）。
    float _pressX = 0.0f;
    float _pressValue = 0.0f;
    bool _dragging = false;

    /// 现算的部件（`mutable`：`GetParts()` 与 `onRender` 都是 const）。
    mutable SliderParts _parts{};
};

}  // namespace inking_prefab
