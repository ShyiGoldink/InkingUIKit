#pragma once

// 预制件 C3 的内部件：**列表里的一行**。
//
// 它就是 `InkingStaticButton`（三态、点击、文字、形状、描边阴影全是现成的），
// 只多一个"选中"标志：选中时把外观那一笔的**填充色**换成高亮色。
//
// 为什么不给按钮加"第四态"：三态是**指针状态**（悬停 / 按下），选中是**内容状态**
// （业务决定的），两者在按钮的状态机里是两个维度。真要把它们合起来得动底层
// （`ButtonLook` 的状态机），那是"每类词汇一次"的活，不在"用现有配件组装"这条
// C 系列的路上。所以这里用最薄的一层覆盖：重写 `onRender`，换一个颜色画。
//
// 它是 `prefab` 命名空间里的**内部件**（装配件），不是给人单独用的组件——
// 所以它不进 `src/inking.cpp` 的组件断言清单，也没有自己的 CXXCSS 类型。

#include <button/InkingStaticButton.h>

#include <cstdint>

namespace inking::prefab {

/**
 * @brief 列表里的一行：按钮 + 一个"选中"标志。
 *
 * 选中态只改**填充色**（文字、形状、尺寸、命中区域一个字节都不动），
 * 所以它不影响命中表，也不需要重建节点。
 */
class ListRow : public InkingStaticButton {
public:
    ListRow(InkingWidget* parent, const ButtonData& data,
            std::uint32_t selectedColor);

    /**
     * 设选中（幂等）。
     *
     * @return 真的变了才返回 true（会标重绘脏）。
     */
    bool SetSelected(bool selected);

    bool IsSelected() const noexcept { return _selected; }

    /// 选中时用的填充色（0 = 不覆盖，等同于没选中）。
    std::uint32_t GetSelectedColor() const noexcept { return _selectedColor; }

protected:
    /// 选中时换填充色重画一遍（**只有一份画法**：形状 / 描边 / 阴影 / 文字
    /// 都走与按钮完全相同的那两个 `detail::` 入口，见 `include/button/ButtonPaint.h`）。
    void onRender(float pixelX, float pixelY) const override;

private:
    std::uint32_t _selectedColor = 0;
    bool _selected = false;
};

}  // namespace inking::prefab
