#pragma once

// 动态档 Widget：**几何会自己变**，所以不进命中表、自己维护命中状态，
// 每帧收到一次 `Tick`。
//
// ---------------------------------------------------------------------------
// 与静态档的差异**不止几何**：一共六项（对照表见 docs/zh/ComponentGuide.md §2）
//
//   1. 几何写入口：本类下面四个（静态档没有）；
//   2. 命中：静态进全局命中表，动态**不进表**、自己判，占地记成"洞"；
//   3. 输入：静态由场景**推** `onPointer*`；动态只收到一次
//      `onPointerStateChanged` 通知，自己从场景读状态（`detail::PointerProbe`）；
//   4. 每逻辑步 `Tick`：静态收不到，动态收（场景按固定步长推，见 `Tick`）；
//   5. 渲染帧 `onAnimationTick`：**两档都收**，没差别；
//   6. 默认 `onRender`：两档各写一份，都是一行 `detail::FillDefaultRect`。
//
// 早先这里把两档差异写成"只有几何写入口"——那句话是错的（本文件 `Tick` /
// `onTick` / `onRender` 就摆着），而它是很多下游判断的地基，2026-11-02 改正（G-00）。
//
// 下面这四个是第 1 项：

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
