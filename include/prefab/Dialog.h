#pragma once

// 预制件「弹出对话框」（`docs/zh/plans/2026-11-03-uikit-completeness-tasks.md` T-06）。
//
// **它就是**：一层铺满的遮罩 + 一块居中的面板 + 三个事件槽（确认 / 取消 / 哪个按钮）。
//
// ---------------------------------------------------------------------------
// 为什么**不是** `class Dialog : public InkingFixedPopup`（任务书 §6 T-06 的写法）
//
// 两者的几何模型对不上，而"模态"恰恰是几何在干活：
//
//   - `FixedPopup` 是**点锚定**的：`OpenAt(x, y)` 把它的框摆在定点的某一侧，
//     适用的是右键菜单 / 下拉那一类"贴着指针展开"的东西；对话框是**居中**的，
//     没有定点；
//   - 模态要挡住的不是"对话框那块矩形"，而是**整块画布**——所以必须有一个
//     **铺满**的实体去命中。`FixedPopup` 的框就是它自己，铺满就等于"对话框是
//     全屏的"，那又和"居中面板"矛盾。
//
// 于是本类的做法：**自己就是那层全屏遮罩**（静态面板、几何 = 设计画布、
// `HitTest` 覆盖全屏），中间那块框是它的**子面板**。好处是三件事一起成立：
//
//   1. 命中：遮罩铺满 ⇒ 底下任何组件都点不到（模态，构造上成立）；
//   2. 绘制：父先画、子后画 ⇒ 遮罩在下、面板在上，不需要额外抬 z 的两个节点；
//   3. 可见性：`SetVisible(false)` 之后整棵子树既不画也不命中（`IsVisibleInTree`），
//      所以"关掉就完全不存在"不用另写一条规则。
//
// `InkingFixedPopup` 那两条默认值（点外部关闭 / 打开夺焦点）恰好是对话框要**反**
// 过来的：遮罩点击默认**不关**（防误触丢数据），焦点要夺（但由本类自己管）。
//
// ---------------------------------------------------------------------------
// 三个槽（§2.5.1）
//
//   `accepted`  确认（Enter / `Accept()`）
//   `cancelled` 取消（Esc / `Cancel()`）
//   `action`    确认时那一个"默认动作"的索引——给"要区分点了哪个按钮"的用法
//
// 确认时 `accepted` 与 `action` **都发**（前者给"只要知道成了"，后者给"要区分"）。
// **不给 `SetOnResult` 便利写法**，理由同 §2.5.1。
//
// 事件的派发顺序固定为：**先关（`IsOpen()` 已是假）→ 再发事件**。于是订阅者
// 在回调里读到的是"已经关掉"的一致状态，想在回调里再开一次也不会打架。

#include <panel/InkingStaticPanel.h>
#include <event/InkingEvent.h>
#include <button/ButtonData.h>

#include <memory>
#include <string>

// 只前向声明：`onKeyDown` 的声明只要类型名（同 `prefab/MenuPopup.h`）。
struct InkingKeyEvent;

namespace inking_prefab {

/// 对话框的外观与尺寸。
struct DialogData {
    std::string name = "dialog";

    // ---- 中间那块面板 ----
    int width = 420;
    int height = 240;
    float radius = 16.0f;
    InkingButtonAppearance panel{};

    // ---- 遮罩 ----
    /**
     * 遮罩色。默认 `0x66000000`（四成黑）。
     *
     * **不要写全透明**：`InkingPanelData::Normalize()` 会把"两态 alpha 全 0"兜底成
     * 不透明黑（`AGENTS.md` §6 第 52 条）——那是给"漏配看得见"用的，拿来当遮罩
     * 会直接挡住整个界面。
     */
    InkingButtonAppearance scrim{};

