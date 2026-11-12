// 拖拽会话的实现：**唯一一份**阈值 / 落点 / 收尾的判定。
//
// 两个"为什么在这里"值得先说清楚，免得后来者把它搬走：
//
//   1. **阈值与落点判定要问场景**（`QueryHit`），而命中只有场景知道。
//      放控件里就得每个组件自己查一遍表——那正是这次要消除的重复。
//   2. **收尾只有一个出口**（`end`）。四条取消路径 + 落靶全走它：
//      各写一份必出"Esc 那条忘了删幽灵"这类只在一半路径上错的问题。
//
// 判据的取法（照着 `docs/zh/input` 那一族的老规矩）：**只比指针、不解引用**。
// 会话里存的全是裸指针，源控件完全可能在拖拽途中被删掉（A6 断言钉的就是它）。

#include <drag/InkingDragSession.h>

#include <scene/InkingScene.h>
#include <widget/InkingWidget.h>

#include <cstring>

namespace {

/// 沿父链找**最近的、声明了拖拽源的祖先**（含自己）。
///
/// 为什么要沿父链：按下的常常是行里的标签 / 图标，而"这一行能被拖"是行的语义。
/// 与 `HandlesSubtreeWheel` / `OwnsSubtreeHit` 同一条"组件说一句我要、框架负责
/// 找路"的分工——**场景不认识"列表行"这个具体类型**。
InkingWidget* FindDragSourceAncestor(InkingWidget* hit, float x, float y) {
    for (InkingWidget* node = hit; node != nullptr; node = node->GetParent()) {
        const InkingDragSourceSpec& spec = node->GetDragSource();
        if (spec.payloadType == nullptr || spec.payloadType[0] == '\0') {
            continue;  // 没声明，往上找
        }
        // `canBegin` 是"这次按下不能起拖"的唯一表达（例如按在行内的重命名框里）。
        if (spec.canBegin != nullptr && !spec.canBegin(spec.user, x, y)) {
            return nullptr;  // 最近的声明者说不行 ⇒ 就是不行，不往上找
        }
        return node;
    }
    return nullptr;
}

}  // namespace

// ---------------------------------------------------------------------------
// 按下：只记候选，不建幽灵
// ---------------------------------------------------------------------------

void InkingDragSession::notePress(InkingScene& /*scene*/, InkingWidget* hit,
                                  float x, float y) {
    clearCandidate();
    // 已经有一场拖拽在跑：这一次按下**不响应**（A8，一次只允许一个源）。
    if (IsDragging()) {
        return;
    }
    InkingWidget* source = FindDragSourceAncestor(hit, x, y);
    if (source == nullptr) {
        return;
    }
    _pressNode = source;
    _pressX = x;
    _pressY = y;
}

// ---------------------------------------------------------------------------
// 每帧：逾阈值 / 幽灵跟手 / 落点高亮
// ---------------------------------------------------------------------------

void InkingDragSession::update(InkingScene& scene, bool inside, float x, float y,
                               bool primaryDown) {
    if (!_started) {
        if (_pressNode == nullptr) {
            return;
        }
        if (!primaryDown) {
            // 松手了还没逾阈值：这就是一次普通点击，候选到此为止
            // （click 由场景照常派发，A1 断言的正是这一条）。
            clearCandidate();
            return;
        }
        const float dx = x - _pressX;
        const float dy = y - _pressY;
        if (dx * dx + dy * dy
            < InkingDragStartThreshold * InkingDragStartThreshold) {
            return;  // 还在阈值内：宁可当点击，也不让按钮点不下去
        }
        // **先记源再清候选**：`begin` 内部会 `clearCandidate()`，而它要读源。
        InkingWidget* source = _pressNode;
        if (!begin(scene, source)) {
            clearCandidate();  // 源不可拖（被隐藏 / 已被摘掉）⇒ 这次手势作废
            return;
        }
    }

    if (!inside) {
        // 指针出了窗口：幽灵留在原地（人要能看见自己拖走了它），但**落点清空**
        // ——窗口外没有"落点"这回事，留着上一帧那个会让高亮停在屏幕里骗人。
        if (_target != nullptr) {
            _target->MarkDragOver(false);
            _target = nullptr;
        }
        return;
    }
    inking_detail::MoveDragGhost(_ghost, x, y);
    updateTarget(scene, x, y);
}

