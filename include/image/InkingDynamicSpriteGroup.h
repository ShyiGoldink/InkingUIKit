#pragma once

// 块叠加组的**动态档**（0.9.0 T-06）：几何可以在运行期改的版本。
//
// ---------------------------------------------------------------------------
// 双档的理由与 Button / Image / Sprite 一致
//
//   - 静态档（`InkingSpriteGroup`）：几何**编译期定死**、进命中表；
//   - 动态档（本类）：四个几何写入口都在、**不进**命中表（占地记成"洞"）。
//
// 形状照 `InkingDynamicSprite`：`InkingDynamicWidget` + `InkingImageLike` +
// `InkingSpriteLike`。与静态档的差别只有"几何能不能改"与"进不进命中表"——
// 帧机、绘制、动作切换**逐字相同**。
//
// ---------------------------------------------------------------------------
// 为什么先复制、不抽 `InkingSpriteGroupLike`（任务书 T-06 方案 A）
//
// 第二档刚出现就抽，抽出什么全凭猜。"先复制、再抽取"是本仓库走过的路
// （G-03 → G-04），判据是「两档逐字相同 + 计数相等」而不是「看起来像」——
// 那个判据就在自检里（`CheckDynamicSpriteGroup` 的第 ② 条：100 帧之后两档的
// `GetFrameIndex()` 恒等、`GetFrameAdvanceCount()` 相等）。真分家了，那条先红。
//
// **唯一的重复点是 `onRender` 那十几行**：它按 `_frameRects` 逐块 `PaintImage`。
// 先接受；等第三档出现（或者它俩真分家）再抽。
//
// ---------------------------------------------------------------------------
// 命中表：不进表，占地记成"洞"
//
// 与 `InkingDynamicSprite` 同档：`HitTest` 自己判，场景只在点落进它的洞里时才来
// 问它。`OwnsSubtreeHit()` 保持 false（精灵不当容器）。
//
// 洞过期 = 那一片点不到，所以"`Resize` 之后命中跟着变"是 T-06 的验收第 ① 条：
// 它同时证明几何真的改了、且洞跟着刷了。
//
// `Resize` 之后**不需要**重算格矩形：等格是结构保证，矩形只与图集有关、与组件
// 框无关（这条是静态档与动态档能共用同一个 `_frameRects` 的原因）。

#include <event/InkingEvent.h>
#include <image/ImageLike.h>
#include <image/SpriteGroupData.h>
#include <image/SpriteLike.h>
#include <widget/InkingDynamicWidget.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class InkingWidget;

/**
 * @brief 几何可变、可每帧走位的块叠加组（0.9.0 T-06）。
 *
 * 数据面与静态档同一份 `InkingSpriteGroupData`；绘制与动作切换的实现与静态档
 * 逐字相同，差别只在几何写入口与命中表那一档。
 */
class InkingDynamicSpriteGroup : public InkingDynamicWidget,
                                 public InkingImageLike,
                                 public InkingSpriteLike {
public:
    InkingDynamicSpriteGroup(InkingWidget* parent, const InkingSpriteGroupData& data,
                             const std::string& initialAnimation = std::string());

    /// 换过动作了（新动作名, 旧动作名）。语义与静态档逐条相同。
    InkingEvent<const std::string&, const std::string&> animationChanged;

    /// 帧号变了（新帧号, 旧帧号）。构造时 / 切动作后的第一次落帧**不发**。
    InkingEvent<std::size_t, std::size_t> frameChanged;

    /// 播到某个**有事件的帧**了（事件名, 帧号）。
    InkingEvent<const std::string&, std::size_t> frameEvent;

    /// 非循环的动画播完了。
    InkingEvent<> finished;

    /**
     * @brief 切到某个动作（语义与静态档逐条相同）。
     *
     * 不存在 → false + 一条 ERROR；切到当前动作 → 空操作；`keepPhase` 保留帧号。
     */
    bool SwitchTo(const std::string& name, bool keepPhase = false);

    const std::string& GetCurrentAnimation() const noexcept;
    std::vector<std::string> GetAnimationNames() const;
    const InkingSpriteGroupData& GetGroupData() const noexcept;
    const std::string& GetGroupError() const noexcept;

    // ---------------- 观测口（与静态档同名同义） ----------------

    bool IsPlaying() const noexcept;
    bool IsFinished() const noexcept;
    std::size_t GetFrameIndex() const noexcept;
    std::size_t GetFrameCount() const noexcept;
    std::size_t GetLoadedFrameCount() const noexcept;
    std::uint64_t GetFrameAdvanceCount() const noexcept;
    std::uint64_t GetSkippedFrameCount() const noexcept;

    // ---------------- 与静态档同名同义的图片面 ----------------
    //
    // 静态档有的公开面这里一个不少（G-01 立的规矩）：这样"换档"才只换一个类型名。

    /// 指针在这个位置上吗（世界坐标）。**照常参与命中**：精灵是实体，不是标签。
    bool HitTest(float worldX, float worldY) const override;

    const InkingShapeSpec& GetShape() const noexcept;
    const InkingButtonAppearance& GetAppearance() const noexcept;
    std::uint32_t GetDisplayColor() const noexcept;
    InkingTransformSpec GetDisplayTransform() const noexcept;
    InkingTransformSpec GetSubtreeTransform() const noexcept override;
    bool AcceptsExternalTransform() const noexcept override { return true; }
    InkingShapeBounds GetHitEnvelope() const override;

    const InkingImageData& GetData() const noexcept;
    const std::string& ImageName() const noexcept;
    std::size_t AssetByteCount() const noexcept;
    bool UsingAssetBytes() const noexcept;
    int AssetLoadCount() const noexcept;

    /// 运行期换图：从资源根读一个相对路径。读不到**保留旧图**。
    bool LoadAsset(const std::string& relative);

    /// 运行期换图：直接给字节（不经过数据面的 `imageBytes` 那条构造期通道）。
    void SetImageBytes(const std::uint8_t* data, std::size_t size,
                       const std::string& name);

protected:
    /// 每块各画一次（同一张纹理 ⇒ 同帧一次提交）。与静态档逐字相同。
    void onRender(float pixelX, float pixelY) const override;

    /// 每渲染帧一次：转调帧机的 `advanceFrame`（两档**同一条路**）。
    void onAnimationTick(float deltaSeconds) override;

private:
    static InkingSpriteLike::Hooks hooksFor(InkingDynamicSpriteGroup& self);

    void applyAnimation(std::size_t index, bool keepPhase);
    void validateSheetGeometry();

    /// 构造基类之前要的那份 `InkingImageData`（与静态档那份逐字相同）。
    static InkingImageData imageDataFor(const InkingSpriteGroupData& data);

    InkingSpriteGroupData _group{};
    std::string _currentAnimation;
    std::string _groupError;
    std::size_t _currentIndex = InkingSpriteGroupData::kNotFound;
    std::vector<InkingShapeBounds> _frameRects;

    /// 组不接受"换整份 `InkingSpriteData`"：要切动作走 `SwitchTo`。
    using InkingSpriteLike::SetSpriteData;
    using InkingSpriteLike::GetSpriteData;
};
