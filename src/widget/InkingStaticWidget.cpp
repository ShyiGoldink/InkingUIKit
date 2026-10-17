// 静态档 Widget 的实现（`include/widget/InkingStaticWidget.h`）。
//
// 这个档位**没有几何写入口**——那不是漏了，是"静态组件几何构造即定型"这条
// 硬保证的实现方式：拿到 `InkingWidget*`（命中表里存的就是它）也改不了几何。
// 唯一那条口子是基类里 private 的 `ApplyLayoutRect`（布局层 friend 专用）。

#include <widget/InkingStaticWidget.h>

#include <detail/InkingGeometry.h>

namespace inking {

// 静态档：几何构造即定型，所以 dynamic 传 false。
InkingStaticWidget::InkingStaticWidget(InkingWidget* parent,
                                       const AnchorData& data)
    : InkingWidget(parent, data, false) {}

InkingStaticWidget::InkingStaticWidget(InkingWidget* parent,
                                       const std::string& name)
    : InkingWidget(parent, name, false) {}

void InkingStaticWidget::onRender(float pixelX, float pixelY) const {
    // 第 0 层渲染：一块实心矩形（SDF 形状层接手后换掉 detail::FillDefaultRect）。
    detail::FillDefaultRect(*this, pixelX, pixelY);
}

}  // namespace inking
