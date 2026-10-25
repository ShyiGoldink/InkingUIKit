#pragma once

// 指针（鼠标 / 触摸）输入契约：想收指针的组件重写这里的钩子。
//
// 它是 Widget 的一条可选能力，不是另一种节点。
// `InkingWidget` 与它多继承，所以任何 Widget 都天然有这个契约。
// 通过 重写 onPointerHover / onPointerPress / onPointerClick 就能收输入，
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
//
// ---------------------------------------------------------------------------
// 按钮维度（0.5.0）：两种输入状态，组件层看不见设备
// ---------------------------------------------------------------------------
// 0.5.0 起 `onPointerPress` / `onPointerClick` 带 `PointerButton` 参数。上层
// **永远只看到 Primary / Secondary**，不需要知道这是鼠标左键、右键，还是触摸的
// 轻点、长按——设备差异在窗口层与场景层就被吸收干净了（见 PointerButton.h 的
// 文件头）。所以这里没有"触摸分支"，也不该出现。
//
// 三个钩子的参数形状是**故意不同**的：
//
//     onPointerHover(bool)                  只有主键有 hover：hover 是"我指着谁"，
//                                           不是"我按着谁"，右键不该改变它
//     onPointerPress(PointerButton, bool)   按下不关心位置：要位置问场景即可
//     onPointerClick(PointerButton, x, y)   **必须**带坐标
//
// 为什么 click 带坐标而 press 不带，这两者不是同一类东西：
//
//   信号 / 转变（press、onPointerStateChanged）：回答"现在是什么状态"。之后还能
//   读，所以不该带参数——带了就是把持续状态抄第二份（这正是
//   `onPointerStateChanged` 那句"带坐标等于把约定抄一遍"的意思）。
//
//   事件（click、drag）：回答"发生过一次什么"。发生之后状态已经变了，不带走
//   那一刻的坐标就**永久丢失**。`onPointerDrag` 早就带坐标，click 只是跟上。
//
// 坐标是**按下点**而不是抬起点：触摸长按的锚点是按下处，手指在 slop 内微动几
// 像素不该让菜单跟着挪；鼠标右键按下与抬起通常同点，两者无差异。

#include <input/PointerButton.h>

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
     *
     * `button` 是**语义位**（Primary = 左键 / 轻点，Secondary = 右键 / 长按），
     * 不是设备位——组件不需要、也拿不到"这是鼠标还是手指"。
     */
    void PushPointerPress(PointerButton button, bool press) noexcept {
        onPointerPress(button, press);
    }

    /**
     * 一次完整的点击（按下与抬起都发生在它身上）。转给 onPointerClick。
     *
     * `designX` / `designY` 是**按下点**（设计坐标），不是抬起时的位置。
     * 理由见文件头"按钮维度"那段：click 是事件，坐标不带走就永久丢失；
     * 而锚点取按下点，长按后手指微动才不会让右键菜单跟着挪。
     */
    void PushPointerClick(PointerButton button, float designX,
                          float designY) noexcept {
        onPointerClick(button, designX, designY);
    }

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

    /**
     * 指针状态变了一次（位置 / 按下 / 抬起 / 进出窗口）。转给 onPointerStateChanged。
     *
     * **只对动态节点推**（0.4.1 T-02）。静态节点的三件事按结果推：
     * `PushPointerHover` / `PushPointerPress` / `PushPointerClick`——场景已经替它
     * 判完了"你在不在下面 / 按下算不算你的"。
     *
     * 动态节点不进命中表、场景也不替它判，它要的不是结果而是**时机**：
     * 一个"该重新算一次"的信号。采样点因此落在**指针变化的那一帧**，
     * 所以鼠标不动时一次都不推（判据是 `InkingScene::_pointerDirty`）。
     * 这一条取代了"每个逻辑步 Tick 里自己拉"：逻辑步是 50Hz 且与渲染帧
     * 不同拍，快速划过时会多判（白算）或晚判（最多差一个逻辑步）。
     */
    void PushPointerStateChanged() noexcept { onPointerStateChanged(); }

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
     *
     * **不带按钮参数**：只有主键有 hover（§1.2 不变量 1）。右键按下不改变
     * "我指着谁"——否则右键按住期间悬停会莫名其妙消失。
     */
    virtual void onPointerHover(bool /*hover*/) {}

    /**
     * 按下 / 抬起（由 PushPointerPress 转来）。抬起也是这条（press = false）。
     *
     * `button` 决定这次按下属于哪个键。**重写了就必须判它**：
     * 按钮只在 `PointerButton::Primary` 时进 Pressed 态，否则右键按下会让按钮
     * 亮起，用户会以为左键生效了。
     *
     * 不关心右键的组件写一行守卫即可：
     * `if (button != PointerButton::Primary) { return; }`
     */
    virtual void onPointerPress(PointerButton /*button*/, bool /*press*/) {}

    /**
     * 一次完整点击（由 PushPointerClick 转来）。默认什么都不做。
     *
     * `(designX, designY)` 是按下点（设计坐标）。最典型的用法是右键菜单：
     * 业务在 `PointerButton::Secondary` 那一支里直接 `OpenAt(designX, designY)`。
     *
     * 坐标不必取整、也不必再换算——它就是 `QueryHit` 用的那一套设计坐标。
     */
    virtual void onPointerClick(PointerButton /*button*/, float /*designX*/,
                                float /*designY*/) {}

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

    /**
     * 指针状态变了一次（由 `PushPointerStateChanged` 转来）。默认什么都不做。
     *
     * **只有动态节点会收到**：它是"该重新读一次状态了"的信号，而静态节点
     * 拿的是场景判好的结果（上面那五个钩子）。
     *
     * 不带参数是**故意的**（零状态原则）：要什么自己读——`GetScene()` 上的
     * `GetPointerX/Y` / `IsPointerDown` / `IsPointerInside`。带坐标就等于把
     * "哪个坐标系、哪一帧"这份约定又抄一遍，而场景已经存了一份。
     *
     * 频率：指针变化的那一帧一次（一帧内多次移动已合并成一次状态变化），
     * 鼠标不动时一次都不调。所以**它不适合做"按住持续移动"**——那种逻辑
     * 仍然读状态（`IsPointerDown`）在自己的 Tick 里跑。
     */
    virtual void onPointerStateChanged() {}
};

}  // namespace inking
