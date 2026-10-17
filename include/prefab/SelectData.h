#pragma once

// 预制件 C4「下拉 / 选择器」的**纯数据**面。
//
// 它就是"从固定集合里挑一个"（编辑器里满处是：角色 / 背景 / 动画类型 / 分支目标）：
//   - 收起态 = **一个按钮**（当前值 + 占位提示 + 展开指示）；
//   - 展开态 = **就地显示一个 C1 选项栏**（不引入浮层）。
//
// "就地显示 / 隐藏一个容器"是任务里定死的做法（C 系列三个通用约定第 3 条）：
// 不引入浮层、不引入输入消费。所以"点别处收起"由**调用方约定**
// （别处一点就调 `SetExpanded(false)`），第一版不做浮层那套。

#include <button/ButtonData.h>
#include <panel/PanelData.h>
#include <prefab/OptionBarData.h>
#include <text/InkingTextInput.h>

#include <cstddef>
#include <string>

namespace inking::prefab {

/// 模块名常量：日志与自检要用。
inline constexpr const char* kSelectModuleName = "Select";

/// 展开方向。
enum class SelectExpandDirection : std::uint8_t {
    Down = 0,  ///< 向下（默认）：展开容器挂在收起按钮**下面**
    Up,        ///< 向上：挂在**上面**（贴近窗口底部时用）
};

/**
 * @brief 下拉 / 选择器的外观与行为。
 *
 * 展开态复用 C1：`options` 就是一份 `OptionBarStyle`（面板外观 + 排列 + 项外观）。
 * 所以"换个项外观 / 换个间距"这些配置在 C4 这里**不用再配一遍**——
 * 想换就把 `options.itemLook` 换掉（或者干脆 `options = 你那份`）。
 */
struct SelectStyle {
    /// 收起态那个按钮的外观 + 尺寸（它的宽高也决定展开容器的默认宽度）。
    ButtonData closedLook{};

    /// 展开态那个选项栏（C1）的样式。尺寸默认跟收起按钮同宽。
    OptionBarStyle options{};

    /// 展开方向。
    SelectExpandDirection direction = SelectExpandDirection::Down;

    /// 展开容器的宽度（0 = 跟收起按钮一样宽）。
    float expandWidth = 0.0f;

    /// 展开容器的高度（0 = 用 `options.panel.height`）。
    float expandHeight = 0.0f;

    /// 收起按钮与展开容器之间留的那道缝（设计坐标）。
    float expandGap = 2.0f;

    /**
     * 展开时带一个**过滤输入框**吗。
     *
     * 打开之后，展开的那一刻会把键盘焦点交给过滤框（`SetKeyboardFocus`），
     * 打字即过滤（子串匹配，大小写不敏感）。第一版不做"首字母定位"。
     */
    bool filterable = false;

    /// 过滤框的数据（字号 / 内缩 / 光标颜色…）。`filterable` 为假时用不到。
    TextInputData filterLook{};

    /// 过滤框的高度（设计坐标）。
    float filterHeight = 30.0f;

    /// 没有任何选择时，收起按钮上显示的文字。
    std::string placeholder = "请选择";

    /**
     * 收起时 Select**自己**那一层的 z（默认 0 = 普通内容）。
     *
     * 展开时整块会临时提到 `expandedZIndex`（见下），收起就还原成这个值。
     */
    int zIndex = 0;

    /**
     * 展开时**整块**提到这一层（收起时还原成 `zIndex`）。
     *
     * 默认 200：高于普通内容（0）与菜单栏（100）。
     * 这是"用全局 z 换来的展开在内容之上"——真做局部层级（A8b）之前，
     * 不改框架就只有这一条路。注意它是**从 Select 自己开始**提的：
     * 容器在命中表里只有一条，表里排序看的就是它（`AGENTS.md` §6 第 104 条），
     * 内部那一栏再依次 +1，于是"背景 / 选项 / 过滤框"作为一整块叠放。
     */
    int expandedZIndex = 200;

    /// 一份能直接用的默认样式。
    static SelectStyle MakeDefault();
};

/// 一份"深色圆角 + 浅色字 + 右侧一个 ▾"的收起态外观。
ButtonData MakeDefaultClosedLook();

}  // namespace inking::prefab
