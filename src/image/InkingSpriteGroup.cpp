// 块叠加组的静态档实现（0.9.0 T-05）。
//
// 三块：构造（装数据 → 装动作 → 校验图集几何）、接线（`hooksFor` 把帧机的六个
// 回调绑到本类的成员上）、绘制（每块调一次 `PaintImage`，同一张纹理合并成一次提交）。

#include <image/InkingSpriteGroup.h>

#include <detail/InkLog.h>
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

InkingImageData InkingSpriteGroup::imageDataFor(
    const InkingSpriteGroupData& data) {
    InkingImageData image;
    image.name = data.source.empty() ? std::string("spriteGroup") : data.source;
    // 组件框 = **一格**的大小：K 块全部画在同一个框里（叠图），不是整张图集。
    // 这也解释了"等格同原点"——每块取图集里不同的格，但画在同一个框内。
    image.width = data.frameWidth > 0 ? data.frameWidth : 1;
    image.height = data.frameHeight > 0 ? data.frameHeight : 1;
    image.normal.type = InkingAppearanceType::Image;
    image.normal.source = data.source;
    // 生成期烘进来的字节优先（有就不读盘）；没有才走"运行期从资源根读"。
    // 与 `InkingSpriteLike::baseImageData` 里"序列帧取哪一张图"同一条口径。
    image.normal.imageBytes = data.imageBytes;
    image.normal.imageSize = data.imageSize;
    image.runtimeAsset = data.imageBytes == nullptr || data.imageSize == 0;
    image.shape = InkingShapeSpec::Rect();
    // 定位：左上角对齐父级左上角、偏移 0（与自检 / demo 的读回区域对得上）。
    // 要别的摆法就调 `ChangeSelfAnchor` / `ChangeOffset`（静态档也有那几个入口）。
    image.selfAnchor = InkingChangeAnchor::LeftTop;
    image.traceAnchor = InkingChangeAnchor::LeftTop;
    return image;
}

