#pragma once

// 定点浮窗（0.4.1 T-06）：一个**固定几何 + 自己定位**的动态面板。
//
// ---------------------------------------------------------------------------
// 它是什么
//
//   - 基类是 `InkingDynamicWidget`：几何会自己变（打开时定位、内容变了改尺寸），
//     所以**不进命中表**，每帧不糊一层"快照"；
//   - 语义是**吃掉**：`OwnsSubtreeHit()` 为真、`SetHitPassThrough(false)` 固定，
//     点在它身上（含空白区）不会透到下面的组件。**不提供穿透开关**——
//     "点透传到下层"是扩展语义，0.4.1 不排期（见任务书 §7）；
//   - 内容是调用方的事：`AddItem(任意 widget)`，本类只管"框在哪、吃不吃、什么时候关"。
//
// ---------------------------------------------------------------------------
// 定点与"不出窗口"
//
// `OpenAt(x, y)` 的 `(x, y)` 是**父级坐标里的一个定点**（通常就是点击位置）。
// 落位由 `detail::SolvePopupPlacement` 算（纯算术，可单测）：
//
//   1. 先按自锚点 `(0,0)` 摆 ⇒ 向右下展开；
//   2. 该方向放不下就**翻到对侧**（自锚点变 `(1,1)`）；
//   3. 还放不下就夹到范围内；**尺寸大于范围时贴住左上、允许另一侧溢出**
//      （"除非真的过大"）——此时 `IsClampedX/Y()` 为真，可断言、不静默。
//
// 默认范围 = **父级矩形**。挂在场景下时父级就是设计画布（= 窗口），
// 挂在某个面板里时以那个面板为界；`SetClampBounds` 可以再收窄。
//
// ---------------------------------------------------------------------------
// 焦点与关闭
//
//   - `OpenAt` 时默认**夺取键盘焦点**（`SetTakeFocusOnOpen(false)` 可关），
//     打开期间是同场景的**焦点模态**——这是菜单类浮层的语义；
//   - 点外部默认**关闭**（`SetCloseOnOutsideClick(false)` 可关）；
//   - 这两件事都在 `onPointerStateChanged` 里判（0.4.1 T-02 的指针变化通知），
//     **不在 `onTick` 里**：鼠标不动就不判，也就不反复抢焦点。
//
// 继承扩展点：`onPopupOpened` / `onPopupClosed` / `onOutsidePressed`。
// 想换内容组织方式就派生并重写它们，不必改本类。

#include <dataStruct/InkingAnchorData.h>
#include <dataStruct/InkingShapeSpec.h>
#include <dataStruct/InkingTransform.h>
#include <detail/InkingPointerProbe.h>
#include <event/InkingEvent.h>
#include <panel/PanelData.h>
#include <widget/InkingDynamicWidget.h>

#include <memory>

namespace inking {

/**
 * @brief 定点浮窗：固定几何、自己定位、点击吃掉的面板。
 *
 * ```cpp
 * inking::PanelData data;
 * data.name = "context_menu";
 * data.width = 200; data.height = 120;
 * data.closed.color = 0xFFFFFFFFu;
 * data.shape = inking::ShapeSpec::RoundedRect(16.0f);
 *
 * inking::InkingFixedPopup popup(&scene, data);
 * popup.AddItem(std::make_unique<...>(nullptr));   // 内容自己决定
 * popup.OpenAt(clickX, clickY);                    // 定点打开（父级坐标）
 * ```
 */
class InkingFixedPopup : public InkingDynamicWidget {
public:
    /**
     * @param parent 上级节点（通常是场景）。
     * @param data   面板配置。**只用 `closed` 那一份**（颜色 / 形状 / 描边 / 阴影 /
     *               尺寸 / 布局 / z）：浮窗是"开 / 关"而不是"两态过渡"，`open`、
     *               `startOpen`、`scroll` 在这里被忽略（写进注释而不是静默丢弃）。
     */
    InkingFixedPopup(InkingWidget* parent, const PanelData& data);

    // ---------------- 定点（调用点就这五个） ----------------

    /**
     * 在定点打开（父级坐标，通常传点击位置）。
     *
     * 已经开着时只**挪位置**，不重发 `opened`；关着时依次做：落位（`Relayout`
     * → 算落位 → 写锚点 / 偏移）→ 变可见 → 取焦点（按 `takeFocus`）→ `opened`。
     */
    void OpenAt(float targetX, float targetY);

    /// 只改定点（开着就重新落位；关着就先记下来，等 `OpenAt` 用）。
    void SetTarget(float targetX, float targetY);

