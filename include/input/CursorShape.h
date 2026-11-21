#pragma once

//提供鼠标形状的枚举

#include <cstddef>
#include <cstdint>

//光标形状。默认Default，不声明就是箭头
enum class InkingCursorShape : std::uint8_t {
    Default = 0,  //箭头
    Hand,         //手型
    Text,         //I 形
    Crosshair,    //十字
    Count,        //哨兵，形状总数，不是可用形状
};

// 形状总数，用于窗口里那张缓存表的长度
inline constexpr std::size_t InkingCursorShapeCount = static_cast<std::size_t>(InkingCursorShape::Count);