    static DialogData MakeDefault();
};

/**
 * @brief 模态对话框。
 *
 * ```cpp
 * inking_prefab::Dialog dialog(&scene);
 * dialog.AddItem(std::make_unique<InkingStaticLabel>(nullptr, caption));
 * dialog.accepted.Subscribe([]{ Save(); });
 * dialog.cancelled.Subscribe([]{ Abort(); });
 * dialog.Open();     // 遮罩 + 面板出现，底下点不到
 * ```
 *
 * 内容用 `AddItem` 挂（任意 `InkingWidget`），位置由调用方在自己的数据里定
 * ——本类不替你排版（`AGENTS.md` §3 第 10 条：组件不猜业务布局）。
 */
class Dialog : public InkingStaticPanel {
public:
    /// 确认（Enter 或 `Accept()`）。**已经关掉**之后才发。
    InkingEvent<> accepted;
    /// 取消（Esc 或 `Cancel()`）。**已经关掉**之后才发。
    InkingEvent<> cancelled;
    /// 确认时的"默认动作"索引（`SetDefaultAction` 指定的那个）。与 `accepted` 同时发。
    InkingEvent<int> action;

    explicit Dialog(InkingWidget* parent,
                    const DialogData& data = DialogData::MakeDefault());

    /// 显示（幂等）。会夺取键盘焦点（Esc / Enter 才有地方落）。
    void Open();
    /// 隐藏（幂等）。**不发任何事件**——程序关掉不算"用户取消"。
    void Close();

    /// 走"确认"这条路：关 → 发 `accepted` → 发 `action`。没开着时返回 false。
    bool Accept();
    /// 走"取消"这条路：关 → 发 `cancelled`。没开着时返回 false。
    bool Cancel();

    bool IsOpen() const noexcept { return _open; }

    /// 往**中间那块面板**里挂一个由它拥有的子项。
    InkingWidget* AddItem(std::unique_ptr<InkingWidget> item);

    /// Enter 时 `action` 带出去的那个索引（默认 0）。本类不解释它。
    void SetDefaultAction(int index) noexcept { _defaultAction = index; }
    int GetDefaultAction() const noexcept { return _defaultAction; }

    /// 点遮罩是否关闭（默认 **false**：防误触丢数据）。开着的语义 = 点遮罩取消。
    void SetCloseOnScrimClick(bool on) noexcept { _closeOnScrim = on; }
    bool IsCloseOnScrimClick() const noexcept { return _closeOnScrim; }

    const DialogData& GetData() const noexcept { return _data; }

    /// 中间那块面板节点（要单独设布局 / 加子项时用；多数场合用 `AddItem` 就够）。
    InkingStaticPanel* GetPanelNode() const noexcept { return _box; }

    /// 打开时参与键盘（Esc / Enter）。
    bool WantsKeyboardFocus() const noexcept override { return _open; }

protected:
    void onPointerClick(InkingPointerButton button, float designX,
                        float designY) override;
    void onKeyDown(const InkingKeyEvent& key) override;

    /**
     * @brief 每渲染帧把键盘焦点**收回子树**（模态的一部分）。
     *
     * 为什么需要：点一下对话框本体（面板 / 内容）会把焦点清掉——场景的规则是
     * "点到的组件不想要焦点就别留着"（`InkingScene::SetKeyboardFocus` 的注释）。
     * 焦点一没，Esc / Enter 就没有落点，而"模态"在用户看来就是**键盘也归它**。
     *
     * 只做"补"不做"抢"：焦点已经在本子树里（例如点进了内容里的输入框）就一个
     * 字节都不动——否则打字会被每帧抢回来。
     */
    void onAnimationTick(float deltaSeconds) override;

private:
    /// 设计坐标落在中间那块面板里吗（落在里面就不算"点遮罩"）。
    bool isInsidePanel(float designX, float designY) const;

    /// `node` 在本子树里吗（含自己）。
    bool isInSubtree(const InkingWidget* node) const noexcept;

    DialogData _data{};

    /// 中间那块面板（由本节点拥有，`AddChild` 挂上来）。
    InkingStaticPanel* _box = nullptr;

    int _defaultAction = 0;
    bool _closeOnScrim = false;
    bool _open = false;
};

}  // namespace inking_prefab
