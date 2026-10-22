#include <button/ButtonPaint.h>

#include <render/InkingDraw.h>
#include <render/InkingText.h>

#include <SDL3/SDL.h>

#include <cstdint>

namespace inking::detail {

namespace {

/**
 * 配置里没写 fontSize 时用的兜底字号（设计坐标）。
 *
 * 文字真正落地（字形图集）时，这里会换成"机器默认字体在当前 DPI 下的推荐
 * 字号"；现在写死一个常量，是为了让"没配字号"不至于画出一个零高度的方块——
 * 那看起来像 bug，其实是没配。
 */
constexpr float kDefaultFontSize = 16.0f;

/**
 * 设计坐标 → 设备像素的倍率。
 *
 * 基类的 SubmitToRenderer 已经把**左上角**换算过了，但尺寸还得自己乘——
 * 这是"展示倍率不参与布局、只在绘制时换算"那条（docs/API.md「[x] 展示倍率」）。
 */
float Magnify(const InkingAnchor& node) {
    const float value = node.GetMagnification();
    return value > 0.0f ? value : 1.0f;
}

/// 文字占位块的颜色兜底：文字色纯透明时看不到，给个不透明的白。
std::uint32_t VisibleTextColor(std::uint32_t color) {
    return ColorAlpha(color) == 0u ? 0xFFFFFFFFu : color;
}

}  // namespace

// ---------------------------------------------------------------------------
// 「形状表面」三笔的公共件（0.2.0 T-07）
//
// 从 `PaintButtonAppearance` 整段搬出，**顺序不变**：阴影 → 填充 → 描边。
// 搬完画面必须一模一样——这条由自检的像素断言锁着（button_paint_unchanged）。
// ---------------------------------------------------------------------------

void PaintSurfaceShadow(SDL_Renderer* renderer, const InkingAnchor& node,
                        const ShapeSpec& shape, const ShadowSpec& shadow,
                        const TransformSpec& transform, float pixelX,
                        float pixelY) {
    // ① 阴影：形状的**偏移实心副本**（没有模糊，见 InkingDecoration.h）。
    // 先画它，后面两笔才盖在上面。它跟变换一起走（旋转的组件，阴影也转）。
    if (renderer == nullptr || !shadow.Visible()) {
        return;
    }
    const float mag = Magnify(node);
    fillShape(renderer, shape, node.GetWidth(), node.GetHeight(), mag,
              pixelX + shadow.offsetX * mag, pixelY + shadow.offsetY * mag,
              transform, shadow.color);
}

void PaintSurfaceFill(SDL_Renderer* renderer, const InkingAnchor& node,
                      const ShapeSpec& shape, std::uint32_t fillColor,
                      const TransformSpec& transform, float pixelX,
                      float pixelY) {
    // ② 填充：形状怎么变成像素（三角化 + 边缘羽化 + 顶点过变换）在
    // `detail::fillShape` 里，按钮 / 面板 / 基类默认矩形共用同一份实现。
    if (renderer == nullptr) {
        return;
    }
    fillShape(renderer, shape, node.GetWidth(), node.GetHeight(), Magnify(node),
              pixelX, pixelY, transform, fillColor);
}

void PaintSurfaceStroke(SDL_Renderer* renderer, const InkingAnchor& node,
                        const ShapeSpec& shape, const StrokeSpec& stroke,
                        const TransformSpec& transform, float pixelX,
                        float pixelY) {
    // ③ 描边：宽度一半在形状外、一半在内。最后画，才能压在填充/图片的边缘上
    // （描边外半圈本来就该盖住底下那点像素）。
    if (renderer == nullptr || !stroke.Visible()) {
        return;
    }
    strokeShape(renderer, shape, node.GetWidth(), node.GetHeight(),
                Magnify(node), pixelX, pixelY, transform, stroke.width,
                stroke.color);
}

void PaintSurface(SDL_Renderer* renderer, const InkingAnchor& node,
                  const ShapeSpec& shape, const StrokeSpec& stroke,
                  const ShadowSpec& shadow, std::uint32_t fillColor,
                  const TransformSpec& transform, float pixelX, float pixelY) {
    PaintSurfaceShadow(renderer, node, shape, shadow, transform, pixelX, pixelY);
    PaintSurfaceFill(renderer, node, shape, fillColor, transform, pixelX,
                     pixelY);
    PaintSurfaceStroke(renderer, node, shape, stroke, transform, pixelX,
                       pixelY);
}

void PaintButtonAppearance(const InkingAnchor& node, const ShapeSpec& shape,
                           const ButtonAppearance& appearance,
                           const StrokeSpec& stroke, const ShadowSpec& shadow,
                           std::uint32_t displayColor,
                           const TransformSpec& transform, float pixelX,
                           float pixelY) {
    // 三态之间的差别体现在**颜色（含 alpha）+ 变换**上（T-4 之后还多了外观
    // 类型：img 画一张纹理），形状与尺寸完全不动——这正是静态按钮能一边有动画、
    // 一边继续待在"进命中表"那一档的原因（变换不改表，见 InputDesign.md §11）。
    //
    // 三笔已经抽成公共件（T-07）：纯色路径整段走 `PaintSurface`；
    // 图片路径只换中间那笔（填充 → 纹理），顺序仍是阴影 → 填充/图片 → 描边。
    SDL_Renderer* renderer = currentRenderer();
    const bool hasImage = appearance.type == AppearanceType::Image
        && appearance.imageBytes != nullptr && appearance.imageSize > 0;
    if (!hasImage) {
        PaintSurface(renderer, node, shape, stroke, shadow, displayColor,
                     transform, pixelX, pixelY);
        return;
    }

    PaintSurfaceShadow(renderer, node, shape, shadow, transform, pixelX,
                       pixelY);

    // ---- ② 图片（中间那笔换成纹理） ----
    bool painted = false;
    {
        const ImageBytes image{appearance.imageBytes, appearance.imageSize,
                               appearance.source};
        // ⚠️ 图片的调制色**固定为白**，不是 `displayColor`。
        //
        // 理由：`img` 外观**没有颜色字段**（CXXCSS 的图片层只认 type / source /
        // transition / transform / width / height / fit），所以 `AppearanceData::color`
        // 对它永远是结构体默认值 **0xFF000000（不透明黑）**。拿它去调制纹理 =
        // 把整张图乘成黑块——症状是"背景与立绘全黑，看着像图没加载"，
        // 而 `imageBytes` 明明非空（实测：出图 11 KB 全黑 vs 旧版 738 KB 正常）。
        // 白 = 不改动原图；三态之间的"淡入淡出"走 `transition` 那条通道。
        const std::uint32_t tint = 0xFFFFFFFFu;
        if (PaintImage(renderer, image, shape, node.GetWidth(),
                       node.GetHeight(), Magnify(node), pixelX, pixelY,
                       transform, appearance.imageFit, tint)) {
            painted = true;  // 图片画上了，就走不到"按颜色填"
        }
        // 解不出来：落回"按颜色填"（按钮还是个按钮，不是个洞）。
    }
    if (!painted) {
        PaintSurfaceFill(renderer, node, shape, displayColor, transform, pixelX,
                         pixelY);
    }

    PaintSurfaceStroke(renderer, node, shape, stroke, transform, pixelX,
                       pixelY);
}

ShapeBounds ButtonHitEnvelope(const ButtonData& data, const ShapeSpec& shape,
                              int width, int height) {
    // 每个状态**各自的尺寸**（T-5 的布局通道）：状态里没写就用组件自己的。
    // 尺寸不同 → 形状 AABB 与旋转中心都不同，所以这两样都得按状态现算。
    const auto stateSize = [&](const ButtonAppearance& appearance, bool isWidth) {
        const float override = isWidth ? appearance.width : appearance.height;
        return override > 0.0f
                   ? static_cast<int>(override + 0.5f)
                   : (isWidth ? width : height);
    };

    const auto transformedBounds = [&](const ButtonAppearance& appearance) {
        const int stateWidth = stateSize(appearance, true);
        const int stateHeight = stateSize(appearance, false);
        const TransformSpec& transform = appearance.transform;

        // 形状本身的 AABB：圆和椭圆不是整块矩形（`GetShapeBounds` 已经算准了）。
        const ShapeBounds local =
            GetShapeBounds(shape, stateWidth, stateHeight);
        // 绕**这个状态的**中心做变换：和绘制 / 命中用的是同一个原点
        // （`InkingTransform.h` 定的"原点取组件中心"，而中心随尺寸走）。
        const float centerX = static_cast<float>(stateWidth) * 0.5f;
        const float centerY = static_cast<float>(stateHeight) * 0.5f;

        // 四个角各过一次正变换，再取外接 AABB。旋转后的包围盒会比原来的大——
        // 这正是"保守"的含义：宁可多登记几个格子，也不能漏。
        const float xs[4] = {local.x, local.x + local.width, local.x,
                             local.x + local.width};
        const float ys[4] = {local.y, local.y, local.y + local.height,
                             local.y + local.height};

        float minX = 0.0f;
        float minY = 0.0f;
        float maxX = 0.0f;
        float maxY = 0.0f;
        for (int i = 0; i < 4; ++i) {
            const Point2 point = ApplyTransform(transform, xs[i], ys[i],
                                                centerX, centerY);
            if (i == 0) {
                minX = maxX = point.x;
                minY = maxY = point.y;
                continue;
            }
            minX = point.x < minX ? point.x : minX;
            minY = point.y < minY ? point.y : minY;
            maxX = point.x > maxX ? point.x : maxX;
            maxY = point.y > maxY ? point.y : maxY;
        }
        return ShapeBounds{minX, minY, maxX - minX, maxY - minY};
    };

    // **三个状态都要算**：悬停会平移+放大（还有 T-5 的尺寸变化）、按下会下沉。
    // 只按当前状态登记的话，"悬停那一帧按钮挪出去一点 / 长大一圈"边缘的点就落在
    // 表外了——那是漏命中，属于最难查的那种（偶尔点不中）。
    //
    // 尺寸**每帧插值**（中间态介于两端之间），所以这份并集仍然覆盖得住：
    // 中间尺寸的 AABB 一定落在两端 AABB 的并集里（三个状态共享同一个左上角锚点，
    // 见 `ButtonData.h` 里"从左上角生长"那条约定）——这就是**不用换表**的原因。
    const ButtonAppearance* states[3] = {&data.normal, &data.hover,
                                         &data.onclicked};

    ShapeBounds merged = transformedBounds(*states[0]);
    for (int i = 1; i < 3; ++i) {
        const ShapeBounds one = transformedBounds(*states[i]);
        const float maxX = merged.x + merged.width;
        const float maxY = merged.y + merged.height;
        const float oneMaxX = one.x + one.width;
        const float oneMaxY = one.y + one.height;

        const float minX = one.x < merged.x ? one.x : merged.x;
        const float minY = one.y < merged.y ? one.y : merged.y;
        const float outMaxX = oneMaxX > maxX ? oneMaxX : maxX;
        const float outMaxY = oneMaxY > maxY ? oneMaxY : maxY;
        merged = ShapeBounds{minX, minY, outMaxX - minX, outMaxY - minY};
    }
    return merged;
}

Point2 ButtonTextOffset(const InkingAnchor& node, const ButtonData& data) {
    const Point2 configured{data.text.leftSpace, data.text.topSpace};
    const bool needsMeasuring = data.text.align != TextAlignH::Left
                                || data.text.alignV != TextAlignV::Top;
    if (!needsMeasuring || data.label.empty()) {
        return configured;  // 老行为（默认路径，零开销）
    }

    // 量一遍真实文字块。`MeasureText` 不依赖渲染器：拿不到字体时给估算值，
    // 所以"居中"在没有字体子系统时也不会变成"从中间开始画"。
    TextRequest probe;
    probe.text = data.label;
    probe.fontSize = data.text.fontSize > 0.0f ? data.text.fontSize
                                               : kDefaultFontSize;
    probe.fontPath = data.text.path;
    probe.fontPaths = data.text.paths;
    const TextMetrics metrics = MeasureText(probe);
    if (metrics.width <= 0.0f && metrics.height <= 0.0f) {
        return configured;  // 量不出来就退回配置值，别硬算出一个怪位置
    }

    const float width = static_cast<float>(node.GetWidth());
    const float height = static_cast<float>(node.GetHeight());

    float x = data.text.leftSpace;
    switch (data.text.align) {
        case TextAlignH::Center:
            x = (width - metrics.width) * 0.5f;
            break;
        case TextAlignH::Right:
            x = width - metrics.width - data.text.leftSpace;
            break;
        case TextAlignH::Left:
        default:
            break;  // 保持 leftSpace
    }

    float y = data.text.topSpace;
    switch (data.text.alignV) {
        case TextAlignV::Middle:
            y = (height - metrics.height) * 0.5f;
            break;
        case TextAlignV::Bottom:
            y = height - metrics.height - data.text.topSpace;
            break;
        case TextAlignV::Top:
        default:
            break;  // 保持 topSpace
    }

    return Point2{x, y};
}

void PaintButtonText(const InkingAnchor& node, const ButtonData& data,
                     const TransformSpec& transform, float pixelX,
                     float pixelY) {
    SDL_Renderer* renderer = currentRenderer();
    if (renderer == nullptr) {
        return;
    }
    if (data.label.empty()) {
        return;  // 没配文字、或者运行期被 SetText("") 清掉：什么都不画
    }

    // 文字走**纹理层**（include/render/InkingText.h）：它负责栅格化一次、
    // 缓存、上色、以及跟着变换走。这里只把配置翻译成一次 TextRequest。
    TextRequest request;
    request.text = data.label;
    request.fontSize =
        data.text.fontSize > 0.0f ? data.text.fontSize : kDefaultFontSize;
    request.fontPath = data.text.path;
    request.fontPaths = data.text.paths;
    request.color = VisibleTextColor(data.textColor);
    // 位置：`align` / `alignV` 为默认值时就是配置里的 leftSpace / topSpace；
    // 写了居中 / 右对齐 / 贴底时先量一遍文字块再算（见 `ButtonTextOffset`）。
    const Point2 offset = ButtonTextOffset(node, data);
    request.offsetX = offset.x;
    request.offsetY = offset.y;

    PaintText(renderer, request, node.GetWidth(), node.GetHeight(),
              Magnify(node), pixelX, pixelY, transform);
}

}  // namespace inking::detail
