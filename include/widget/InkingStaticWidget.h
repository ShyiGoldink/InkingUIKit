#pragma once

// 静态档 Widget：**几何构造即定型**。
//
// 它没有几何写入口，基类里也没有——**唯一的例外是布局层那条 private 通道**
// `InkingWidget::ApplyLayoutRect`（只有 `detail::layout::ArrangeSubtree` 是它的
// friend，业务代码调不到）。所以就算拿到 `InkingWidget*`（命中表里存的就是它）
// 也改不了几何。这是**类型上的硬保证**，不靠约定。
//
//   - 静态：构造时定型，之后几何不动 → 进命中表；
//   - 动态：见 `InkingDynamicWidget`（几何会自己变 → 不进表，每帧收一次 Tick）。
//
// 分流不靠运行期标记：谁属于哪一档，在**登记**那一刻就是明确的。

#include <widget/InkingWidget.h>

namespace inking {

class InkingStaticWidget : public InkingWidget {
public:
    InkingStaticWidget(InkingWidget* parent, const AnchorData& data);
    InkingStaticWidget(InkingWidget* parent, const std::string& name);

protected:
    /// 默认画成一块实心矩形，用 `AnchorData` 里的 color。
    ///
    /// 这是第 0 层渲染：够用、能看，而且**不用每个静态组件都自己写一遍**。
    /// SDF 形状层接手后，这里换成"按形状描述生成 SDF 并合批"，
    /// 但"基类给个默认实现、子类按需重写"这个结构不用变。
    void onRender(float pixelX, float pixelY) const override;
};

}  // namespace inking
