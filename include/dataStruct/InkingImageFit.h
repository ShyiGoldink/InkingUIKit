#pragma once

// 图片的**适配模式**（B1）：`img` 外观在组件框里怎么摆、按不按宽高比。
//
// 三种模式照 CSS `object-fit` 收窄到最常用的三个：
//   - `Fill`（**默认**，也是 B1 之前唯一的行为）：拉伸铺满，**不保持宽高比**；
//   - `Contain`：保持宽高比，整张图放进框里、居中，剩下的地方**不画**；
//   - `Cover`：保持宽高比铺满框、居中，超出框的一边**被框裁掉**。
//
// 为什么把"算"放在这里、而不是塞进绘制里：它是**纯几何**——给定框与图，
// 算出"图占哪一块""本地点对应哪个纹理坐标"。纯几何就能单测（源项目的
// `tests/test_image.cpp` 第一段就是它；**那套件未搬入本仓库**），而绘制那边只
// 负责拿这些数去填顶点。三份实现各自演化就会出现
// "contain 的留白算了、cover 的裁边忘了"这种只在某个模式下露头的错。
//
// 单位：全部是**设备像素**。调用方（纹理层）已经把设计坐标乘过展示倍率了。

#include <dataStruct/InkingShapeSpec.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace inking {

/// 图片适配模式（配置里写 `"fit": "fill" | "contain" | "cover"`）。
enum class ImageFit : std::uint8_t {
    Fill = 0,  ///< 拉伸铺满（默认）
    Contain,   ///< 完整放进框、居中
    Cover,     ///< 铺满框、居中、裁掉超出
};

/**
 * @brief 一次"把图放进框"的完整计算（纯数据 + 纯函数，不碰渲染器）。
 *
 * 用法（纹理层）：`ImageFitSpec{fit, 框宽, 框高, 图宽, 图高}` → `DrawnRect()` 知道
 * 图占哪块；生成顶点时每个点问 `UvAt()` 与 `CoverageAt()`。
 */
struct ImageFitSpec {
    ImageFit fit = ImageFit::Fill;
    float frameWidth = 0.0f;   ///< 组件框宽（设备像素）
    float frameHeight = 0.0f;  ///< 组件框高
    float imageWidth = 0.0f;   ///< 纹理像素宽（**整张**纹理，UV 的分母）
    float imageHeight = 0.0f;  ///< 纹理像素高（整张）

    /**
     * @brief **这一帧**在纹理里的像素矩形（图集切格，0.6.1 T-04）。
     *
     * 两条语义必须分清，混了就会"图集播放时比例对、位置错"：
     *   - `imageWidth/imageHeight` = **整张纹理**尺寸，只当 UV 的分母；
     *   - `srcRect` = 这次要画的那块（宽高可以远小于整张）。
     *
     * **全零 = 整张纹理**。这是既有图片（非图集）的路径，一个字节的行为都不变。
     */
    ShapeBounds srcRect{};

    /// 给过源矩形吗（`srcRect` 宽高都 > 0）。
    bool HasSourceRect() const noexcept {
        return srcRect.width > 0.0f && srcRect.height > 0.0f;
    }

    /// 归一化后的源矩形（像素）：没给就是整张纹理。
    ShapeBounds SourceRect() const noexcept {
        if (!HasSourceRect()) {
            return ShapeBounds{0.0f, 0.0f, imageWidth, imageHeight};
        }
        return srcRect;
    }

    bool Valid() const noexcept {
        return frameWidth > 0.0f && frameHeight > 0.0f && imageWidth > 0.0f
            && imageHeight > 0.0f && SourceRect().width > 0.0f
            && SourceRect().height > 0.0f;
    }

    /**
     * 图在框里占的那块矩形，**相对框左上角**（设备像素）。
     *
     * - `Fill`：就是整个框（拉伸）；
     * - `Contain`：等比缩小到装得下，居中（宽或高有一边会留白）；
     * - `Cover`：等比放大到盖满，居中（**起点会是负数**，多出来的部分在框外，
     *   由形状裁剪负责切掉——这正是"cover 的裁边"和"形状裁剪"合流的地方）。
     *
     * **等比算的是"这一帧"的宽高（`SourceRect()`），不是整张纹理**：图集里的
     * 一格是 40×40、整张是 160×40，拿整张去算比例会让 `Contain` / `Cover`
     * 的留白和裁边全错（帧被当成宽高比 4:1 的超宽图）。
     */
    ShapeBounds DrawnRect() const noexcept {
        if (!Valid()) {
            return ShapeBounds{0.0f, 0.0f, 0.0f, 0.0f};
        }
        const ShapeBounds source = SourceRect();
        float drawnWidth = frameWidth;
        float drawnHeight = frameHeight;
        if (fit != ImageFit::Fill) {
            const float byWidth = frameWidth / source.width;
            const float byHeight = frameHeight / source.height;
            const float scale = (fit == ImageFit::Contain)
                                    ? std::min(byWidth, byHeight)
                                    : std::max(byWidth, byHeight);
            drawnWidth = source.width * scale;
            drawnHeight = source.height * scale;
        }
        return ShapeBounds{(frameWidth - drawnWidth) * 0.5f,
                           (frameHeight - drawnHeight) * 0.5f, drawnWidth,
                           drawnHeight};
    }

    /**
     * 本地点（相对框左上角，设备像素）→ 纹理坐标 `(u, v)`。
     *
     * 注意它算的是"这个点落在图的哪个位置"，所以：
     *   - `Fill` / `Contain`：u、v 在 [0,1] 内（图区域外会超出，那部分由
     *     "形状 ∩ 图区域"裁掉，见下）；
     *   - `Cover`：u、v 天然落在 [0,1] 内（图比框大，框只取中间一段）。
     *
     * 有 `srcRect` 时先算"帧内归一坐标"，再映射到整张纹理上：
     * `u = (src.x + u01 × src.width) / imageWidth`。
     */
    void UvAt(float x, float y, float& u, float& v) const noexcept {
        const ShapeBounds rect = DrawnRect();
        if (rect.width <= 0.0f || rect.height <= 0.0f) {
            u = 0.0f;
            v = 0.0f;
            return;
        }
        const float u01 = (x - rect.x) / rect.width;
        const float v01 = (y - rect.y) / rect.height;
        if (!HasSourceRect()) {
            // 整张纹理：**直接给**，不绕 `(u01 × W) / W` 那一圈——
            // 二进制里那不是恒等，会让"没有 srcRect 的图片"多出一个 ulp 的抖动，
            // 逐像素对拍从此不可靠。
            u = u01;
            v = v01;
            return;
        }
        const ShapeBounds source = srcRect;
        u = (source.x + u01 * source.width) / imageWidth;
        v = (source.y + v01 * source.height) / imageHeight;
    }

    /**
     * `Contain` 才有意义：**图区域**（相对框左上角）。图区域外那圈留白不画纹理
     * ——实现是"形状 ∩ 图区域"（绘制时按它设一次裁剪矩形，见
     * `detail::fillShapeTextured`），而不是逐顶点透明度。
     *
     * 为什么不做成逐顶点 alpha：形状网格的顶点只落在**轮廓**上（矩形就四个角 +
     * 一个中心），而图区域的边界在形状内部——用顶点插值表达不出来（实测：
     * contain 的留白会变成整块半透明，而不是干净的边界）。
     */
    ShapeBounds ImageArea() const noexcept { return DrawnRect(); }
};

}  // namespace inking
