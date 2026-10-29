#pragma once

// 动画组（0.6.6 T-18）：**一个角色有几个动作、切换其中一个**。
//
// ---------------------------------------------------------------------------
// 为什么是"一个节点 + 换配置"，不是"一个容器装 N 个 sprite"
//
// 后者最自然（每个动作有独立相位），但代价是三笔：
//   - 绘制列表与命中表里**始终躺着 N−1 个不可见节点**（`SetVisible(false)` 只是不画，
//     遍历与命中判定的开销照付）；
//   - 每个动作各有一份时间基准，切回来时相位是"上次离开的地方"——看起来像卡了一下；
//   - 与 0.6.2 的合并收益相抵（同图集 N 个 sprite 本来能合成一次提交，隐藏的那些
//     反而让节点数翻了 N 倍）。
//
// 所以走"数据层多个动作 + 组件层一个 `InkingSprite`"：切换 = `SetSpriteData()`（T-13），
// 节点数恒 1，订阅 / 父子 / z 序全都不动。
//
// ---------------------------------------------------------------------------
// 与单组件**相反**的一条：尺寸跟着动作走
//
//   - `InkingSprite::SetSpriteData()`：**不动** `width/height`（改尺寸会触发重布局，
//     那是调用方的事）；
//   - `InkingSpriteGroup::SwitchTo()`：**默认**把 `width/height` 改成新动作的配置值
//     （`resize = false` 可关）。
//
// 两处相反是有意的：组就是"一个角色"，角色的轮廓随动作变（待机 40 宽、攻击 56 宽）
// 是它的职责；而单个 sprite 换配置时，"要不要动布局"只有调用方知道。

#include <event/InkingEvent.h>
#include <image/InkingSprite.h>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace inking {

/// 模块名常量（日志用；约定：每个模块都暴露一个）。
inline constexpr const char* kSpriteGroupModuleName = "InkingSpriteGroup";

/**
 * @brief 一个角色的全部动作：**名字 → 一份 `SpriteData`**（顺序 = 声明顺序）。
 *
 * 用 `vector<pair>` 而不是 `map`：顺序有意义（第 0 个是默认动作），而且动作数
 * 只有几个，线性查比哈希更省也更可读。
 *
 * 每条 `SpriteData` 都是**完整配置**（素材、fps、loop、事件、宽高），组不替它们
 * 补默认值——"待机 12fps 循环、攻击 8fps 不循环"本来就是两条独立的配置。
 */
struct SpriteGroupData {
    std::vector<std::pair<std::string, SpriteData>> animations;

    /// 找不到时的返回值（**不是** 0：0 是第一个动作）。
    static constexpr std::size_t kNotFound = static_cast<std::size_t>(-1);

    /// 按名字找下标；找不到返回 `kNotFound`。
    std::size_t FindIndex(const std::string& animationName) const noexcept;

    /**
     * @brief 组自身的数据校验：返回**错误说明**，空串 = 通过。
     *
     * 只管"组"这一层（空表 / 动作名为空 / 重名）。每条 `SpriteData` 自己的合法性
     * 由 `SpriteData::Validate()` 判——换到那条动作时组件会照常报错并 `IsValid()`
     * 为假（判据不在这里复制一份）。
     */
    std::string Validate() const;
};

/**
 * @brief 动画组：**一个节点**承载多个动作，`SwitchTo()` 换的是配置不是节点。
 *
 * 构造时的 `initialAnimation` 决定先播哪个（空 = 第 0 个；名字不存在 → 记 ERROR
 * 并退回第 0 个——"少一个动作"不该让整个界面起不来）。
 */
class InkingSpriteGroup : public InkingSprite {
public:
    InkingSpriteGroup(InkingWidget* parent, const SpriteGroupData& data,
                      const std::string& initialAnimation = std::string());

    /**
     * 换过动作了（新动作名, 旧动作名）。
     *
     * - 在 `SetSpriteData()` **之后**发：回调里 `GetFrameCount()` / `IsPlaying()`
     *   已经是新动作的（顺序反过来，订阅者读到的还是旧动作的帧数）；
     * - 构造时**不发**（没有"旧动作"这回事）；
     * - **旧名字是临时量**，只在回调期间有效（新名字指向组件自己的成员）。
     */
    InkingEvent<const std::string&, const std::string&> animationChanged;

    /**
     * @brief 切到某个动作。
     *
     * @param name      动作名（`GetAnimationNames()` 里的一个）。
     * @param keepPhase `true` = 保留当前帧号（同动作换方向时用）；默认从第 0 帧起。
     * @param resize    `true`（默认）= 组件宽高改成新动作配置里的值。
     * @return 动作存在才返回 true。**不存在时返回 false + 一条 ERROR**——不静默停在
     *         旧动作上，否则"切了没反应"看起来像动画坏了。
     *
     * 切到**当前动作**是空操作（返回 true、不重建、不发事件）：UI 上一遍遍点
     * "播放待机"不该每点一次就重置相位。要重置用 `SeekToFrame(0)`。
     */
    bool SwitchTo(const std::string& name, bool keepPhase = false,
                  bool resize = true);

    /// 当前动作名（构造后恒非空；数据非法时是空串）。
    const std::string& GetCurrentAnimation() const noexcept;

    /// 全部动作名（给 UI 做下拉 / 按钮组）。
    std::vector<std::string> GetAnimationNames() const;

    /// 构造时那份组数据。
    const SpriteGroupData& GetGroupData() const noexcept;

    /// 组这一层的错误说明（空表 / 重名 / 动作名为空）；空串 = 合法。
    const std::string& GetGroupError() const noexcept;

private:
    SpriteGroupData _group{};
    std::string _currentAnimation;
    std::string _groupError;
};

}  // namespace inking
