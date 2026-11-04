// 块叠加组的动态档实现（0.9.0 T-06）。
//
// 与静态档（`InkingSpriteGroup.cpp`）**逐字同源**，只有三处不同：
//   1. 基类是 `InkingDynamicWidget` + `InkingImageLike`，几何走动态档的四个写入口；
//   2. `HitTest` 自己判（静态档是 `InkingStaticImage::HitTest`）；
//   3. 多几个"运行期换图"的转发（静态档那些在 `InkingStaticImage` 上）。
//
// 重复是任务书 T-06 的方案 A（先复制、再抽取）。判据不是"看起来像"，而是自检里
// 那条「100 帧之后两档的帧号与换帧计数逐一相等」——真分家了它先红。

#include <image/InkingDynamicSpriteGroup.h>

#include <asset/InkingAsset.h>
#include <detail/InkLog.h>
#include <detail/InkingHit.h>
#include <render/InkingDraw.h>
#include <render/InkingText.h>

#include <SDL3/SDL.h>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// 构造
// ---------------------------------------------------------------------------

InkingImageData InkingDynamicSpriteGroup::imageDataFor(
    const InkingSpriteGroupData& data) {
    // 与静态档那份**逐字相同**（`InkingSpriteGroup::imageDataFor`）：
    // 组件框 = 一格大小、烘字节优先、左上角对齐。
    InkingImageData image;
    image.name = data.source.empty() ? std::string("spriteGroup") : data.source;
    image.width = data.frameWidth > 0 ? data.frameWidth : 1;
    image.height = data.frameHeight > 0 ? data.frameHeight : 1;
    image.normal.type = InkingAppearanceType::Image;
    image.normal.source = data.source;
    image.normal.imageBytes = data.imageBytes;
    image.normal.imageSize = data.imageSize;
    image.runtimeAsset = data.imageBytes == nullptr || data.imageSize == 0;
    image.shape = InkingShapeSpec::Rect();
    image.selfAnchor = data.selfAnchor;
    image.traceAnchor = data.traceAnchor;
    image.offsetX = data.offsetX;
    image.offsetY = data.offsetY;
    return image;
}

InkingDynamicSpriteGroup::InkingDynamicSpriteGroup(
    InkingWidget* parent, const InkingSpriteGroupData& data,
    const std::string& initialAnimation)
    : InkingDynamicWidget(parent,
                          data.source.empty() ? "spriteGroup" : data.source),
      InkingImageLike(static_cast<InkingWidget&>(*this)),
      InkingSpriteLike(static_cast<InkingWidget&>(*this),
                       static_cast<InkingImageLike&>(*this), hooksFor(*this)) {
    const InkingImageData image = imageDataFor(data);
    // 图片面：配置与资源状态归 `InkingImageLike`（`runtimeAsset` 时在这里读一次图）。
    resetImageData(image);

    // 几何：走**动态档的四个写入口**。它们在构造体里调是安全的——写入口只标脏、
    // 只通知场景，不读派生类状态（与 `InkingDynamicSprite::applyData` 同一条）。
    Resize(image.width > 0 ? image.width : 0,
           image.height > 0 ? image.height : 0);
    ChangeSelfAnchor(image.selfAnchor);
    ChangeTraceAnchor(image.traceAnchor);
    ChangeOffset(image.offsetX, image.offsetY);

    _group = data;
    _group.Normalize();
    _groupError = _group.Validate();
    setConfigError(_groupError);

    if (!_groupError.empty()) {
        INK_LOG_ERROR(std::string(InkingSpriteGroupModuleName),
                      "块叠加组配置不合法（" + _group.source + "）：" + _groupError);
        ClearDirty();  // 构造期这批几何写入已经通知过场景了
        return;
    }

    std::size_t index = 0;
    if (!initialAnimation.empty()) {
        const std::size_t found = _group.FindIndex(initialAnimation);
        if (found == InkingSpriteGroupData::kNotFound) {
            INK_LOG_ERROR(std::string(InkingSpriteGroupModuleName),
                          "初始动作不存在：\"" + initialAnimation + "\"（退回第 0 个，组 "
                              + _group.source + "）");
        } else {
            index = found;
        }
    }

    applyAnimation(index, false);
    validateSheetGeometry();
    setAutoplay(true);
    ClearDirty();
}

// ---------------------------------------------------------------------------
// 动作切换（与静态档逐字相同）
// ---------------------------------------------------------------------------

void InkingDynamicSpriteGroup::applyAnimation(std::size_t index, bool keepPhase) {
    if (index >= _group.animations.size()) {
        return;
    }
    const InkingSpriteGroupAnimation& animation = _group.animations[index];
    _currentIndex = index;
    _currentAnimation = animation.name;

    setTiming(animation.fps, animation.loop, 0, 0.0, animation.events);

    std::vector<std::vector<InkingShapeBounds>> perFrame;
    perFrame.reserve(animation.frames.size());
    for (const std::vector<int>& cells : animation.frames) {
        std::vector<InkingShapeBounds> rects;
        rects.reserve(cells.size());
        for (const int cell : cells) {
            rects.push_back(_group.CellRect(cell));
        }
        perFrame.push_back(std::move(rects));
    }

    replaceFrameTable(std::move(perFrame), keepPhase);
}

