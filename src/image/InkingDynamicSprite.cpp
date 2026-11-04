#include <image/InkingDynamicSprite.h>

// 动态精灵的实现（G-03 新增；G-04 把帧机抽进 `InkingSpriteLike` 之后只剩接线）。
//
// 这里**只剩动态档自己的三件事**：
//   1. 把 `InkingSpriteData` 搬进 `InkingImageLike`（图片面）与 `InkingSpriteLike`（帧机）；
//   2. 几何走动态档的四个写入口（静态档没有这几个口，那是两档最本质的差别）；
//   3. 绘制与命中的两笔（`PaintButtonAppearance` / `HitTestShape`，与图片同源）。
//
// 帧推进、图集切格、事件区间扫描全在 `InkingSpriteLike`——G-03 时这里是那份"复制品"，
// 当时靠"两份逐字节相同"证明同源；现在只有一份，那条纪律作废。

#include <asset/InkingAsset.h>
#include <button/ButtonPaint.h>
#include <detail/InkLog.h>
#include <detail/InkingHit.h>

// ---------------------------------------------------------------------------
// 构造
// ---------------------------------------------------------------------------

InkingSpriteLike::Hooks InkingDynamicSprite::hooksFor(InkingDynamicSprite& self) {
    // 六件事各自绑到本类的成员上（静态档绑的是继承来的那一套，语义相同：
    // 都会标重绘脏；动态档这三个是本类自己声明的版本）。
    InkingSpriteLike::Hooks hooks;
    hooks.setBytes = [&self](const std::uint8_t* data, std::size_t size,
                             const std::string& name) {
        self.SetImageBytes(data, size, name);
    };
    hooks.loadAsset = [&self](const std::string& relative) {
        return self.LoadAsset(relative);
    };
    hooks.setSourceRects = [&self](const InkingShapeBounds* rects,
                                   std::size_t count) {
        // 与静态档同一套（`InkingSprite::hooksFor` 里有完整理由）：单块档只该拿到
        // 一块；多块是数据层漏校验，画第一块 + 一条 WARN。
        if (count != 1) {
            INK_LOG_WARN(std::string(InkingSpriteModuleName),
                         "单块动态精灵收到 " + std::to_string(count)
                             + " 个源矩形，只画第一块（检查数据层）");
        }
        if (count > 0 && rects != nullptr) {
            self.SetRenderSourceRect(rects[0]);
        }
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

InkingDynamicSprite::InkingDynamicSprite(InkingWidget* parent,
                                         const InkingSpriteData& data)
    : InkingDynamicWidget(parent, data.name),
      InkingImageLike(static_cast<InkingWidget&>(*this)),
      InkingSpriteLike(static_cast<InkingWidget&>(*this),
                 static_cast<InkingImageLike&>(*this), hooksFor(*this)) {
    applyData(data);
}

void InkingDynamicSprite::applyData(const InkingSpriteData& data) {
    // G-02：配置与资源状态归 InkingImageLike；几何仍走动态档的公开写入口。
    //
    // 传进去的是 `InkingSpriteLike::baseImageData(data)` 而不是 `data`——静态版是拿它当
    // 基类构造的参数，这里只有一个 InkingImageLike 可喂，但**那份数据必须一样**，否则
    // `GetDisplayTransform()` / `GetHitEnvelope()` 读到的 `normal` 会分叉。
    resetImageData(InkingSpriteLike::baseImageData(data));
    const InkingImageData& config = GetData();

    // -----------------------------------------------------------------------
    // 几何：走**动态档的写入口**（这正是与静态版最本质的差别，同 G-01）
    //
    // 写入口自己会标脏、自己会通知场景（走 T-19 的分流），于是"改了几何忘了
    // 通知"这条路在动态档上不存在。构造函数体里调是安全的：写入口只标脏、
    // 只通知场景，不读派生类状态。
    // -----------------------------------------------------------------------
    Resize(config.width > 0 ? config.width : 0,
           config.height > 0 ? config.height : 0);
    ChangeSelfAnchor(config.selfAnchor);
    ChangeTraceAnchor(config.traceAnchor);
    ChangeOffset(config.offsetX, config.offsetY);

    // 布局**只搬 `_layoutItem`，不搬 `_layout`**——精灵不当容器（判据是"它能不能
    // 当容器"，不是"字段有没有"）。
    _layoutItem = config.layoutItem;

    SetVisible(config.visible);
    ChangeZIndex(config.zIndex);
    // 命中策略不是几何：它不标脏、不进命中表。
    SetHitPassThrough(config.hitPassThrough);

    if (config.runtimeAsset) {
        // 运行期那条路：字节不进 exe，构造时读一次（读不到保留底色 + ERROR）。
        // 序列帧读的是 `baseImageData` 换成的**起始帧**那份，与静态版同一个入口。
        if (!LoadAsset(config.normal.source)) {
            INK_LOG_ERROR(std::string(InkingSpriteModuleName),
                          "动态精灵运行期读图失败：" + config.normal.source
                              + "（资源根 " + InkingAssetRoot()
                              + "）；这张图暂时只有底色");
        }
    }
#if defined(INK_ISDEBUG)
    else {
        noticeUndersampled();
    }
#endif

    // -----------------------------------------------------------------------
    // 帧机：搬配置 → 落起始帧 → 定播放状态（G-04：整段在 `InkingSpriteLike`）
    //
    // 时间基准不在这里对（构造可能早于"进场景"），那是 `ensureTimeBase` 的事。
    // -----------------------------------------------------------------------
    resetSpriteData(data);

    // 构造期这批几何写入已经通知过场景了，没有"等着被消费"的改动。
    ClearDirty();
}

void InkingDynamicSprite::refreshRenderAppearance() {
    InkingImageLike::refreshRenderAppearance();
}

void InkingDynamicSprite::SetRenderSourceRect(const InkingShapeBounds& srcRect) {
    InkingImageLike::SetRenderSourceRect(srcRect);
    MarkRepaintDirty();  // 画面变了；几何没动，所以不标命中脏
}

const InkingShapeBounds& InkingDynamicSprite::GetRenderSourceRect() const noexcept {
    return InkingImageLike::GetRenderSourceRect();
}

// ---------------------------------------------------------------------------
// 每帧推进（帧机在 InkingSpriteLike，这里只是"渲染帧"那条通道的入口）
// ---------------------------------------------------------------------------

void InkingDynamicSprite::onAnimationTick(float deltaSeconds) {
    advanceFrame(deltaSeconds);
}

// ---------------------------------------------------------------------------
// 帧机转发（实现全在 InkingSpriteLike；本类保留同名声明，公开面与 G-03 一致）
// ---------------------------------------------------------------------------
//
// 为什么要这一层：G-03 的 `InkingDynamicSprite` 已经有这些公开函数，调用方
// （含自检）写的是 `sprite->SeekToFrame(2)`。让它们继续在本类名下，公开面不变，
// 也就不用去动 `selftest` 一个字。代价是 17 行一行转发——纯重构里这是最便宜的
// 那种代价。

bool InkingDynamicSprite::IsPlaying() const noexcept {
    return InkingSpriteLike::IsPlaying();
}

bool InkingDynamicSprite::IsFinished() const noexcept {
    return InkingSpriteLike::IsFinished();
}

std::size_t InkingDynamicSprite::GetFrameIndex() const noexcept {
    return InkingSpriteLike::GetFrameIndex();
}

std::size_t InkingDynamicSprite::GetFrameCount() const noexcept {
    return InkingSpriteLike::GetFrameCount();
}

std::size_t InkingDynamicSprite::GetLoadedFrameCount() const noexcept {
    return InkingSpriteLike::GetLoadedFrameCount();
}

std::uint64_t InkingDynamicSprite::GetFrameAdvanceCount() const noexcept {
    return InkingSpriteLike::GetFrameAdvanceCount();
}

std::uint64_t InkingDynamicSprite::GetSkippedFrameCount() const noexcept {
    return InkingSpriteLike::GetSkippedFrameCount();
}

double InkingDynamicSprite::GetAnimationTime() const noexcept {
    return InkingSpriteLike::GetAnimationTime();
}

const InkingSpriteData& InkingDynamicSprite::GetSpriteData() const noexcept {
    return InkingSpriteLike::GetSpriteData();
}

const std::string& InkingDynamicSprite::GetConfigError() const noexcept {
    return InkingSpriteLike::GetConfigError();
}

bool InkingDynamicSprite::IsValid() const noexcept {
    return InkingSpriteLike::IsValid();
}

bool InkingDynamicSprite::IsSheetMode() const noexcept {
    return InkingSpriteLike::IsSheetMode();
}

void InkingDynamicSprite::Play() { InkingSpriteLike::Play(); }
void InkingDynamicSprite::Pause() { InkingSpriteLike::Pause(); }
void InkingDynamicSprite::Stop() { InkingSpriteLike::Stop(); }

bool InkingDynamicSprite::SeekToFrame(std::size_t frame) {
    return InkingSpriteLike::SeekToFrame(frame);
}

void InkingDynamicSprite::SetSpriteData(const InkingSpriteData& data, bool keepPhase) {
    InkingSpriteLike::SetSpriteData(data, keepPhase);
}

// ---------------------------------------------------------------------------
// 查询：图片面转发到 InkingImageLike，变换仍由动态档自己合成
// ---------------------------------------------------------------------------

bool InkingDynamicSprite::HitTest(float worldX, float worldY) const {
    // 世界坐标 → 本地 → 形状判定，与绘制共用同一套定义；读的是**生效的变换**
    // （A13 可能叠了外部那份）。几何改了，这里跟着变——那是 G-03 的验收第 3 条。
    return inking_detail::HitTestShape(*this, GetShape(), GetDisplayTransform(),
                                worldX, worldY);
}

const InkingShapeSpec& InkingDynamicSprite::GetShape() const noexcept {
    return InkingImageLike::GetShape();
}

const InkingButtonAppearance& InkingDynamicSprite::GetAppearance() const noexcept {
    return InkingImageLike::GetAppearance();
}

std::uint32_t InkingDynamicSprite::GetDisplayColor() const noexcept {
    return InkingImageLike::GetDisplayColor();
}

InkingTransformSpec InkingDynamicSprite::GetDisplayTransform() const noexcept {
    // A13：外部显示变换叠在配置那份之上；返回值而不是引用（合成结果不再指向字段）。
    return CombineWithExternal(GetData().normal.transform);
}

InkingTransformSpec InkingDynamicSprite::GetSubtreeTransform() const noexcept {
    return CombineWithExternal(GetData().normal.transform);
}

InkingShapeBounds InkingDynamicSprite::GetHitEnvelope() const {
    return InkingImageLike::GetHitEnvelope();
}

const InkingImageData& InkingDynamicSprite::GetData() const noexcept {
    return InkingImageLike::GetData();
}

const std::string& InkingDynamicSprite::ImageName() const noexcept {
    return InkingImageLike::ImageName();
}

std::size_t InkingDynamicSprite::AssetByteCount() const noexcept {
    return InkingImageLike::AssetByteCount();
}

bool InkingDynamicSprite::UsingAssetBytes() const noexcept {
    return InkingImageLike::UsingAssetBytes();
}

int InkingDynamicSprite::AssetLoadCount() const noexcept {
    return InkingImageLike::AssetLoadCount();
}

// ---------------------------------------------------------------------------
// 运行期换图（共用实现不标脏，动态档在这里补重绘脏）
// ---------------------------------------------------------------------------

bool InkingDynamicSprite::LoadAsset(const std::string& relative) {
    if (!InkingImageLike::LoadAsset(relative)) {
        return false;
    }
    MarkRepaintDirty();  // 画面变了；几何没动，所以不标命中脏
#if defined(INK_ISDEBUG)
    noticeUndersampled();
#endif
    return true;
}

void InkingDynamicSprite::SetImageBytes(const std::uint8_t* data,
                                        std::size_t size,
                                        const std::string& name) {
    InkingImageLike::SetImageBytes(data, size, name);
    MarkRepaintDirty();
#if defined(INK_ISDEBUG)
    noticeUndersampled();
#endif
}

// ---------------------------------------------------------------------------
// 绘制（与静态版逐字相同：`PaintButtonAppearance` 那一笔）
// ---------------------------------------------------------------------------

void InkingDynamicSprite::onRender(float pixelX, float pixelY) const {
    // 画什么由 `include/button/ButtonPaint.h` 提供——**和按钮同一份实现**。
    // 图集切格已经落在 `GetAppearance().srcRect` 里（`SetRenderSourceRect` 写的），
    // 所以这里不必再分支：序列帧与图集在这一笔上是同一件事。
    inking_detail::PaintButtonAppearance(*this, GetShape(), GetAppearance(),
                                  GetData().stroke, GetData().shadow,
                                  GetDisplayColor(), GetDisplayTransform(),
                                  pixelX, pixelY);
}