    /// 关闭（幂等）。焦点若在本子树则**当场释放**——焦点是场景级唯一归属。
    void Close();

    bool IsOpen() const noexcept { return _open; }

    float GetTargetX() const noexcept { return _targetX; }
    float GetTargetY() const noexcept { return _targetY; }

    // ---------------- 内容 ----------------

    /**
     * 挂一个**由本浮窗拥有**的子项（任意 `InkingWidget`）。
     *
     * 挂完把 z 抬到浮窗之上一层（同 `MenuBar::AddItem`：只给容器高 z 会出现
     * "背景在上、内容被盖"）并重排一次（幂等）。
     */
    InkingWidget* AddItem(std::unique_ptr<InkingWidget> item);

    // ---------------- 行为开关 ----------------

    /// 点外部自动关闭（默认 true）。
    void SetCloseOnOutsideClick(bool on) noexcept { _closeOnOutside = on; }
    bool IsCloseOnOutsideClick() const noexcept { return _closeOnOutside; }

    /// 打开时夺取键盘焦点（默认 true）。关掉之后焦点模态随之消失。
    void SetTakeFocusOnOpen(bool on) noexcept { _takeFocusOnOpen = on; }
    bool IsTakeFocusOnOpen() const noexcept { return _takeFocusOnOpen; }

    /// 收窄落位范围（父级坐标）。默认 = 父级矩形。
    void SetClampBounds(const ShapeBounds& bounds);
    /// 恢复默认范围（父级矩形）。
    void ClearClampBounds() noexcept;

    // ---------------- 观测（自检断言用） ----------------

    int GetResolvedSideX() const noexcept { return _sideX; }
    int GetResolvedSideY() const noexcept { return _sideY; }
    bool IsClampedX() const noexcept { return _clampedX; }
    bool IsClampedY() const noexcept { return _clampedY; }

    const ShapeSpec& GetShape() const noexcept { return _shape; }

    /// 打开了（已经落位、已经可见）。参数为空。
    InkingEvent<> opened;
    /// 关闭了（已经不可见）。
    InkingEvent<> closed;

protected:
    bool HitTest(float worldX, float worldY) const override;

    /// 子树由我判（子孙不进命中表）——浮窗是容器的标准语义。
    bool OwnsSubtreeHit() const noexcept override { return true; }

    /// 整棵子树的并集（子孙溢出本体时，包络也要盖住）。
    ShapeBounds GetHitEnvelope() const override;

    /// 填充 + 阴影（阴影在形状之下，和面板同一条）。
    void onRender(float pixelX, float pixelY) const override;

    /// 描边画在子孙**之上**（装饰遍）——跟着填充走会被内容盖住。
    void onRenderDecoration(float pixelX, float pixelY) const override;

    /**
     * 指针变了（场景推，鼠标不动不推）：判"点外部关闭" + "把焦点取回来"。
     *
     * 顺序不能反：先判外部按下（可能要关），再判焦点（关掉之后不该再抢）。
     */
    void onPointerStateChanged() override;

    /// 打开且允许取焦点时，本节点想要键盘焦点。
    bool WantsKeyboardFocus() const noexcept override {
        return _open && _takeFocusOnOpen;
    }

    // ---------------- 继承扩展点 ----------------

    virtual void onPopupOpened() {}
    virtual void onPopupClosed() {}

    /**
     * 指针在浮窗**外部**按下。默认按 `closeOnOutside` 关闭。
     *
     * 派生类可以重写它做别的事（例如先播一段收起动画再关），但**要自己保证
     * 最终状态正确**——不关也不抢焦点是允许的，只是那之后这个浮窗不会自动消失。
     */
    virtual void onOutsidePressed();

private:
    /// 按当前定点 / 尺寸 / 范围重算落位，写进自锚点与偏移。
    void applyPlacement();

    /// 实际生效的落位范围（显式设置 > 父级矩形 > 设计画布）。
    ShapeBounds effectiveClampBounds() const;

    /// 键盘焦点是否在本子树内（含自己）。
    bool IsFocusInsideSubtree() const;

    ShapeSpec _shape{};
    PanelAppearance _appearance{};  ///< 只用 `closed` 那份
    ShapeBounds _clampBounds{};
    bool _hasClampBounds = false;

    float _targetX = 0.0f;
    float _targetY = 0.0f;
    int _sideX = 1;
    int _sideY = 1;
    bool _clampedX = false;
    bool _clampedY = false;

    bool _open = false;
    bool _closeOnOutside = true;
    bool _takeFocusOnOpen = true;

    detail::PointerProbe _probe;
};

}  // namespace inking
