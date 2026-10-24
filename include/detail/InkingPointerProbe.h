#pragma once

// 动态组件的指针边沿判定：hover 进出 / 按下捕获 / 抬起算不算一次点击。
//
// ---------------------------------------------------------------------------
// 为什么单拎出来（0.4.1 T-03）
//
//   静态档这份"历史状态"住在场景里（`_pressedNode` / `_pointerWasDown`），动态档
//   没人替它记，所以每个动态组件都得自己存 `_wasDown` / `_captured`。
//   `InkingDynamicButton` 已经存了一份；定点浮窗要判"点了外部就关"就得抄第二份，
//   第三份（列表行 / 树节点）必然走形——`InkingButtonLike` 那次拆分就是这么来的。
//
//   **它不是 `InkingButtonLike` 的替代**：那个类是三态专用（外观 / 过渡 / 事件槽），
//   它吃的是"hover / press 两个 bool"。本类负责把指针状态算成这两个 bool。
//   分工一句话：本类产结论，`InkingButtonLike` 吃结论。
//
// ---------------------------------------------------------------------------
// 三条语义与静态档保持一致（改之前先读 `InkingScene::DispatchPointer` 的 2、3 段）
//
//   1. 按下之后指针滑出去**仍然算按着**（捕获）；抬起时不在身上就**不算点击**——
//      这是用户唯一能表达"我反悔了"的方式；
//   2. 在别处按下、按着挪进来，不算这次按下（那一趟是别的组件抓住的）；
//   3. 指针不在窗口里时一律不算命中：调用方算 `hit` 时把 `IsPointerInside()`
//      折进去（本类不知道窗口边界）。
//
// 调用时机由场景决定：`onPointerStateChanged()`（指针真的变了的那一帧）。
// **不要每帧无条件调**——那正是 0.4.1 之前"逻辑步里拉一遍"的老做法。

namespace inking {
namespace detail {

/// 一次判定的结果。全 false 表示"指针没变、也没发生边沿"。
struct PointerProbeResult {
    bool hoverChanged = false;  ///< 悬停进出变了（`hovered` 是新值）
    bool hovered = false;       ///< 这次判定后指针在本节点上
    bool downEdge = false;      ///< **发生了**一次按下（不管在谁身上）
    bool pressed = false;       ///< 按下发生在本节点上（捕获开始）
    bool released = false;      ///< 捕获结束（这一下抬起结束了本节点的捕获）
    bool clicked = false;       ///< 按下与抬起都落在本节点上
};

/**
 * @brief 动态组件的边沿判定器（无渲染、无场景依赖）。
 *
 * ```cpp
 * // 在 onPointerStateChanged() 里：
 * const auto* scene = GetScene();
 * const bool hit = scene->IsPointerInside()
 *               && HitTest(scene->GetPointerX(), scene->GetPointerY());
 * const auto r = _probe.Update(hit, scene->IsPointerDown());
 * if (r.clicked) { ... }
 * ```
 */
class PointerProbe {
public:
    /**
     * 判一次。`hit` = 指针当前在不在本节点上；`down` = 左键按着没有。
     *
     * 幂等：同一份输入连着调两次，第二次只会在 `hoverChanged` / `pressed`
     * 这类"边沿"上什么都不报（这正是它取代"每帧无条件判"的前提）。
     */
    PointerProbeResult Update(bool hit, bool down) noexcept {
        PointerProbeResult result;

        result.hovered = hit;
        result.hoverChanged = (hit != _hovered);
        _hovered = hit;

        const bool justPressed = down && !_wasDown;
        const bool justReleased = !down && _wasDown;

        // "按下发生了"与"按下算在我身上"是两件事：前者给"点外部就关"这类
        // 组件用（它在外面被按下时也要知道），后者才是捕获。
        result.downEdge = justPressed;

        if (justPressed) {
            // 按下那一刻指针在不在它身上，决定这次按下算不算它的。
            // 在别处按下、按着挪进来的，不算（那一趟是别的组件抓住的）。
            _captured = hit;
            result.pressed = hit;
        }
        if (justReleased) {
            // **先算 clicked 再清 captured**：反过来的话点击永远不成立。
            result.released = _captured;
            result.clicked = _captured && hit;
            _captured = false;
        }

        _wasDown = down;
        return result;
    }

    /**
     * 捕获作废（指针离开窗口 / 节点离开场景 / 浮窗关闭）。
     *
     * 与 `Update` 的区别：它不产生任何事件，只把状态退回初始——用于
     * "这一趟交互不该再算了"的场合，避免下次进来时带着一份过期的按着状态。
     */
    void Reset() noexcept {
        _hovered = false;
        _wasDown = false;
        _captured = false;
    }

    /// 上次判定里指针在本节点上吗。
    bool IsHovered() const noexcept { return _hovered; }

    /// 上次判定里"这次按下算在本节点上"吗（按下之后滑出去仍为 true）。
    bool IsCaptured() const noexcept { return _captured; }

private:
    bool _hovered = false;
    bool _wasDown = false;
    bool _captured = false;
};

}  // namespace detail
}  // namespace inking
