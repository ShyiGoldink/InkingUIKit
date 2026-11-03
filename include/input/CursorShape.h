#pragma once

// 鼠标光标的形状（T-09）：组件声明"指针停在我身上时，光标该长什么样"。
//
// 为什么放在 `input/` 而不是 `window/`：声明者全是**组件**（按钮 / 输入框 /
// 滑块），而窗口层只是**执行者**。放进 window 会让 widget 反过来依赖 window
// ——那是把树倒过来长（`AGENTS.md` §3 第 10 条的归属判据）。
//
// 值很少是刻意的：每一种都要在窗口里缓存一个 `SDL_Cursor*`，而且"多到分不清"
// 比"少而准"更糟。要加新的就走同一条路：这里加一项、窗口那张映射表加一行。

#include <cstddef>
#include <cstdint>

/// 光标形状。**默认 `Default`**：不声明就是箭头，框架不猜。
enum class InkingCursorShape : std::uint8_t {
    Default = 0,  ///< 箭头：默认 / 不可交互
    Hand,         ///< 手型：这里能点（按钮 / 滑块 / 勾选件）
    Text,         ///< I 形：这里能打字（输入框）
    Crosshair,    ///< 十字：这里能拖 / 对准（将来给画布类用）
};

/// 形状总数（窗口里那张缓存表的长度；加一项要一起改）。
inline constexpr std::size_t InkingCursorShapeCount = 4;