// ---------------------------------------------------------------------------
// 起拖
// ---------------------------------------------------------------------------

bool InkingDragSession::begin(InkingScene& scene, InkingWidget* source) {
    // 一次只允许一个（A8）：多选批量拖是任务书 §4 明确不做的。
    if (source == nullptr || _started) {
        return false;
    }
    const InkingDragSourceSpec& spec = source->GetDragSource();
    if (spec.payloadType == nullptr || spec.payloadType[0] == '\0') {
        return false;
    }
    // 跨场景的拖拽不成立：幽灵挂在**本场景**上，落点也按本场景的命中查。
    // （"跨窗口 / 跨进程"整类都在不做清单里。）
    if (source->GetScene() != &scene) {
        return false;
    }
    // 隐藏的节点不能拖：它连命中都进不去，起拖的手势根本无从发生。
    if (!source->IsVisibleInTree()) {
        return false;
    }

    int width = spec.ghostWidth > 0 ? spec.ghostWidth : source->GetWidth();
    int height = spec.ghostHeight > 0 ? spec.ghostHeight : source->GetHeight();
    InkingWidget* ghost = inking_detail::CreateDragGhost(scene, spec, width, height);
    if (ghost == nullptr) {
        return false;
    }

    _source = source;
    _payloadType = spec.payloadType;
    _payload = spec.payload;
    _ghost = ghost;
    _started = true;
    clearCandidate();
    return true;
}

// ---------------------------------------------------------------------------
// 落点解析
// ---------------------------------------------------------------------------

void InkingDragSession::updateTarget(InkingScene& scene, float x, float y) {
    InkingWidget* found = nullptr;

    const InkingHitResult hit = scene.QueryHit(x, y);
    for (InkingWidget* node = hit.IsHit() ? hit.target : nullptr;
         node != nullptr; node = node->GetParent()) {
        const InkingDropTargetSpec& spec = node->GetDropTarget();
        if (spec.acceptType == nullptr || spec.acceptType[0] == '\0') {
            continue;  // 这层没声明落点，继续往上找
        }
        if (_payloadType == nullptr
            || std::strcmp(spec.acceptType, _payloadType) != 0) {
            // 声明了、但类型不符：**跳过它继续往上**。于是"行只收图片、外层列表
            // 收一切"这种配置自然成立（任务书 §2 只要求类型相符才可落）。
            continue;
        }
        if (spec.canDrop != nullptr && spec.canDrop(spec.user, _payload, x, y) == 0) {
            // 类型对、但它说现在不行 ⇒ **就这里不行**，不再往上找：往上找等于
            // "在拒绝我的行头上松开，落进了整个列表"——语义上更糟。
            break;
        }
        found = node;
        break;
    }

    if (found == _target) {
        return;
    }
    // 高亮只是**状态**：外观由控件自己在 `onRender` 里问 `IsDragOver()` 画。
    // `MarkDragOver` 内部标重绘脏（不是命中脏——命中一点没变）。
    if (_target != nullptr) {
        _target->MarkDragOver(false);
    }
    if (found != nullptr) {
        found->MarkDragOver(true);
    }
    _target = found;
}

// ---------------------------------------------------------------------------
// 松手：落靶 or 取消
// ---------------------------------------------------------------------------

