#pragma once

// 块叠加组的**静态档**（0.9.0 T-05）：一个 sprite 由多块拼成。
//
// ---------------------------------------------------------------------------
// 它跟 `InkingAnimateGroup` 不是一件事（T-02 刚把名字对调过来）
//
//   - `InkingAnimateGroup`：**一个角色的多个动作**，一帧**一块**，切动作换配置。
//   - `InkingSpriteGroup`（本类）：**一个 sprite 由多块拼成**，一帧**K 块**，
//     全部来自同一张图集的等格，切动作只换格号表。
//
// 两者的共同点只有"一个节点"，其余都不同——所以是并列的两个类，不是一个的两档。
//
// ---------------------------------------------------------------------------
// 为什么派生 `InkingStaticImage` + `InkingSpriteLike`，而不是派生 `InkingSprite`
//
// 任务书 T-05 的三条路里选 B（见 `plans/2026-11-04-sprite-group-and-animate-group.md`
// §6）。派生 `InkingSprite` 表面上"白拿帧机"，实际要在构造期编一份**假配置**去
// 骗过 `InkingSpriteData::Validate()`，再覆盖帧表——那是"为了绕过校验而编数据"，
// 正是 `AGENTS.md` 第 30 条最忌的东西。
//
// 所以本类与 `InkingSprite` **平级**：吃 `InkingStaticImage`（图片那套：字节、
// 外观、命中、拖拽）加 `InkingSpriteLike`（帧机：时间 → 帧号、事件、状态机）。
// 代价是复刻约 30 行接线（`hooksFor` / `onAnimationTick`），换掉的是两份配置
// 互相骗来骗去。
//
// ---------------------------------------------------------------------------
// 一帧 K 块，**一次提交**
//
// `onRender` 对每块调一次 `inking_detail::PaintImage`（同一张纹理、不同的格矩形）。
// 提交层按**纹理**分批（`AppendTexturedGeometry`），同一张纹理连续 N 笔合成
// **一次** `SDL_RenderGeometry`——所以"K 块"不产生 K 次 drawcall。这条由
// `BatchStats::textureDraws` 的差值断言钉住（T-05 的验收 ③，T-07 扩进
// `CheckSpriteBatching`）。
//
// 反过来说：两块的图**来自不同图集**时不会合并（跨纹理本来就刷批）——本类只吃
// 一张图集，那种情况配不出来。

#include <event/InkingEvent.h>
#include <image/InkingStaticImage.h>
#include <image/SpriteGroupData.h>
#include <image/SpriteLike.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class InkingWidget;

/**
 * @brief 块叠加组：**同一个节点**上，一帧画 K 块（同图集、等格）。
 *
 * 构造时的 `initialAnimation` 决定先播哪个（空 = 第 0 个；名字不存在 → 记一条
 * ERROR 并退回第 0 个——"少一个动作"不该让整个界面起不来）。
 */
class InkingSpriteGroup : public InkingStaticImage, public InkingSpriteLike {
public:
    InkingSpriteGroup(InkingWidget* parent, const InkingSpriteGroupData& data,
                      const std::string& initialAnimation = std::string());

    /**
     * 换过动作了（新动作名, 旧动作名）。
     *
     * - 在帧表**换完之后**发：回调里 `GetFrameCount()` 已经是新动作的；
     * - 构造时**不发**；切到**当前**动作也不发（那是空操作）；
     * - **旧名字是临时量**，只在回调期间有效（新名字指向本类的成员）——与
     *   `InkingAnimateGroup::animationChanged` 同一条约定。
     */
    InkingEvent<const std::string&, const std::string&> animationChanged;

    /// 帧号变了（新帧号, 旧帧号）。构造时 / 切动作后的第一次落帧**不发**。
    InkingEvent<std::size_t, std::size_t> frameChanged;

    /// 播到某个**有事件的帧**了（事件名, 帧号）。语义与 `InkingSprite` 逐条相同
    /// ——同一份 `fireEventsBetween`（快照派发、区间扫描、回绕拆段，T-01 修过）。
    InkingEvent<const std::string&, std::size_t> frameEvent;

    /// 非循环的动画播完了（播完再 `Play()` 从头再来）。
    InkingEvent<> finished;

