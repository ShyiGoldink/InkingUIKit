#pragma once

// 指针（鼠标 / 触摸）输入契约：想收指针的组件重写这里的钩子。
//
// 它是 Widget 的一条可选能力，不是另一种节点。
// `InkingWidget` 与它多继承，所以任何 Widget 都天然有这个契约。
// 通过 重写 onPointerHover / onPointerPress/onPointerClick`就能收输入，
// 没重写的拿默认空实现、什么都不会发生。
// 两条设计约束：
//   1. 零状态：这里不存任何字段。拖动归属（"场景记住谁被按下"）住在场景上；
//   2. 钩子即能力发现：场景判出"指针在谁身上 / 谁被按下 / 谁被抬起"之后，
//      把判定结果用 Push 推给那个节点，并且不做任何类型查询
//DispatchPointer早年会把任何非动态节点static_cast<InkingStaticButton*>
//再调MouseHover，第一个不继承 Button 的可命中组件一被指针扫到就是 UB。
// 切勿在这里加 dynamic_cast 的路子，那是把那个 bug 装回来。
//
// 派生类重写钩子时要写 override：漏了就变成"隐藏"而不是"重写"，
// 而场景是拿基类指针调
// 钩子静默不生效

namespace inking {

/**
 * @brief 指针目标：能收指针（鼠标 / 触摸）事件的组件。
 *
 * 钩子全是空实现——"什么都不做"是正确的默认值。
 */
class InkingPointerHandler {
public:
    virtual ~InkingPointerHandler() = default;

    /**
     * @brief 先获取本子树的滚轮
     *
     * 滚轮原本给"指针下最上面那个"，而滚动容器里指针下通常是容器里的子按钮
     * 如果容器收不到滚轮，能直接滚就成了空话。
     * 声明之后，场景会沿父链找最近的声明者，把滚轮先交给它，也就是用户裁定的容器优先。
     * 和 OwnsSubtreeHit()`是同一族机制：组件说一句"我要"，框架负责找路。
     * 场景层不认识"滚动容器"这个具体类型。
     *
     * 沿父链找那一步不在这个接口里（它要用树结构）。见`InkingWidget::FindSubtreeWheelOwner()`。
     */
    virtual bool HandlesSubtreeWheel() const noexcept { return false; }

    /** 指针进出：hover = true进入，false离开。转给 onPointerHover。 */
    void PushPointerHover(bool hover) noexcept { onPointerHover(hover); }

    /**
     * 按下 / 抬起：press = true 按下，false 抬起。转给 onPointerPress。
     *
     * 抬起与按下走同一条（而不是两个入口），因为"回哪一态 / 还算不算一次
     * 点击"是组件自己的知识：按钮按下滑出去再松开要回 Normal，不是 Hover。
     */
    void PushPointerPress(bool press) noexcept { onPointerPress(press); }

    /** 一次完整的点击（按下与抬起都发生在它身上）。转给 onPointerClick。 */
    void PushPointerClick() noexcept { onPointerClick(); }

    /**
     * 滚轮：转给 onPointerWheel。
     *
     * 归属：指针下最上面的那个静态组件；若这时正按着某个节点（拖动捕获生效中），改推给捕获者
     * 两者都是唯一目标，不会同时给两个组件。
     */
    void PushPointerWheel(float deltaX, float deltaY) noexcept {
        onPointerWheel(deltaX, deltaY);
    }

    /**
     * 拖动：转给 onPointerDrag。
     *
     * 只有捕获者收得到：按下时命中的那个节点抓住这次交互，之后指针怎么移
     * 都只推给它（跨组件不被抢，移出矩形也算它的）；抬起或窗口失焦时释放。
     * 这就是设计稿里说的 pointer capture，只不过落点从"路由层"换成了
     * 场景记住谁被按下。
     */
    void PushPointerDrag(float designX, float designY, float deltaX,
                         float deltaY) noexcept {
        onPointerDrag(designX, designY, deltaX, deltaY);
    }

protected:
    // ---------------- [√] 可重写钩子 ----------------
    //
    // 这五个必须在这里给空实现，不能只留声明：
    // 它们都是虚函数，而"没有重写它们"是正常情况
    // 绝大多数组件不关心指针。
    // 只声明不定义的话，基类 vtable 会引用一个不存在的符号，会造成任何派生类（哪怕一个钩子都不重写）在链接期拿到undefined reference to inking::InkingPointerHandler::onPointerWheel

    /**
     * 指针进出（由 PushPointerHover 转来）。默认什么都不做。
     *
     * 它只表示"场景认为指针在你身上"，不表示"你被按下了"。
     * 拖拽时按着不放、 指针滑出去，组件会先收到一次 `hover = false`，而它仍然按着。
     * 此情况正是 按钮"按下期间滑出去还是按下"这条手感的来源。
     */
    virtual void onPointerHover(bool /*hover*/) {}

    // 按下 / 抬起（由PushPointerPress 转来）。抬起也是这条（press = false）。
    virtual void onPointerPress(bool /*press*/) {}

    // 一次完整点击（由 PushPointerClick 转来）。默认什么都不做。
    virtual void onPointerClick() {}

    /**
     * 滚轮（由 `PushPointerWheel` 转来）。默认什么都不做。
     *
     * `deltaX` / `deltaY` 是本帧累积的滚动量（正 = 向右 / 远离用户），一帧多个滚轮事件已经合并成一次调用。
     * 要"滚动内容"就在这里累加自己的偏移量；要"缩放"就把 `deltaY` 当成幂次。具体含义由组件定。
     *与指针位置的区别：位置是跨帧状态（每帧都能问）
     * 滚轮是一次性事件（这一帧没收到就是没滚）
     * 所以它走钩子而不是"每帧读状态"
     */
    virtual void onPointerWheel(float /*deltaX*/, float /*deltaY*/) {}

    /**
     * 拖动（由 PushPointerDrag 转来）。默认什么都不做。
     *
     * 只会在本节点"按着"期间来（按下时它抓住了这次交互），并且是唯一收得到拖动事件的节点。指针移到别的组件上也不改归属。
     * 移出矩形仍继续收到，这正是拖拽该有的语义（滑块 / 分割条 / 框选）。
     *
     * `designX` / `designY` 是当前指针位置（设计坐标），`deltaX` / `deltaY`
     * 是相对上一次拖动事件（第一次是相对按下点）的位移，已经从窗口
     * 换算到设计坐标。
     * 组件直接加到自己身上即可，不必再乘倍率。
     */
    virtual void onPointerDrag(float /*designX*/, float /*designY*/,
                               float /*deltaX*/, float /*deltaY*/) {}
};

}  // namespace inking
