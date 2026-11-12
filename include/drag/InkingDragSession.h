#pragma once

// 拖拽会话：**一个场景一份**的状态机，回答"这一帧谁在拖、拖到哪儿了"。
//
// ---------------------------------------------------------------------------
// 它为什么在场景上，而不是在控件上
// ---------------------------------------------------------------------------
// 拖拽天生是**跨组件**的：源在 A、落点在 B、幽灵在两者之上。控件之间没有
// 共同的父级好放这份状态（A 和 B 可能是两棵子树），而"一次只允许一个拖拽"
// 也是**场景级**的唯一性。所以会话归场景，和"谁被按下（捕获）"、"谁有键盘
// 焦点"同一档——那两条也都在场景上。
//
// ---------------------------------------------------------------------------
// 四条取消路径，一处收尾
// ---------------------------------------------------------------------------
//   1. Esc（`DispatchKeyboard` 里先问会话，再轮到焦点节点）；
//   2. 松手在无落点处（指针下没有类型匹配的落点，或 `canDrop` 说不）；
//   3. 源控件被移除（`InkingScene::RemoveDrawNode` 里清）；
//   4. 调用方显式 `InkingScene::CancelDrag()`。
//
// 四条都只做一件事：`end()`——清高亮、删幽灵、清状态。各写一份的话，必然
// 出现"Esc 那条忘了删幽灵"这类只在一半路径上错的问题（本仓踩过的通用教训：
// 收尾只能有一个出口）。
//
// 交互规则五条（阈值 / 独占 / z / 取消 / 单实例）写在 `DragSpec.h` 与本文件
// 的注释里；它们是**手感**，改之前先看任务书 `docs/zh/plans/` 里那份拖拽规格。
//
// 实现见 `src/drag/InkingDragSession.cpp` 与 `src/drag/InkingDragGhost.cpp`。
//
// ---------------------------------------------------------------------------
// 生命周期（一条必须知道的）
// ---------------------------------------------------------------------------
// 本对象是 `InkingScene` 的**成员**，所以它比基类 `InkingWidget` 先析构，
// 而节点析构时会回头调 `InkingScene::RemoveDrawNode`。场景析构体里先
// `shutdown()` 把会话掐掉（顺手摘掉幽灵），之后那一路才走不到这里——
// 顺序在 `~InkingScene` 里写着，动它之前先读那段注释。

#include <drag/DragSpec.h>

#include <cstdint>

class InkingScene;
class InkingWidget;

/**
 * 一次拖拽会话的状态。
 *
 * **只有 `InkingScene` 驱动它**（friend）。业务读的是场景上的那几个门面：
 * `IsDragging()` / `GetDragSourceNode()` / `GetDragTargetNode()` / `CancelDrag()`。
 * 自检除外——探针要看"幽灵到底挂在哪、还在不在"，那几条也只读。
 */
class InkingDragSession {
public:
    InkingDragSession() = default;

    // 禁拷贝 / 移动：它存的是"这次交互"的身份，第二份就是第二场拖拽。
    InkingDragSession(const InkingDragSession&) = delete;
    InkingDragSession& operator=(const InkingDragSession&) = delete;
    InkingDragSession(InkingDragSession&&) = delete;
    InkingDragSession& operator=(InkingDragSession&&) = delete;

    /// 真的拖起来了吗（已逾阈值、幽灵在）。**按下还没动**的那一段不算。
    bool IsDragging() const noexcept { return _started && _source != nullptr; }

    /// 有候选吗（按下了、还没逾阈值）。业务一般不用，自检判"阈值那段"用得上。
    bool HasCandidate() const noexcept { return _pressNode != nullptr; }

    /// 有没有会话可取消（候选也算——候选被取消时不会起拖）。
    bool HasSession() const noexcept { return _started || _pressNode != nullptr; }

    /// 正在被拖的源控件（**非拥有**）；没在拖时为 nullptr。
    InkingWidget* GetSource() const noexcept { return _source; }

    /// 当前高亮的落点；指针底下没有可落的类型匹配节点时为 nullptr。
    InkingWidget* GetTarget() const noexcept { return _target; }

    /// 幽灵节点（**场景拥有**，随会话生灭）；没在拖时为 nullptr。
    ///
    /// 暴露它只为一件事：自检要断言"幽灵挂在场景下、z = 850"。业务不该拿它
    /// 做任何事——它不是给你的控件树用的。
    InkingWidget* GetGhost() const noexcept { return _ghost; }

    /// 这次拖的是什么类型 / 什么 payload（原样；归调用方）。
    const char* GetPayloadType() const noexcept { return _payloadType; }
    void* GetPayload() const noexcept { return _payload; }

    /// 成功落靶 / 被取消各几次（观测口，自检与诊断用）。
    std::uint64_t GetDropCount() const noexcept { return _dropCount; }
    std::uint64_t GetCancelCount() const noexcept { return _cancelCount; }

private:
    friend class InkingScene;