bool InkingDynamicSpriteGroup::SwitchTo(const std::string& name, bool keepPhase) {
    const std::size_t index = _group.FindIndex(name);
    if (index == InkingSpriteGroupData::kNotFound) {
        INK_LOG_ERROR(std::string(InkingSpriteGroupModuleName),
                      "动作不存在：\"" + name + "\"（组 " + _group.source + "）");
        return false;
    }
    if (index == _currentIndex) {
        return true;
    }
    const std::string previous = _currentAnimation;
    applyAnimation(index, keepPhase);
    animationChanged.Emit(_currentAnimation, previous);
    return true;
}

// ---------------------------------------------------------------------------
// 观测（与静态档逐字相同）
// ---------------------------------------------------------------------------

const std::string& InkingDynamicSpriteGroup::GetCurrentAnimation() const noexcept {
    return _currentAnimation;
}

std::vector<std::string>
InkingDynamicSpriteGroup::GetAnimationNames() const {
    std::vector<std::string> names;
    names.reserve(_group.animations.size());
    for (const InkingSpriteGroupAnimation& animation : _group.animations) {
        names.push_back(animation.name);
    }
    return names;
}

const InkingSpriteGroupData&
InkingDynamicSpriteGroup::GetGroupData() const noexcept {
    return _group;
}

const std::string& InkingDynamicSpriteGroup::GetGroupError() const noexcept {
    return _groupError;
}

bool InkingDynamicSpriteGroup::IsPlaying() const noexcept {
    return InkingSpriteLike::IsPlaying();
}

bool InkingDynamicSpriteGroup::IsFinished() const noexcept {
    return InkingSpriteLike::IsFinished();
}

std::size_t InkingDynamicSpriteGroup::GetFrameIndex() const noexcept {
    return InkingSpriteLike::GetFrameIndex();
}

std::size_t InkingDynamicSpriteGroup::GetFrameCount() const noexcept {
    return InkingSpriteLike::GetFrameCount();
}

std::size_t InkingDynamicSpriteGroup::GetLoadedFrameCount() const noexcept {
    return InkingSpriteLike::GetLoadedFrameCount();
}

std::uint64_t InkingDynamicSpriteGroup::GetFrameAdvanceCount() const noexcept {
    return InkingSpriteLike::GetFrameAdvanceCount();
}

std::uint64_t InkingDynamicSpriteGroup::GetSkippedFrameCount() const noexcept {
    return InkingSpriteLike::GetSkippedFrameCount();
}

// ---------------------------------------------------------------------------
// 接线（第三份 `hooksFor`：三份只绑自己的成员，逻辑全在 `InkingSpriteLike`）
// ---------------------------------------------------------------------------

InkingSpriteLike::Hooks InkingDynamicSpriteGroup::hooksFor(
    InkingDynamicSpriteGroup& self) {
    InkingSpriteLike::Hooks hooks;
    // 组不换字节（一张图集贯穿全部动作）。填上是为了不让空 `std::function` 被调。
    hooks.setBytes = [](const std::uint8_t*, std::size_t, const std::string&) {};
    hooks.loadAsset = [&self](const std::string& relative) {
        return self.LoadAsset(relative);
    };
    hooks.setSourceRects = [&self](const InkingShapeBounds* rects,
                                   std::size_t count) {
        if (rects == nullptr || count == 0) {
            self._frameRects.clear();
        } else {
            self._frameRects.assign(rects, rects + count);
        }
        self.MarkRepaintDirty();  // 换的是"取哪几块"，几何没动
    };
    hooks.frameChanged = [&self](std::size_t now, std::size_t before) {
        self.frameChanged.Emit(now, before);
    };
    hooks.frameEvent = [&self](const std::string& name, std::size_t frame) {
        self.frameEvent.Emit(name, frame);
    };
    hooks.finished = [&self] { self.finished.Emit(); };
    return hooks;
}

// ---------------------------------------------------------------------------
// 构造期：图集几何校验（与静态档逐字相同）
// ---------------------------------------------------------------------------