InkingSpriteGroup::InkingSpriteGroup(InkingWidget* parent,
                                     const InkingSpriteGroupData& data,
                                     const std::string& initialAnimation)
    : InkingStaticImage(parent, imageDataFor(data)),
      InkingSpriteLike(static_cast<InkingWidget&>(*this),
                       static_cast<InkingImageLike&>(*this), hooksFor(*this)) {
    _group = data;
    _group.Normalize();
    _groupError = _group.Validate();
    // 让 `GetConfigError()` / `IsValid()` 对组也有意义（否则它们恒真——
    // "配错了却看起来正常"正是最该避免的静默失效）。
    setConfigError(_groupError);

    if (!_groupError.empty()) {
        INK_LOG_ERROR(std::string(InkingSpriteGroupModuleName),
                      "块叠加组配置不合法（" + _group.source + "）：" + _groupError);
        // 配置不合法：一个动作都不装（`_frames` 空 ⇒ 不画）。节点仍在，父级布局
        // 照常——"少一个动画"不该让整个界面起不来。
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

    // 顺序：先装动作（内含 `setTiming` + `replaceFrameTable`），再量图校验，最后
    // 定播放状态。`setAutoplay` 必须在 `_frames` 建好之后——它按"有没有帧"决定。
    applyAnimation(index, false);
    validateSheetGeometry();
    setAutoplay(true);
}

// ---------------------------------------------------------------------------
// 动作切换
// ---------------------------------------------------------------------------

void InkingSpriteGroup::applyAnimation(std::size_t index, bool keepPhase) {
    if (index >= _group.animations.size()) {
        return;
    }
    const InkingSpriteGroupAnimation& animation = _group.animations[index];
    _currentIndex = index;
    _currentAnimation = animation.name;

    // ① 时间参数先装：`replaceFrameTable` 末尾会落帧，而落帧读 `_sprite.fps`。
    //    顺序反了就会拿上一个动作的 fps 算相位——"切动作之后第一帧怪怪的"。
    setTiming(animation.fps, animation.loop, 0, 0.0, animation.events);

    // ② 格号表 → 源矩形表。**几何换算只在这里**（`CellRect` 是格号的唯一算术点）。
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

    // ③ 换帧表：内部会清"没有旧帧"的账、重对时间基准、落第一帧。
    replaceFrameTable(std::move(perFrame), keepPhase);
}

bool InkingSpriteGroup::SwitchTo(const std::string& name, bool keepPhase) {
    const std::size_t index = _group.FindIndex(name);
    if (index == InkingSpriteGroupData::kNotFound) {
        // **不静默停在旧动作上**：返回 false + 一条 ERROR，否则"切了没反应"看起来
        // 像动画坏了（`InkingAnimateGroup::SwitchTo` 同一条判据）。
        INK_LOG_ERROR(std::string(InkingSpriteGroupModuleName),
                      "动作不存在：\"" + name + "\"（组 " + _group.source + "）");
        return false;
    }
    if (index == _currentIndex) {
        // 切到当前动作 = 空操作（不重建、不发事件）：UI 上一遍遍点"播放待机"不该
        // 每点一次就重置相位。要重置用 `SeekToFrame(0)`。
        return true;
    }

    const std::string previous = _currentAnimation;
    applyAnimation(index, keepPhase);
    // 先换完再发：回调里 `GetFrameCount()` / `IsPlaying()` 已经是新动作的。
    animationChanged.Emit(_currentAnimation, previous);
    return true;
}

// ---------------------------------------------------------------------------
// 观测
// ---------------------------------------------------------------------------

const std::string& InkingSpriteGroup::GetCurrentAnimation() const noexcept {
    return _currentAnimation;
}

std::vector<std::string> InkingSpriteGroup::GetAnimationNames() const {
    std::vector<std::string> names;
    names.reserve(_group.animations.size());
    for (const InkingSpriteGroupAnimation& animation : _group.animations) {
        names.push_back(animation.name);
    }
    return names;
}

const InkingSpriteGroupData& InkingSpriteGroup::GetGroupData() const noexcept {
    return _group;
}

const std::string& InkingSpriteGroup::GetGroupError() const noexcept {
    return _groupError;
}

bool InkingSpriteGroup::IsPlaying() const noexcept {
    return InkingSpriteLike::IsPlaying();
}

bool InkingSpriteGroup::IsFinished() const noexcept {
    return InkingSpriteLike::IsFinished();
}

std::size_t InkingSpriteGroup::GetFrameIndex() const noexcept {
    return InkingSpriteLike::GetFrameIndex();
}

std::size_t InkingSpriteGroup::GetFrameCount() const noexcept {
    return InkingSpriteLike::GetFrameCount();
}

std::size_t InkingSpriteGroup::GetLoadedFrameCount() const noexcept {
    return InkingSpriteLike::GetLoadedFrameCount();
}

std::uint64_t InkingSpriteGroup::GetFrameAdvanceCount() const noexcept {
    return InkingSpriteLike::GetFrameAdvanceCount();
}

std::uint64_t InkingSpriteGroup::GetSkippedFrameCount() const noexcept {
    return InkingSpriteLike::GetSkippedFrameCount();
}

// ---------------------------------------------------------------------------
// 接线：帧机的六个回调
// ---------------------------------------------------------------------------

InkingSpriteLike::Hooks InkingSpriteGroup::hooksFor(InkingSpriteGroup& self) {
    InkingSpriteLike::Hooks hooks;

    // 组**不换字节**：一张图集从头用到尾，`LoadedFrame::data` 恒空 ⇒ `applyFrame`
    // 永远不会走到这里。仍然填上：空的 `std::function` 被调用会抛
    // `std::bad_function_call`，那种崩法比"少一个回调"难查得多。
    hooks.setBytes = [](const std::uint8_t*, std::size_t, const std::string&) {};

    hooks.loadAsset = [&self](const std::string& relative) {
        return self.LoadAsset(relative);
    };

    // ⚠️ **不走** `InkingImageLike::SetRenderSourceRect`：那个只存**一个**矩形，
    //    而组一帧要 K 个。存进自己的 `_frameRects`，绘制时逐个取。
    hooks.setSourceRects = [&self](const InkingShapeBounds* rects,
                                   std::size_t count) {
        if (rects == nullptr || count == 0) {
            self._frameRects.clear();
        } else {
            self._frameRects.assign(rects, rects + count);
        }
        // 写入口负责标脏（`AGENTS.md` §3 第 3 条）：换的是"取哪几块"，
        // 几何一个字节都没动 ⇒ 只标重绘脏。
        self.MarkRepaintDirty();
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
// 构造期：图集几何校验
// ---------------------------------------------------------------------------

void InkingSpriteGroup::validateSheetGeometry() {
    const int expectedWidth = _group.SheetWidth();
    const int expectedHeight = _group.SheetHeight();
    if (expectedWidth <= 0 || expectedHeight <= 0) {
        return;  // 网格本身不合法，`InkingSpriteGroupData::Validate()` 已经报过
    }

    // 借绘制层"当前生效的那份字节"（基类构造里已经 `LoadAsset` 过了）。
    const InkingButtonAppearance& appearance = GetAppearance();
    const InkingImageBytes image{appearance.imageBytes, appearance.imageSize,
                                 appearance.source};
    int imageWidth = 0;
    int imageHeight = 0;
    if (!inking_detail::ProbeImageSize(image, &imageWidth, &imageHeight)) {
        // 解不出来（没接 SDL3_image / 字节坏了 / 是 SVG）：**不假装通过**，
        // 但也别拦着播——这台机器上整条图片路都画不出来，报两条错只是噪音。
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
// 每帧
// ---------------------------------------------------------------------------

void InkingSpriteGroup::onAnimationTick(float deltaSeconds) {
    // 帧机在 `InkingSpriteLike`；这里只是"渲染帧"那条通道的入口（与
    // `InkingSprite::onAnimationTick` 同一行）。
    advanceFrame(deltaSeconds);
}

void InkingSpriteGroup::onRender(float pixelX, float pixelY) const {
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
        return;  // 没有字节：不画（一张空图不如让对方看得见"什么都没有"）
    }
    const InkingImageBytes image{appearance.imageBytes, appearance.imageSize,
                                 appearance.source};

    const float magnification = GetMagnification() > 0.0f ? GetMagnification() : 1.0f;
    const InkingTransformSpec transform = GetDisplayTransform();
    // `tintColor` **只取 alpha**（`fillShapeTextured` 的约定）；RGB 置白 = 不改原图
    // 颜色。与 `PaintButtonAppearance` 的图片路径同一条取舍（那里直接写死白）。
    const std::uint32_t tint = (GetDisplayColor() & 0xFF000000u) | 0x00FFFFFFu;

    // 每块各自 `BuildShapeMesh` + 追加顶点：**同一张纹理**，提交层按纹理分批，
    // 所以 K 块合成一次 `SDL_RenderGeometry`（`BatchStats::textureDraws` 断言）。
    //
    // 不调 `PaintButtonAppearance`：它会把描边 / 阴影按 K 份各画一遍——K 块的
    // 轮廓完全相同，那样画出来的是一层层叠加的描边，不是"一个角色的描边"。
    for (const InkingShapeBounds& srcRect : _frameRects) {
        inking_detail::PaintImage(renderer, image, GetShape(), GetWidth(),
                                  GetHeight(), magnification, pixelX, pixelY,
                                  transform, appearance.imageFit, tint, srcRect);
    }
}