    /**
     * 按下：记一个**候选**（还没起拖）。
     *
     * `hit` 是按下那一刻命中的节点；沿父链找第一个声明了拖拽源的祖先。
     * 找不到 / `canBegin` 说不 ⇒ 什么都不记（这次按下就是普通点击）。
     */
    void notePress(InkingScene& scene, InkingWidget* hit, float x, float y);

    /**
     * 每帧一次（`DispatchPointer` 里，捕获更新之后）：逾阈值就起拖，
     * 起拖之后让幽灵跟手、重算落点。
     *
     * `primaryDown` 为假时什么都不做——松手由 `finishOnRelease` 收尾。
     */
    void update(InkingScene& scene, bool inside, float x, float y,
                bool primaryDown);

    /**
     * 松手（或指针离开窗口）：落靶就调 `onDrop`，否则取消。
     *
     * @return 这次松手**属于拖拽**（调用方不能再派发 click）。候选还没逾阈值时
     *         返回 false——那就是普通点击，A1 断言钉的正是这条。
     */
    bool finishOnRelease(InkingScene& scene, bool released, bool inside, float x,
                         float y);

    /// 程序化起拖（`InkingScene::BeginDrag`）。已在拖 / 不可拖 ⇒ false。
    bool begin(InkingScene& scene, InkingWidget* source);

    /// 显式取消。没有会话 ⇒ false。
    bool cancel(InkingScene& scene);

    /// 有节点离开场景（`InkingScene::RemoveDrawNode`）：是源就是取消。
    ///
    /// **只比指针、不解引用**——调用方可能正在析构中途。
    void onNodeRemoved(InkingScene& scene, InkingWidget* node);

    /// 场景析构前掐掉会话（摘幽灵、清状态）。
    void shutdown(InkingScene& scene);

    /// 唯一收尾：清高亮 → 删幽灵 → 清状态。四条取消路径 + 落靶都走这里。
    void end(InkingScene& scene, bool dropped);

    /// 按指针重算落点并同步高亮。类型匹配的最近祖先才算落点。
    void updateTarget(InkingScene& scene, float x, float y);

    /// 把幽灵挪到"指针 + 一点偏移"处（夹在场景矩形里，别飘出窗外）。
    void moveGhostTo(float x, float y);

    /// 清候选（不影响已起的会话）。
    void clearCandidate() noexcept;

    /// 清 payload 与源（**不碰幽灵**：幽灵由 `end` 摘）。
    void clearPayload() noexcept;

    // ---------------- 状态 ----------------
    //
    // 全是裸指针 + 平凡类型：本对象要能安全地"析构之后只剩一堆字节"（见文件头
    // 那段生命周期说明）。所以这里**不许**出现 owning 指针或非平凡析构的东西。

    InkingWidget* _pressNode = nullptr;  ///< 候选：按下时命中的那个（可能是源的子孙）
    InkingWidget* _source = nullptr;     ///< 真的开始拖之后的源控件
    InkingWidget* _ghost = nullptr;      ///< 幽灵（场景拥有）
    InkingWidget* _target = nullptr;     ///< 当前高亮的落点

    const char* _payloadType = nullptr;  ///< 类型标签（归调用方）
    void* _payload = nullptr;            ///< payload（归调用方；库不碰）

    float _pressX = 0.0f;  ///< 按下点（设计坐标），阈值从这里算
    float _pressY = 0.0f;

    bool _started = false;  ///< 已逾阈值、幽灵已建

    std::uint64_t _dropCount = 0;
    std::uint64_t _cancelCount = 0;
};

namespace inking_detail {

/**
 * 造一只幽灵挂在场景上（`zIndex = InkingDragGhostZIndex`、恒不参与命中）。
 *
 * 返回的是**场景拥有**的节点（`InkingScene::AddChild`），调用方只记指针、
 * 不负责释放。宽度 / 高度 <= 0 时按文案量一次（量不出来就用 1×1，
 * 至少不会因为 0 尺寸被判成"不存在"）。
 *
 * 只给 `InkingDragSession` 用；造在这个头里而不是 `src/` 里，是因为会话的实现
 * 也是 .cpp，两处要认同一个返回类型。
 */
InkingWidget* CreateDragGhost(InkingScene& scene,
                             const InkingDragSourceSpec& spec, int width,
                             int height);

/**
 * 把幽灵挪到"指针 + 错位"处（夹在设计画布内）。
 *
 * 与 `CreateDragGhost` 一起放在这里只为一件事：**类型转换只许发生在
 * `InkingDragGhost.cpp` 里那一处**。会话拿的是 `InkingWidget*`，它不需要、
 * 也不该知道幽灵的具体类型（那是"谁造的谁认识"）。
 */
void MoveDragGhost(InkingWidget* ghost, float pointerX, float pointerY);

}  // namespace inking_detail
