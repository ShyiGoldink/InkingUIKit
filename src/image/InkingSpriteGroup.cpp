#include <image/InkingSpriteGroup.h>

#include <detail/InkLog.h>

namespace inking {

namespace {

/// 初始动作下标：空名 → 第 0 个；找不到 → `kNotFound`（由调用方报错后退回第 0 个）。
///
/// **不在这里打日志**：它会在构造函数的 mem-initializer 里被调一次，报错要等
/// 组件本身构造完再打（那时 `GetConfigError()` 之类的观测口才可用）。
std::size_t InitialIndex(const SpriteGroupData& group,
                         const std::string& wanted) noexcept {
    if (group.animations.empty()) {
        return SpriteGroupData::kNotFound;
    }
    if (wanted.empty()) {
        return 0u;
    }
    return group.FindIndex(wanted);
}

}  // namespace

std::size_t SpriteGroupData::FindIndex(const std::string& animationName) const noexcept {
    for (std::size_t i = 0; i < animations.size(); ++i) {
        if (animations[i].first == animationName) {
            return i;
        }
    }
    return kNotFound;
}

std::string SpriteGroupData::Validate() const {
    if (animations.empty()) {
        return "动画组里没有任何动作";
    }
    for (std::size_t i = 0; i < animations.size(); ++i) {
        const std::string& name = animations[i].first;
        if (name.empty()) {
            return "第 " + std::to_string(i) + " 个动作没有名字";
        }
        // 重名必须拦：`FindIndex()` 只命中第一个，第二个动作会被静默忽略，
        // 表现是"某个动作永远切不过去"。
        for (std::size_t j = i + 1; j < animations.size(); ++j) {
            if (animations[j].first == name) {
                return "动作名重复：\"" + name + "\"（第 " + std::to_string(i)
                       + " 条与第 " + std::to_string(j) + " 条）";
            }
        }
    }
    return {};
}

InkingSpriteGroup::InkingSpriteGroup(InkingWidget* parent,
                                     const SpriteGroupData& data,
                                     const std::string& initialAnimation)
    // 基类要先拿到一份**具体的**配置：空组时给一份空配置（`IsValid()` 会是假，
    // 与"配置坏了照样构造"的既有口径一致）。
    : InkingSprite(parent, [&data, &initialAnimation]() -> SpriteData {
          const std::size_t index = InitialIndex(data, initialAnimation);
          if (index != SpriteGroupData::kNotFound) {
              return data.animations[index].second;
          }
          // 名字写错 / 空表：基类也要拿一份**能播的**配置。给空配置的话，
          // 基类自己会再打一条"帧动画配置不合法"——两条 ERROR 里那条是派生的噪音
          // （自检抓过：`CountOf(Error)` 变成 2，且帧数为 0、画面一片空白）。
          return data.animations.empty() ? SpriteData{}
                                         : data.animations.front().second;
      }()) {
    _group = data;
    _groupError = _group.Validate();
    if (!_groupError.empty()) {
        INK_LOG_ERROR(std::string(kSpriteGroupModuleName),
                      "动画组配置不合法：" + _groupError);
        return;
    }

    const std::size_t index = InitialIndex(_group, initialAnimation);
    if (index == SpriteGroupData::kNotFound) {
        // 初始名字写错：退回第 0 个**并说出来**。静默退会让"一开始播错了动作"
        // 看起来像数据配错，而真正的问题只在那一行初始名上。
        _currentAnimation = _group.animations.front().first;
        INK_LOG_ERROR(std::string(kSpriteGroupModuleName),
                      "初始动作 \"" + initialAnimation + "\" 不存在，已退回第 0 个 \""
                          + _currentAnimation + "\"（可选："
                          + std::to_string(_group.animations.size()) + " 个）");
        return;
    }
    _currentAnimation = _group.animations[index].first;
}

bool InkingSpriteGroup::SwitchTo(const std::string& name, bool keepPhase,
                                 bool resize) {
    if (!_groupError.empty()) {
        // 组数据本身就不合法：任何切换都会落在"没有动作"上，直接拒绝，
        // 免得一条配置错刷出 N 条 ERROR。
        INK_LOG_ERROR(std::string(kSpriteGroupModuleName),
                      "动画组配置不合法，切换被忽略（要 \"" + name + "\"）："
                          + _groupError);
        return false;
    }

    const std::size_t index = _group.FindIndex(name);
    if (index == SpriteGroupData::kNotFound) {
        INK_LOG_ERROR(std::string(kSpriteGroupModuleName),
                      "动作 \"" + name + "\" 不存在（当前 \"" + _currentAnimation
                          + "\"，可选 " + std::to_string(_group.animations.size())
                          + " 个）——不静默停在旧动作上");
        return false;
    }

    if (name == _currentAnimation) {
        // 同一个动作：不重建、不发事件。理由见头文件（UI 反复点同一个按钮）。
        return true;
    }

    const SpriteData& target = _group.animations[index].second;
    const std::string previous = _currentAnimation;

    // 先换配置（帧号 / 素材 / 播放状态都归它管），**再**说"换了动作"：
    // 回调里读到的必须是新动作的帧数。
    SetSpriteData(target, keepPhase);
    _currentAnimation = name;

    if (resize) {
        // 组与单组件唯一相反的那一条（见头文件）。`ApplySelfSize` 不动锚点与 offset，
        // 所以"靠锚点摆着的角色"换动作不会跳位置，只是轮廓变了。
        ApplySelfSize(target.width, target.height);
    }

    animationChanged.Emit(_currentAnimation, previous);
    return true;
}

const std::string& InkingSpriteGroup::GetCurrentAnimation() const noexcept {
    return _currentAnimation;
}

std::vector<std::string> InkingSpriteGroup::GetAnimationNames() const {
    std::vector<std::string> names;
    names.reserve(_group.animations.size());
    for (const auto& entry : _group.animations) {
        names.push_back(entry.first);
    }
    return names;
}

const SpriteGroupData& InkingSpriteGroup::GetGroupData() const noexcept {
    return _group;
}

const std::string& InkingSpriteGroup::GetGroupError() const noexcept {
    return _groupError;
}

}  // namespace inking
