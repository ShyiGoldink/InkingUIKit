#pragma once

#include <cstddef>
#include <cstdint>

//统一指针语义，将触摸和鼠标的输入统一为两种类型
enum class InkingPointerButton : std::uint8_t {
    Primary = 0,    //鼠标左键 / 触摸轻点
    Secondary = 1,  //鼠标右键 / 触摸长按
    Count,          //哨兵：按钮总数，不是可用按钮
};

// 按钮个数。所有"按按钮并行"的数组都以它为长度。
inline constexpr std::size_t InkingPointerButtonCount = static_cast<std::size_t>(InkingPointerButton::Count);
//事件来源，如果是触摸就需要转语义，由窗口确认
enum class InkingPointerSource : std::uint8_t {
    Mouse = 0,
    Touch = 1,
};

//将枚举值转换为数组下标，所有"按按钮并行"的数组都用它索引
inline constexpr std::size_t InkingToIndex(InkingPointerButton button) noexcept {
    return static_cast<std::size_t>(button);
}

//各按键按下的状态
struct InkingPointerDownState {
    bool primary = false;
    bool secondary = false;

    // 有任意一个键按着吗
    bool any() const noexcept { return primary || secondary; }

    bool IsDown(InkingPointerButton button) const noexcept {
        return button == InkingPointerButton::Primary ? primary : secondary;
    }

    void SetDown(InkingPointerButton button, bool down) noexcept {
        if (button == InkingPointerButton::Primary) {
            primary = down;
        } else {
            secondary = down;
        }
    }

    // 整包比较。
    friend bool operator==(const InkingPointerDownState&, const InkingPointerDownState&) = default;
};

static_assert(InkingPointerButtonCount == 2,"加了新的 InkingPointerButton 就必须同步扩 InkingPointerDownState 与 IsDown");