void InkingDynamicSpriteGroup::validateSheetGeometry() {
    const int expectedWidth = _group.SheetWidth();
    const int expectedHeight = _group.SheetHeight();
    if (expectedWidth <= 0 || expectedHeight <= 0) {
        return;
    }

    const InkingButtonAppearance& appearance = GetAppearance();
    const InkingImageBytes image{appearance.imageBytes, appearance.imageSize,
                                 appearance.source};
    int imageWidth = 0;
    int imageHeight = 0;
    if (!inking_detail::ProbeImageSize(image, &imageWidth, &imageHeight)) {
        INK_LOG_WARN(std::string(InkingSpriteGroupModuleName),
                     "量不到图集尺寸，网格几何未校验：" + _group.source);
        return;
    }
    if (imageWidth == expectedWidth && imageHeight == expectedHeight) {
        return;
    }

    _groupError = "图集尺寸对不上：真实图片是 " + std::to_string(imageWidth) + "×"
                  + std::to_string(imageHeight) + "，按 frameWidth="
                  + std::to_string(_group.frameWidth) + " / columns="
                  + std::to_string(_group.columns) + " / rows="
                  + std::to_string(_group.rows) + " / spacing="
                  + std::to_string(_group.spacing) + " / margin="
                  + std::to_string(_group.margin) + " 算出来应是 "
                  + std::to_string(expectedWidth) + "×"
                  + std::to_string(expectedHeight);
    setConfigError(_groupError);
    INK_LOG_ERROR(std::string(InkingSpriteGroupModuleName),
                  "块叠加组图集切格不成立（" + _group.source + "）：" + _groupError);
}

// ---------------------------------------------------------------------------
// 每帧 / 绘制（与静态档逐字相同）
// ---------------------------------------------------------------------------

void InkingDynamicSpriteGroup::onAnimationTick(float deltaSeconds) {
    advanceFrame(deltaSeconds);
}

void InkingDynamicSpriteGroup::onRender(float pixelX, float pixelY) const {
    if (_frameRects.empty()) {
        return;
    }
    SDL_Renderer* renderer = inking_detail::currentRenderer();
    if (renderer == nullptr) {
        return;
    }

    const InkingButtonAppearance& appearance = GetAppearance();
    if (appearance.type != InkingAppearanceType::Image
        || appearance.imageBytes == nullptr || appearance.imageSize == 0) {
        return;
    }
    const InkingImageBytes image{appearance.imageBytes, appearance.imageSize,
                                 appearance.source};

    const float magnification = GetMagnification() > 0.0f ? GetMagnification() : 1.0f;
    const InkingTransformSpec transform = GetDisplayTransform();
    const std::uint32_t tint = (GetDisplayColor() & 0xFF000000u) | 0x00FFFFFFu;

    for (const InkingShapeBounds& srcRect : _frameRects) {
        inking_detail::PaintImage(renderer, image, GetShape(), GetWidth(),
                                  GetHeight(), magnification, pixelX, pixelY,
                                  transform, appearance.imageFit, tint, srcRect);
    }
}

// ---------------------------------------------------------------------------
// 图片面（转发到 `InkingImageLike`；变换仍由本类合成）
// ---------------------------------------------------------------------------

bool InkingDynamicSpriteGroup::HitTest(float worldX, float worldY) const {
    // 世界坐标 → 本地 → 形状判定，与绘制共用同一套定义。几何改了这里跟着变
    // （动态档的验收第 ① 条就是它）。
    return inking_detail::HitTestShape(*this, GetShape(), GetDisplayTransform(),
                                       worldX, worldY);
}

const InkingShapeSpec& InkingDynamicSpriteGroup::GetShape() const noexcept {
    return InkingImageLike::GetShape();
}

const InkingButtonAppearance&
InkingDynamicSpriteGroup::GetAppearance() const noexcept {
    return InkingImageLike::GetAppearance();
}

std::uint32_t InkingDynamicSpriteGroup::GetDisplayColor() const noexcept {
    return InkingImageLike::GetDisplayColor();
}

InkingTransformSpec InkingDynamicSpriteGroup::GetDisplayTransform() const noexcept {
    // A13：外部显示变换叠在配置那份之上；返回值而不是引用。
    return CombineWithExternal(GetData().normal.transform);
}

InkingTransformSpec
InkingDynamicSpriteGroup::GetSubtreeTransform() const noexcept {
    return CombineWithExternal(GetData().normal.transform);
}

InkingShapeBounds InkingDynamicSpriteGroup::GetHitEnvelope() const {
    return InkingImageLike::GetHitEnvelope();
}

const InkingImageData& InkingDynamicSpriteGroup::GetData() const noexcept {
    return InkingImageLike::GetData();
}

const std::string& InkingDynamicSpriteGroup::ImageName() const noexcept {
    return InkingImageLike::ImageName();
}

std::size_t InkingDynamicSpriteGroup::AssetByteCount() const noexcept {
    return InkingImageLike::AssetByteCount();
}

bool InkingDynamicSpriteGroup::UsingAssetBytes() const noexcept {
    return InkingImageLike::UsingAssetBytes();
}

int InkingDynamicSpriteGroup::AssetLoadCount() const noexcept {
    return InkingImageLike::AssetLoadCount();
}

bool InkingDynamicSpriteGroup::LoadAsset(const std::string& relative) {
    if (!InkingImageLike::LoadAsset(relative)) {
        return false;
    }
    MarkRepaintDirty();  // 画面变了；几何没动
    return true;
}

void InkingDynamicSpriteGroup::SetImageBytes(const std::uint8_t* data,
                                             std::size_t size,
                                             const std::string& name) {
    InkingImageLike::SetImageBytes(data, size, name);
    MarkRepaintDirty();
}
