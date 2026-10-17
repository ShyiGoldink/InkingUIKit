#pragma once

// 动态档 Widget：**几何会自己变**，所以不进命中表、自己维护命中状态，
// 每帧收到一次 `Tick`。
//
// 与静态档的唯一区别就是下面这四个几何写入口：

#include <widget/InkingWidget.h>

namespace inking {

class InkingDynamicWidget : public InkingWidget {
public:
    InkingDynamicWidget(InkingWidget* parent, const AnchorData& data);
    InkingDynamicWidget(InkingWidget* parent, const std::string& name);

    // ---------------- [final] 写入口：几何变化 ----------------
    // 只有动态版有这几个入口。同样不加 virtual，标脏与钩子都在内部完成；
    // 返回 bool 表示这次是否真的改了，没变就不标脏、不触发钩子。

    /// 改自身尺寸；某个方向传 `InkingResize::none` 表示该方向不动，负值忽略。
    bool Resize(int width, int height) noexcept;
    bool ChangeSelfAnchor(const AnchorPoint& anchor) noexcept;
    bool ChangeTraceAnchor(const AnchorPoint& anchor) noexcept;
    bool ChangeOffset(float offsetX, float offsetY) noexcept;

    /// 每帧一次，由场景调。静态档压根没有这个入口。
    void Tick(float deltaSeconds) noexcept { onTick(deltaSeconds); }

protected:
    /// 每帧一次的钩子，动态组件按需重写。
    virtual void onTick(float deltaSeconds);

    /// 默认画成一块实心矩形，和静态版一致：动态组件常常只是位置在动，
    /// 形状本身没变，所以默认实现能直接复用。
    void onRender(float pixelX, float pixelY) const override;
};

}  // namespace inking
