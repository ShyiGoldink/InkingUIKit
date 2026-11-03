#pragma once

// 预制件「进度条」（`docs/zh/plans/2026-11-03-uikit-completeness-tasks.md` T-03）。
//
// **它就是**：一条轨道 + 一块按比例长的填充。**只读**——不吃点击、不进键盘焦点、
// 没有事件槽（§2.5.1：只读控件不给事件）。
//
// ---------------------------------------------------------------------------
// 为什么是"静态档 + 确定进度"
//
//   A 静态档 + `SetProgress`（本实现）：几何构造即定型，改进度只标**重绘脏**；
//   B 也支持不确定进度（跑马灯）：要每帧推进，而静态档收不到 `Tick`（§6 第 37 条），
//     只能挂 `onRender` 每帧跑——一个纯展示控件开每帧路径，与"静态档零每帧成本"
//     的取向冲突。将来真要就另做一个 `DynamicProgressBar`（任务书 §10 明确不做）；
//   C 复用 `Slider` 只读模式：少一个类，但滑块在一个只读进度条上是**错误暗示**
//     （用户会去拖它，拖不动就以为坏了）。
//
// 填充段的换算与 `Slider` **共用同一个纯函数**（`prefab/PrefabParts.h`：
// `FillBoundsInTrack`）——这是批次 b 的门槛之一（§9.1：grep 只有一份定义）。
//
// 与 `Slider` 一样**第一版不给 `SetPainter`**：`InkingCanvasContext` 的构造是
// private（只 `friend InkingStaticCanvas`，必须带一个 `CanvasRecorder`），
// 叶组件挂不了绘制器。要整块自绘请把 `InkingStaticCanvas` 叠在上面。

#include <widget/InkingStaticWidget.h>
#include <button/ButtonData.h>
#include <dataStruct/InkingAnchorData.h>
#include <dataStruct/InkingShapeSpec.h>

#include <string>

namespace inking_prefab {

/// 进度条的数据（构造时给一次）。
///
/// 定位与尺寸复用 `InkingAnchorData`（锚点 / 偏移 / `width` / `height` / `zIndex`），
/// 与 `SliderData` 同一口径。
struct ProgressBarData : public InkingAnchorData {
    /// 默认就是"能用的那一个"：组件框 200×8、进度 0。
    ProgressBarData() noexcept;

    std::string name = "progressBar";

    /// 进度，`0~1`（构造时会被夹）。`0` = 只画轨道，**不画填充块**。
    float progress = 0.0f;

    // ---- 外观（两部件，见 §2.5.5）----
    InkingButtonAppearance track{};  ///< 轨道底
    InkingButtonAppearance fill{};   ///< 填充块
    /// 填充块与轨道**同形状**（填充是轨道的一段：形状必须同族，否则窄宽度下露边）。
    InkingShapeSpec shape = InkingShapeSpec::Capsule();

    static ProgressBarData MakeDefault();
};

/// 进度条由哪几个面组成（**绘制读它，没别的口径**）。
struct ProgressBarParts {
    InkingShapeBounds track;  ///< 轨道（铺满组件框）
    InkingShapeBounds fill;   ///< 填充块（`track.width × progress`；进度 0 时宽高皆 0）
};

/**
 * @brief 预制件：进度条。
 *
 * ```cpp
 * inking_prefab::ProgressBar bar(&panel);
 * bar.SetProgress(0.25f);   // 每帧调同值也不会有额外重绘（相等直接返回 false）
 * ```
 */
class ProgressBar : public InkingStaticWidget {
public:
    explicit ProgressBar(InkingWidget* parent,
                         const ProgressBarData& data = ProgressBarData::MakeDefault());

    /**
     * @brief 设进度：夹到 `[0, 1]`，变了才写。
     *
     * @return 真的变了才返回 true（并标重绘脏）；相等返回 false、一个脏都不标
     *         ——"每帧喂同一个值"于是零额外重绘。
     */
    bool SetProgress(float progress);

    float GetProgress() const noexcept { return _progress; }

    const ProgressBarData& GetData() const noexcept { return _data; }

    /**
     * 两块矩形（相对组件左上角，设计坐标）。每次按当前组件框现算——
     * 组件被布局拉伸之后不会留一份过期几何。
     */
    const ProgressBarParts& GetParts() const noexcept;

protected:
    void onRender(float pixelX, float pixelY) const override;

private:
    /// 把 `_parts` 按当前组件框与 `_progress` 重算一遍。
    void layoutParts() const noexcept;

    ProgressBarData _data{};
    float _progress = 0.0f;

    /// 现算的部件（`mutable`：`GetParts()` 与 `onRender` 都是 const）。
    mutable ProgressBarParts _parts{};
};

}  // namespace inking_prefab