bool InkingDragSession::finishOnRelease(InkingScene& scene, bool released,
                                       bool inside, float x, float y) {
    if (!_started) {
        // 候选还没起拖：这次松手是普通点击，**不归拖拽管**（A1）。
        clearCandidate();
        return false;
    }
    if (!released && inside) {
        return false;  // 既没松手、也没出窗口：还在拖
    }

    const bool dropping = released && inside;
    if (dropping) {
        // 落点以**松手那一刻**为准：指针从落点上划过再松开，不该落进上一次
        // 算出来的那个地方（`update` 每帧都算，但这一帧可能还没跑到）。
        updateTarget(scene, x, y);
    }

    InkingDropTargetSpec dropSpec{};
    bool dropped = false;
    if (dropping && _target != nullptr) {
        dropSpec = _target->GetDropTarget();
        dropped = dropSpec.onDrop != nullptr;
    }
    void* payload = _payload;

    // **先收尾、再回调**：`onDrop` 里删节点、重建整张列表、连 payload 一起释放
    // 都是允许的（`DragSpec.h` 的 `onDrop` 注释里写着）。反过来先回调就必然
    // 出现"回调里删了源控件，收尾再去碰它"。
    end(scene, dropped);
    if (dropped) {
        dropSpec.onDrop(dropSpec.user, payload, x, y);
    }
    return true;  // 这一次松手属于拖拽：场景不能再派发 click
}

// ---------------------------------------------------------------------------
// 取消与收尾
// ---------------------------------------------------------------------------

bool InkingDragSession::cancel(InkingScene& scene) {
    if (!_started) {
        if (_pressNode == nullptr) {
            return false;
        }
        clearCandidate();  // 候选取消：没有幽灵，也没有"取消了一场拖拽"这回事
        return true;
    }
    end(scene, false);
    return true;
}

void InkingDragSession::onNodeRemoved(InkingScene& scene, InkingWidget* node) {
    if (node == nullptr) {
        return;
    }
    // 源没了 ⇒ 整场拖拽作废（A6：不崩、幽灵消失、状态清零）。
    if (node == _source) {
        end(scene, false);
        return;
    }
    // 落点没了 ⇒ 只清高亮，拖拽继续（`update` 下一帧会重新算落点）。
    if (node == _target) {
        _target = nullptr;
        return;
    }
    // 候选没了 ⇒ 候选作废（还没起拖，没有幽灵要删）。
    if (node == _pressNode) {
        clearCandidate();
    }
}

void InkingDragSession::shutdown(InkingScene& scene) {
    if (_started) {
        end(scene, false);
    }
    // 候选与残留一律清空：场景要没了，任何指向子树的指针都不该留着。
    clearCandidate();
    if (_target != nullptr) {
        _target->MarkDragOver(false);
        _target = nullptr;
    }
    clearPayload();
    _ghost = nullptr;
    _started = false;
}

void InkingDragSession::end(InkingScene& scene, bool dropped) {
    // ① 高亮先清：它只改外观，最安全，也最不该被漏。
    if (_target != nullptr) {
        _target->MarkDragOver(false);
        _target = nullptr;
    }

    // ② 幽灵：**先摘指针再删节点**。`RemoveChild` 会走
    //    `InkingScene::RemoveDrawNode`（幽灵的析构体里那条反注册），那时本会话
    //    已经是"没了"的状态，不会被当成"源被移除"再取消一次。
    InkingWidget* ghost = _ghost;
    _ghost = nullptr;
    clearPayload();
    clearCandidate();
    _started = false;

    if (dropped) {
        ++_dropCount;
    } else {
        ++_cancelCount;
    }

    if (ghost != nullptr) {
        // 不接返回值 = 当场析构（幽灵归场景所有，`RemoveChild` 把所有权交回来）。
        scene.RemoveChild(ghost);
    }
}

void InkingDragSession::clearCandidate() noexcept {
    _pressNode = nullptr;
    _pressX = 0.0f;
    _pressY = 0.0f;
}

void InkingDragSession::clearPayload() noexcept {
    _source = nullptr;
    _payloadType = nullptr;
    _payload = nullptr;
}