    /**
     * @brief 切到某个动作。
     *
     * @param name      动作名（`GetAnimationNames()` 里的一个）。
     * @param keepPhase `true` = 保留当前帧号（同动作换方向）；默认从第 0 帧起。
     * @return 动作存在才返回 true。**不存在时返回 false + 一条 ERROR**——不静默
     *         停在旧动作上（"切了没反应"看起来像动画坏了）。
     *
     * 切到**当前**动作是空操作（返回 true、不重建、不发事件）。要重置相位用
     * `SeekToFrame(0)`。
     */
    bool SwitchTo(const std::string& name, bool keepPhase = false);

    /// 当前动作名（构造后恒非空；数据非法时是空串）。
    const std::string& GetCurrentAnimation() const noexcept;

    /// 全部动作名（给 UI 做下拉 / 按钮组）。
    std::vector<std::string> GetAnimationNames() const;

    /// 构造时那份组数据（已 `Normalize`）。
    const InkingSpriteGroupData& GetGroupData() const noexcept;

    /**
     * 组这一层的错误说明（`InkingSpriteGroupData::Validate()` 的结论，或图集
     * 尺寸对不上）；空串 = 合法。
     *
     * 与 `GetConfigError()` **同一份内容**（构造时塞进去的）：组不想让调用方
     * 记两个错误口。两个都能用，`GetConfigError()` 是为了与 `InkingSprite` 同名。
     */
    const std::string& GetGroupError() const noexcept;

    // ---------------- 观测口（与 `InkingSprite` 同名同义） ----------------
    //
    // 帧机的观测口在 `InkingSpriteLike` 里，public 继承已经拿到；这里**再声明
    // 一遍**只是为了 `nm` 里能看到 `InkingSpriteGroup::GetFrameCount` 这类符号
    // （与 `InkingSprite` 的 ABI 转发同一个考虑：消费者按类名找得到）。

    bool IsPlaying() const noexcept;
    bool IsFinished() const noexcept;
    std::size_t GetFrameIndex() const noexcept;
    std::size_t GetFrameCount() const noexcept;
    std::size_t GetLoadedFrameCount() const noexcept;
    std::uint64_t GetFrameAdvanceCount() const noexcept;
    std::uint64_t GetSkippedFrameCount() const noexcept;

protected:
    /// 每块各画一次（同一张纹理 ⇒ 同帧一次提交）；不做描边 / 阴影（那会按 K 份画）。
    void onRender(float pixelX, float pixelY) const override;

    /// 每渲染帧一次：转调帧机的 `advanceFrame`（与 `InkingSprite` 同一行）。
    void onAnimationTick(float deltaSeconds) override;

private:
    /// 六件事各自绑到本类的成员上。**第三份接线**（前两份是 `InkingSprite` /
    /// `InkingDynamicSprite`）——三份只绑自己的成员，逻辑全在 `InkingSpriteLike`。
    static InkingSpriteLike::Hooks hooksFor(InkingSpriteGroup& self);

    /// 把第 `index` 个动作装进去：`setTiming` → 格号表转矩形表 → `replaceFrameTable`。
    void applyAnimation(std::size_t index, bool keepPhase);

    /// 图集几何校验：量一次图，与 `SheetWidth()/SheetHeight()` 比对。
    void validateSheetGeometry();

    /// 构造基类之前要的那份 `InkingImageData`（`static`：基类构造早于本类）。
    static InkingImageData imageDataFor(const InkingSpriteGroupData& data);

    InkingSpriteGroupData _group{};
    std::string _currentAnimation;
    std::string _groupError;

    /// 当前动作的下标（`kNotFound` = 还没装上任何一个）。
    std::size_t _currentIndex = InkingSpriteGroupData::kNotFound;

    /// 当前帧要画的 K 个源矩形（帧机的 `setSourceRects` 落在这里）。
    std::vector<InkingShapeBounds> _frameRects;

    /**
     * 组**不接受**"换整份 `InkingSpriteData`"：那会把帧表换成另一套（可能换文件、
     * 换尺寸），而组的不变量是"一张图集 + 格号表"。要切动作走 `SwitchTo`。
     *
     * 用 `using` 把它降成 private（继承来的 public 成员本来就在公开面上）。
     */
    using InkingSpriteLike::SetSpriteData;

    /// 同上：组的错误是 `_groupError`，`_configError` 只是它的镜像；`Play` 一族
    /// 保留公开（组当然能暂停 / 定位）。
    using InkingSpriteLike::GetSpriteData;
};
