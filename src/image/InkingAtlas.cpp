#include <image/InkingAtlas.h>

#include <detail/InkLog.h>

#include <cmath>

namespace inking {

namespace {

/// 帧矩形落在图集内吗（只做"非负 + 宽高为正"这层，边界由组件构造期量图再验）。
bool FrameRectUsable(const ShapeBounds& rect) noexcept {
    return rect.width > 0.0f && rect.height > 0.0f && rect.x >= 0.0f && rect.y >= 0.0f;
}

}  // namespace

std::size_t AtlasData::FindIndex(const std::string& animationName) const noexcept {
    for (std::size_t i = 0; i < animations.size(); ++i) {
        if (animations[i].name == animationName) {
            return i;
        }
    }
    return kNotFound;
}

std::string AtlasData::Validate() const {
    if (animations.empty()) {
        return "图集里没有任何动画（" + name + "）";
    }
    for (std::size_t i = 0; i < animations.size(); ++i) {
        const AtlasAnimation& animation = animations[i];
        if (animation.frames.empty()) {
            return "动画 \"" + animation.name + "\" 没有帧";
        }
        if (animation.source.empty()) {
            return "动画 \"" + animation.name + "\" 没有图集路径（整张图集只有一份字节，"
                   "路径写在动画上）";
        }
        for (std::size_t f = 0; f < animation.frames.size(); ++f) {
            if (!FrameRectUsable(animation.frames[f])) {
                return "动画 \"" + animation.name + "\" 第 " + std::to_string(f)
                       + " 帧的矩形非法（宽高必须 > 0、左上角不能为负）";
            }
        }
        // 重名**必须**在这里拦：`FindIndex()` 只命中的第一个，其余会被静默忽略，
        // 表现是"某个动作永远播不出来"——比报错难查得多。
        for (std::size_t j = i + 1; j < animations.size(); ++j) {
            if (animations[j].name == animation.name) {
                return "动画名重复：\"" + animation.name + "\"（第 " + std::to_string(i)
                       + " 条与第 " + std::to_string(j) + " 条）——按名字找只会命中第一条";
            }
        }
    }
    return {};
}

SpriteData MakeSpriteData(const AtlasData& atlas, std::size_t animationIndex,
                          const std::uint8_t* imageBytes, std::size_t imageSize) {
    SpriteData data;
    data.name = atlas.name;
    data.normal.type = AppearanceType::Image;

    if (animationIndex >= atlas.animations.size()) {
        // 越界不是"返回空"就完事：调用方多半是拿 `FindIndex()` 的返回值直接喂进来，
        // 而 `kNotFound` 是个巨大的 `size_t`——不打一条日志，画面只是一片空白。
        INK_LOG_ERROR(std::string(kAtlasModuleName),
                      "动画下标越界（图集 \"" + atlas.name + "\"，要第 "
                          + std::to_string(animationIndex) + " 条，共 "
                          + std::to_string(atlas.animations.size()) + " 条）");
        return data;  // 没有 frames 也没有 sheet：`SpriteData::Validate()` 会说清楚
    }

    const AtlasAnimation& animation = atlas.animations[animationIndex];

    // 名字带上图集名：同屏两个图集都有 "walk" 时，日志/观测口才分得开。
    if (!atlas.name.empty()) {
        data.name = atlas.name + "/" + animation.name;
    } else {
        data.name = animation.name;
    }

    data.normal.source = animation.source;
    // `sheet.source` **留空**（= 沿用 `normal.source`）：两处都写就是两份真相。
    data.sheet.frameRects = animation.frames;
    data.fps = animation.fps;
    data.loop = animation.loop;
    data.events = animation.events;

    // 字节：给了就借视图（生成期烘的那条路），没给就让基类按 `runtimeAsset` 去读。
    data.runtimeAsset = imageBytes == nullptr || imageSize == 0;
    if (!data.runtimeAsset) {
        data.normal.imageBytes = imageBytes;
        data.normal.imageSize = imageSize;
    }

    // 尺寸默认跟第一帧（四舍五入）：图集不知道该画多大，"和第一帧一样大"是最少
    // 意外的默认值。改尺寸属于布局层，不在这里顺手做。
    data.width = static_cast<int>(std::lround(animation.frames.front().width));
    data.height = static_cast<int>(std::lround(animation.frames.front().height));

    // 收尾交给 `Normalize()`（它会把 `sheet.count` 补成帧表长度）：不补的话
    // `FrameCount()` 在转出来的配置上是 0——调用方拿到的"3 帧动画"看着像没帧。
    data.Normalize();
    return data;
}

}  // namespace inking
