#pragma once

// 预制件 C2「菜单栏」的**纯数据**面。
//
// 它就是"窗口顶部一条横向容器"：里面放按钮、图片、**任意 `InkingWidget`**。
// 所以这里的数据面很小——内容项是"锚点对象"（调用方自己造 / 用工厂造），
// 本结构只管外观、排列、以及"要不要接管标题栏"那三件事。

#include <button/ButtonData.h>
#include <dataStruct/InkingLayout.h>
#include <panel/PanelData.h>

namespace inking::prefab {

/// 模块名常量：日志与自检要用。
inline constexpr const char* kMenuBarModuleName = "MenuBar";

/**
 * @brief 菜单栏的外观、排列与"接管标题栏"的开关。
 *
 * 排列规则写在 `panel.layout` 里（和 C1 同一条约定：不抄第二份 axis / gap / padding）。
 * "偏左 / 偏右"就是 `justify` 的 `Start` / `End`，"两端顶住"是 `SpaceBetween`
 * ——任务里特意写明了这三个映射，本类不另立词汇。
 */
struct MenuBarStyle {
    /**
     * 菜单栏那一层：高度、背景、形状 + **排列规则**（`layout`）。
     *
     * 默认：横向 `Stack` + `justify = Start`（偏左）+ `align = Center`
     * + `padding = {6, 2, 6, 2}`。
     */
    PanelData panel{};

    /**
     * 无边框窗口下"自绘标题栏"那三个字（最小化 / 关闭）的外观模板。
     *
     * 只在 `captionButtons = true` 时用得上。默认是 `MakeDefaultCaptionLook()`
     * ——透明底 + 浅灰字，悬停才亮起来（和真实标题栏按钮的观感同源）。
     */
    ButtonData captionLook{};

    /**
     * 按住菜单栏的**空白处**能拖动窗口吗（无边框下的标题栏行为）。
     *
     * 拖动落在 `InkingWindow::MoveWindowBy` 上：位移按**屏幕像素**累加，
     * 设计坐标的位移乘上展示倍率。默认 **false**——普通菜单栏不该拖窗口，
     * 这是"替换系统标题栏"那条路才需要的开关（任务 C2 的附带项）。
     */
    bool draggable = false;

    /// 右上角加"最小化 / 关闭"两个按钮吗（无边框下才有意义）。
    bool captionButtons = false;

    /// 一个标题栏按钮多宽（设计坐标）。
    float captionWidth = 40.0f;

    /**
     * 菜单项文字的颜色（0xAARRGGBB）。**0 = 用按钮外观自带的那份**。
     *
     * 与 `OptionBarStyle::textColor` 同一条理由：CXXCSS 的 `text` 段没有颜色
     * （文字色属于主题 / 运行期），所以"用自己那份 json 外观"时要在这里补。
     * 只有 `AddButton` 造出来的按钮会被它改到（`AddItem` 收的是任意锚点）。
     */
    std::uint32_t textColor = 0;

    /**
     * 全屏时自动隐藏整条菜单栏（默认 **true**）。
     *
     * 菜单栏在"当标题栏用"时全屏几乎必然要隐藏：用户要看的是内容，不是那条
     * 替系统标题栏打工的横条。逻辑在 `MenuBar` 里订阅窗口的 `fullscreenChanged`
     * 完成（**事件**，不是每帧轮询——在 `onRender` 里改可见性属于"提交期改结构"，
     * 那正是本仓库反复避开的那类悬垂）。
     *
     * 隐藏时只记"被这条规则藏起来了"；退出全屏只恢复那一次，不覆盖业务
     * 自己调过的 `SetVisible(false)`。
     */
    bool hideInFullscreen = true;

    /**
     * 吃鼠标滚轮做**窗口缩放**（默认 **true**）。
     *
     * 为什么这件事归菜单栏：无边框窗口没有系统边缘可拖（SDL 的 borderless 在
     * Windows 上是 `WS_POPUP`，没有 `WS_THICKFRAME`），"改尺寸"就没人管了。
     * 菜单栏是唯一始终贴在窗口顶部的东西，让它接滚轮是最省事的代偿。
     *
     * 缩放本身**不改内容倍率**，而是按档位等比改窗口尺寸——letterbox 下黑边的
     * 唯一来源就是"窗口像素比 ≠ 设计像素比"，所以只有改窗口尺寸能让黑边消失。
     * 详见 `InkingWindow::SetZoom` / `GetZoomLadder`。
     *
     * 与 `panel.layout` 的滚动**不叠加**：两者都开时缩放优先（滚一下只发生一件事）。
     */
    bool zoomWheel = true;

    /// 滚轮一格走几档缩放（默认 1）。
    int zoomWheelStep = 1;

    /// 一份能直接用的默认样式（横向、偏左、深色底）。
    static MenuBarStyle MakeDefault();
};

/// 一份"透明底 + 浅灰字、悬停变亮"的标题栏按钮外观。
ButtonData MakeDefaultCaptionLook();

}  // namespace inking::prefab
