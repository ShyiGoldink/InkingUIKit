#pragma once

// 菜单浮窗的样式（`MenuPopup` 的构造参数）。
//
// 和 `MenuBarData.h` 同一套写法：**默认值只有一份**（`MakeDefault()`），
// 颜色走 `PrefabTheme.h` 的调色板，不在 .cpp 里另抄一组色。

#include <button/ButtonData.h>
#include <panel/PanelData.h>

#include <cstdint>

namespace inking {
namespace prefab {

/**
 * @brief 一个菜单浮窗的外观与行为。
 *
 * 尺寸三件套：`itemWidth` 是**菜单宽度**（容器宽度 = `itemWidth + padding × 2`），
 * 每一项的高度固定为 `itemHeight`，项间距 `gap`。
 *
 * `itemLook` 是**每一项的默认外观**（形状 / 三态色 / 字号）；`AddButton` 传了
 * 自己的 `ButtonData` 就按那份走（宽高仍可省略：<= 0 时用上面的默认）。
 */
struct MenuPopupStyle {
    int itemWidth = 200;    ///< 菜单内容区宽度
    int itemHeight = 36;    ///< 每一项的高度
    int padding = 6;        ///< 容器内边距（四边同值）
    int gap = 2;            ///< 项间距
    int zIndex = 400;       ///< 高于 `Select` 的 200：浮层要压住普通内容

    /// 统一文字色（0 = 不动每一项自己的）。用自己那份 `ButtonData` 时特别有用。
    std::uint32_t textColor = 0;

    /// 点了一项 / 按了该项的快捷键之后自动关闭（菜单语义）。
    bool closeOnActivate = true;

    /// 每一项的默认外观。宽高可以留 0（`AddButton` 会补成 `itemWidth` / `itemHeight`）。
    ButtonData itemLook{};

    /// 容器外观（只用 `closed` 那一份；`open` / `startOpen` / `scroll` 被忽略）。
    PanelData panel{};

    /// 预制件统一调色板的那一份默认值。
    static MenuPopupStyle MakeDefault();
};

}  // namespace prefab
}  // namespace inking
