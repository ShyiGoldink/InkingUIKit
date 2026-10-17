#include <prefab/ListRow.h>

#include <button/ButtonPaint.h>

namespace inking::prefab {

ListRow::ListRow(InkingWidget* parent, const ButtonData& data,
                 std::uint32_t selectedColor)
    : InkingStaticButton(parent, data), _selectedColor(selectedColor) {}

bool ListRow::SetSelected(bool selected) {
    if (_selected == selected) {
        return false;  // 幂等：没变就一个脏都不标
    }
    _selected = selected;
    // 选中只改填充色：命中区域没变（形状与尺寸一个字没动），所以**只标重绘脏**。
    MarkRepaintDirty();
    return true;
}

void ListRow::onRender(float pixelX, float pixelY) const {
    if (!_selected || _selectedColor == 0u) {
        InkingStaticButton::onRender(pixelX, pixelY);
        return;
    }

    // 选中：把外观那一笔的**显示色**换成高亮色重画一遍。形状 / 描边 / 阴影 / 文字
    // 都走与按钮完全相同的那两个入口（`AGENTS.md` §6 第 95 条那条"只有一份画法"：
    // 换颜色不该顺手把绘制实现抄第二遍）。
    const ButtonData& data = GetData();
    const TransformSpec& transform = GetDisplayTransform();
    detail::PaintButtonAppearance(*this, GetShape(), GetAppearance(), data.stroke,
                                  data.shadow, _selectedColor, transform, pixelX,
                                  pixelY);
    detail::PaintButtonText(*this, data, transform, pixelX, pixelY);
}

}  // namespace inking::prefab
