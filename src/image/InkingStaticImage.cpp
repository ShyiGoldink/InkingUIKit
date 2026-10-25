#include <image/InkingStaticImage.h>

#include <asset/InkingAsset.h>
#include <button/ButtonPaint.h>
#include <detail/InkLog.h>
// `detail::TransformBounds`（AABB 绕中心过一遍变换）从**源项目那个单体头**
// （`include/ink/basic/InkingAnchor.h`）搬到了 `detail/InkingGeometry.h`。源项目里它是
// 经 `InkingHit.h` → `InkingAnchor.h` 传递进来的，而本仓库的 `InkingHit.h`
// **刻意只前向声明 `InkingWidget`**（不为一个引用参数拖整个树层），这条传递链
// 就断了——所以这里必须显式包含。**拆单体头时第一个现形的就是这类依赖。**
#include <detail/InkingGeometry.h>
#include <detail/InkingHit.h>
#include <scene/InkingScene.h>  // GetPointerX/Y：按下当场定格模式要用

#include <utility>

#include <algorithm>
#include <cmath>

namespace inking {

InkingStaticImage::InkingStaticImage(InkingWidget* parent, const ImageData& data)
    : InkingStaticWidget(parent, data.name) {
    applyData(data);
}

void InkingStaticImage::applyData(const ImageData& data) {
    // 拷一份再 Normalize：调用方给的可能"三态没补齐"，而本类要求 _data 永远
    // 处于补齐状态（绘制那边按它取外观）。图片只用 normal，但保持一致不吃亏。
    _data = data;
    _data.Normalize();

    // -----------------------------------------------------------------------
    // 几何：构造期一次性写进基类
    //
    // 基类**不存整份 `XxxData`**，所以每个字段都要在这里显式搬一次——漏一处的
    // 症状是"配置里写了、运行期就是没生效"，而且不报错（`AGENTS.md` §6 第 83 条）。
    // 静态组件的几何**构造即定型**：这里写的是"构造期一次性写入"，
    // 不是运行期几何变化（静态档没有几何写入口，那条保证由 src/inking.cpp 的
    // 编译期断言钉着）。
    // -----------------------------------------------------------------------
    _width = _data.width > 0 ? _data.width : 0;
    _height = _data.height > 0 ? _data.height : 0;
    _selfAnchor = _data.selfAnchor;
    _traceAnchor = _data.traceAnchor;
    _offsetX = _data.offsetX;
    _offsetY = _data.offsetY;
    // 布局**只搬 `_layoutItem`，不搬 `_layout`**——这不是漏搬，别"补"上去：
    //   - `_layoutItem`（我在父容器布局里的意愿）对图片有意义：它决定图片在
    //     父容器里怎么排；
    //   - `_layout`（我这个容器怎么排孩子）对图片没有意义——**图片不当容器**。
    // 搬了 `_layout` 的只有 panel / canvas / button 那三个（能挂子节点并排布
    // 的），`video` 与本类同档。判据是"它能不能当容器"，不是"字段有没有"。
    _layoutItem = _data.layoutItem;

    // 可见性与层级走现成的写入口：它们自己会夹取、自己判"真的变了没有"，
    // 也就不可能和基类的规则分叉（例如 zindex 的下限）。
    SetVisible(_data.visible);
    ChangeZIndex(_data.zIndex);

    ClearDirty();
    // 几何是构造期写入的，场景列表里的坐标快照要重算一次。
    NotifySceneStructureChanged();

    _imageName = _data.normal.source;
    refreshRenderAppearance();

    if (_data.runtimeAsset) {
        // 运行期那条路：字节不进 exe，构造时读一次。
        // 读不到**不中止构造**：给一张"只有底色"的图 + 一条错误日志，
        // 界面照常跑起来（缺一张图不该让整个程序起不来）。
        if (!LoadAsset(_data.normal.source)) {
            INK_LOG_ERROR(std::string(kImageModuleName),
                          "运行期读图失败：" + _data.normal.source
                              + "（资源根 " + AssetRoot()
                              + "）；这张图暂时只有底色");
        }
    }
}

void InkingStaticImage::refreshRenderAppearance() {
    _renderAppearance = _data.normal;
    if (!_assetBytes.empty()) {
        // 运行期读进来的字节：把视图指过去。**不能**改 `_data.normal`——
        // 那份是配置（生成期烘的静态数组），改了就把"配置"和"当前显示"混在一起。
        _renderAppearance.type = AppearanceType::Image;
        _renderAppearance.imageBytes = _assetBytes.data();
        _renderAppearance.imageSize = _assetBytes.size();
    }
}

// ---------------------------------------------------------------------------
// 查询
// ---------------------------------------------------------------------------

bool InkingStaticImage::HitTest(float worldX, float worldY) const {
    // 世界坐标 → 本地 → 形状判定，三步和绘制共用同一套定义。
    // 读的是**生效的显示变换**（A13：可能含外部拖动 / 缩放），不是配置里那一份
    // ——两处各读各的就会"看着在这儿、点的却是那儿"。
    return detail::HitTestShape(*this, GetShape(), GetDisplayTransform(),
                                worldX, worldY);
}

const ShapeSpec& InkingStaticImage::GetShape() const noexcept {
    return _data.shape;
}

const ButtonAppearance& InkingStaticImage::GetAppearance() const noexcept {
    return _renderAppearance;
}

std::uint32_t InkingStaticImage::GetDisplayColor() const noexcept {
    return _data.normal.color;
}

TransformSpec InkingStaticImage::GetDisplayTransform() const noexcept {
    // A13：外部显示变换（拖动 / 缩放）**叠在**配置那份之上。
    // 返回值类型（而不是引用）就是为它改的：合成结果不再是 `_data` 里的字段。
    return CombineWithExternal(_data.normal.transform);
}

TransformSpec InkingStaticImage::GetSubtreeTransform() const noexcept {
    // 和按钮 / 面板同一条约定：变换作用于整棵子树（图片自己不动，但可能被
    // 放在一个会动的容器里；这条让"图片里再挂东西"也画得对）。
    // A13：外部显示变换同样作用到整棵子树——否则"拖走图片、里面的东西留在原地"。
    return CombineWithExternal(_data.normal.transform);
}

ShapeBounds InkingStaticImage::GetHitEnvelope() const {
    // 复用按钮那份算法：它对图片自然退化成"这一态 + 它的变换"。
    const ShapeBounds base =
        detail::ButtonHitEnvelope(_data, GetShape(), GetWidth(), GetHeight());
    // A13：再套一层**外部变换**——不然图片被拖走之后，表里登记的仍是老位置，
    // 拖过去就点不到（这是"会把表动起来"的那条通道，见 InkingAnchor.h 那段）。
    // 绕的是组件中心，和 `ApplyTransform` 的语义一致。
    return detail::TransformBounds(
        base, GetExternalTransform(), static_cast<float>(GetWidth()) * 0.5f,
        static_cast<float>(GetHeight()) * 0.5f);
}

const ImageData& InkingStaticImage::GetData() const noexcept { return _data; }

const std::string& InkingStaticImage::ImageName() const noexcept {
    return _imageName;
}

std::size_t InkingStaticImage::AssetByteCount() const noexcept {
    return _assetBytes.size();
}

bool InkingStaticImage::UsingAssetBytes() const noexcept {
    return !_assetBytes.empty();
}

int InkingStaticImage::AssetLoadCount() const noexcept { return _assetLoadCount; }

// ---------------------------------------------------------------------------
// 运行期换图
// ---------------------------------------------------------------------------

bool InkingStaticImage::LoadAsset(const std::string& relative) {
    ++_assetLoadCount;
    std::vector<std::uint8_t> bytes = AssetLoad(relative);
    if (bytes.empty()) {
        // 读不到：**保留旧图**。把它清空会让画面变成一个洞，
        // 而"缺一张图"和"这块本来就什么都没有"是两件事。
        return false;
    }
    _assetBytes = std::move(bytes);
    _imageName = relative;
    refreshRenderAppearance();
    MarkRepaintDirty();  // 画面变了；几何没动，所以不标命中脏
    return true;
}

void InkingStaticImage::SetImageBytes(const std::uint8_t* data, std::size_t size,
                                      const std::string& name) {
    if (data == nullptr || size == 0) {
        _assetBytes.clear();
        _imageName.clear();
    } else {
        // 拷一份自己拥有：调用方的缓冲可能是临时的（解码结果、栈上的数组）。
        _assetBytes.assign(data, data + size);
        _imageName = name;
    }
    refreshRenderAppearance();
    MarkRepaintDirty();
}

// ---------------------------------------------------------------------------
// 拖拽 / 缩放（A13）
// ---------------------------------------------------------------------------

namespace {

/// 右下角把手的边长（设计坐标）：按在这块里算"抓把手"，否则算"抓整块"。
constexpr float kImageHandleSize = 20.0f;

/// 缩放的夹取范围：防止拖成 0（看不见了）或者无限大（手感与内存都不好）。
/// 这是**交互的底线**，不是配置项——真要配再往 `ImageData` 加（会多一处同步）。
constexpr float kImageMinScale = 0.2f;
constexpr float kImageMaxScale = 5.0f;

}  // namespace

void InkingStaticImage::SetDraggable(bool draggable) noexcept {
    _draggable = draggable;
}

void InkingStaticImage::SetResizable(bool resizable) noexcept {
    _resizable = resizable;
}

bool InkingStaticImage::IsDraggable() const noexcept { return _draggable; }
bool InkingStaticImage::IsResizable() const noexcept { return _resizable; }
bool InkingStaticImage::IsDragging() const noexcept { return _dragActive; }
bool InkingStaticImage::IsScalingDrag() const noexcept { return _dragScaling; }

void InkingStaticImage::resolveDragMode(float worldX, float worldY) noexcept {
    if (!_resizable) {
        _dragScaling = false;  // 没开缩放：一律当移动
        return;
    }
    const float width = static_cast<float>(GetWidth());
    const float height = static_cast<float>(GetHeight());
    const ShapeBounds handle{width - kImageHandleSize,
                             height - kImageHandleSize, kImageHandleSize,
                             kImageHandleSize};
    // 与**命中**同源：`HitTestLocalRect` 会把世界坐标还原成本地坐标再问这个矩形
    // （撤祖先变换 → 减绝对位置 → 过**自己变换的逆**）。
    // 少了最后那一步（只减绝对位置）就会出现"看到的把手"与"算出来的把手"错位：
    // **缩小时抓把手被判成移动**（手感就是"抓不住 / 好像有延迟"），放大时图片中部
    // 又会被误判成缩放。实测踩过。
    _dragScaling = detail::HitTestLocalRect(*this, handle, GetDisplayTransform(),
                                            worldX, worldY);
}

void InkingStaticImage::onPointerPress(PointerButton button, bool press) {
    // 拖动 / 缩放是主键手势（0.5.0）：右键抬起绝不能把正在进行的拖动收尾，
    // 否则"右键看着什么都不做，图片却不拖了"。
    if (button != PointerButton::Primary) {
        return;
    }
    if (press) {
        // **按下当场定格模式**（不等第一次拖动事件）：否则"按在把手上、手先动了一两
        // 像素"就可能被判成移动，用户会觉得"抓把手要试两次"。
        if ((_draggable || _resizable) && GetScene() != nullptr) {
            resolveDragMode(GetScene()->GetPointerX(), GetScene()->GetPointerY());
        }
        return;
    }
    // 抬手（或指针移出窗口 / 失焦时框架补的那一次）：这次交互结束。
    // 不清的话，下次按下会被当成"上一次还在拖"，模式判定就串了。
    _dragActive = false;
    _dragScaling = false;
}

void InkingStaticImage::onPointerDrag(float designX, float designY, float deltaX,
                                      float deltaY) {
    if (!_draggable && !_resizable) {
        return;  // 两个开关都关：这道钩子等于不存在
    }

    if (!_dragActive) {
        _dragActive = true;
        _dragStartTransform = GetExternalTransform();

        // 按下点在**世界坐标**里：框架约定"第一次拖动的位移是相对**按下点**"，
        // 所以当前位置减掉这次位移就是按下那一刻的指针。
        _dragPressX = designX - deltaX;
        _dragPressY = designY - deltaY;

        // 兜底再判一次（按下那一刻可能没有场景可用）。反推出来的按下点与按下时的
        // 指针位置应当一致，所以这里不会翻掉刚才定下的模式。
        resolveDragMode(_dragPressX, _dragPressY);
    }

    if (_dragScaling) {
        // 绕**左上角**缩放：定点 = 本地 (0,0)，而框架的变换是绕**中心**的，
        // 所以要让左上角不动，就得补一段平移：
        //     绕中心缩放 s 会把左上角挪到 c + s·R(θ)·(−c)，
        //     要它回到原处 ⇒ translate += (s − s0)·R(θ)·c。
        // 少了这段补偿，图片会"从两边一起长"，手感就不是"抓住右下角往外拉"。
        const float width =
            GetWidth() > 0 ? static_cast<float>(GetWidth()) : 1.0f;
        // ⚠️ 用**相对按下点的累计位移**（`designX - _dragPressX`），
        // **不是**本次事件的 `deltaX`：增量每次都从 `_dragStartTransform` 重算一遍，
        // 于是除了第一帧以外都会被算回原位——表现就是"稍微拉开一点就弹回去、
        // 像在对抗缩放"（实测踩过，`AGENTS.md` §6 第 96 条）。
        const float totalDx = designX - _dragPressX;
        float scale = _dragStartTransform.scale * (1.0f + totalDx / width);
        scale = std::clamp(scale, kImageMinScale, kImageMaxScale);

        const float centerX = static_cast<float>(GetWidth()) * 0.5f;
        const float centerY = static_cast<float>(GetHeight()) * 0.5f;
        const float radians =
            _dragStartTransform.rotate * detail::kTransformDegToRad;
        const float cosA = std::cos(radians);
        const float sinA = std::sin(radians);
        const float deltaScale = scale - _dragStartTransform.scale;

        TransformSpec next = _dragStartTransform;
        next.scale = scale;
        next.translateX = _dragStartTransform.translateX
                        + deltaScale * (centerX * cosA - centerY * sinA);
        next.translateY = _dragStartTransform.translateY
                        + deltaScale * (centerX * sinA + centerY * cosA);
        SetDisplayTransform(next);
        return;
    }

    if (_draggable) {
        // 整块移动：直接把这次位移加到外部变换上（标脏由写入口统一做）。
        TransformSpec next = GetExternalTransform();
        next.translateX += deltaX;
        next.translateY += deltaY;
        SetDisplayTransform(next);
    }
}

// ---------------------------------------------------------------------------
// 绘制
// ---------------------------------------------------------------------------

void InkingStaticImage::onRender(float pixelX, float pixelY) const {
    // 画什么由 `include/button/ButtonPaint.h` 提供——**和按钮同一份实现**：
    // 形状填充 / 图片贴图 / 描边阴影三笔的顺序与几何都在那边，这里只是把
    // "这一帧长什么样"喂进去。图片解不出来时它会退回按颜色填（不是个洞）。
    // A13：变换用**生效的那一份**（含外部拖动 / 缩放），和命中读的是同一个。
    detail::PaintButtonAppearance(*this, GetShape(), _renderAppearance,
                                  _data.stroke, _data.shadow,
                                  _data.normal.color, GetDisplayTransform(),
                                  pixelX, pixelY);
}

}  // namespace inking
