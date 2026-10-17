#pragma once

// 文字纹理层：**把一段文字变成一张纹理**，并缓存起来。
//
// 为什么单独一层（`TaskGuide.md` T-3）：
//   - 渲染器只认纹理，不认字符串。"文字 → 像素"这件事得有人烘一次、之后复用，
//     每帧重新栅格化一遍纯属烧时间，而"什么时候该重烘"是个容易漏的判断；
//   - 它同时服务三类消费者：按钮上的文字（现在）、T-4 的 `img` / `svg`
//     （同样是"画一张纹理"）、以及将来的 `TextBox`（**只留接口**，见文件末尾）；
//   - 缓存与失效收在一处，才不会出现"某条路径忘了失效"。
//
// 与 SDL 的关系：这一层**要** SDL（纹理、渲染器、SDL3_ttf），所以它和
// `InkingDraw.h` 一样是**框架内部头**——别把它塞进公开的组件头里
// （公开头不拖 SDL 进来，见 `AGENTS.md` §6 第 8 条）。
//
// SDL3_ttf 是**可选**依赖：没接上、或者机器上一份字体都找不到时，这一层退化成
// "占位方块"（改动之前的老画法），于是关掉 TTF 也能编、也能跑，只是文字还是个块。

#include <dataStruct/InkingColor.h>
#include <dataStruct/InkingImageFit.h>
#include <dataStruct/InkingShapeSpec.h>
#include <dataStruct/InkingTextRequest.h>
#include <dataStruct/InkingTransform.h>

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>

namespace inking {

// `TextRequest` / `TextMetrics` 已经搬到公开的**纯数据**头
// `dataStruct/InkingTextRequest.h`（A12：画布的公开绘制词汇要用它，而本文件
// 是内部头、不能进公开头）。本文件照旧把它们带出来，调用方一处 include 都不用改。

/**
 * @brief 量一段文字在给定字号下占多大。
 *
 * 为什么它必须在**这一层**、而不是藏在某个组件里：按钮将来要按标签自适应、
 * `TextBox` 要撑自己的高度、行高与居中都要问尺寸——谁都能问到，才不会各写
 * 一份"字号 × 字数"的估算（那种估算在中英文混排下必然对不上）。
 *
 * 拿不到字体时（没编 SDL3_ttf / 找不到字体 / 字号 ≤ 0）返回**估算值**
 * （按字号的方块：宽 = 字号 × 码点数，高 = 字号），与绘制那边的退化画法一致。
 * 不依赖渲染器，可以单独调。
 */
TextMetrics MeasureText(const TextRequest& request);

/**
 * @brief 一张图片外观的**字节视图**（T-4）。
 *
 * 不拥有数据：指向生成代码里那个静态数组（见 `ButtonAppearance::imageBytes`）。
 * `name` 是配置里写的那个路径，只用来做**缓存键**与日志——同一个文件被多个
 * 按钮引用时共用一张纹理。
 */
struct ImageBytes {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    std::string name;

    bool Empty() const noexcept { return data == nullptr || size == 0; }
};

}  // namespace inking

namespace inking::detail {

/**
 * @brief 纹理层的自检观测口。
 *
 * "缓存有没有生效"这件事，功能上完全看不出来（画出来一模一样，只是白干活），
 * 只有计数抓得住——`AGENTS.md` §6 第 40 条那条教训。
 */
struct TextCacheStats {
    std::uint64_t texturesCreated = 0;  ///< 真的栅格化 + 建纹理的次数
    std::uint64_t cacheHits = 0;        ///< 直接复用现成纹理的次数
};

TextCacheStats TextCacheStatsSnapshot() noexcept;

/**
 * @brief 画一段文字。这是文字**唯一的绘制入口**（所有组件都走它）。
 *
 * @param renderer      渲染器；nullptr 是空操作。
 * @param request       文字 + 字号 + 字体 + 颜色。
 * @param width,height  组件尺寸，**设计坐标**：变换（旋转 / 缩放）绕它的中心，
 *                      和 `fillShape` 同一套语义。
 * @param pixelX,pixelY 组件左上角，**设备像素**（和 `fillShape` 的入参同一套语义）。
 * @param magnification 展示倍率：纹理按 `fontSize × magnification` **设备像素**
 *                      栅格化（不然放大就糊），所以缓存键里带的是这个乘积。
 * @param transform     变换通道（平移 / 旋转 / 缩放），绕**组件中心**应用——
 *                      和形状那边同一个原点，所以文字跟着按钮一起转。
 *
 * 颜色走**纹理调制**：纹理本身烘成白字，画的时候用 `SDL_SetTextureColorMod`
 * 上色。于是同一句话换颜色**不用重烘纹理**（这是"颜色不进缓存键"的原因，
 * 与 `TaskGuide` T-3 那条"换色要重画"的要求不冲突：要求的是**像素结果**对，
 * 自检里验的正是这个）。
 *
 * 调用线程：绘制全程在 UI 线程，与 `fillShape` 一样不加锁。
 */
void PaintText(SDL_Renderer* renderer, const TextRequest& request, int width,
               int height, float magnification, float pixelX, float pixelY,
               const TransformSpec& transform);

/**
 * @brief 画一张图片（`img` 外观）：**按形状裁剪**、跟着变换走。
 *
 * 四个决定写在这里，免得以后反复猜：
 *   - **解码时机**：首次绘制时解码一次、进同一张纹理表（进程启动什么都不做——
 *     没显示出来的图不该占用启动时间）；
 *   - **裁剪**：**按组件形状裁剪**（B1）。它走的是形状层同一条网格生成
 *     （`fillShapeTextured`），所以圆角按钮配图片时，图片四个角跟着圆角一起被剪，
 *     边缘那一圈也是同一条抗锯齿过渡；
 *   - **适配模式**：`fit` 决定图在框里怎么摆——`fill`（拉伸铺满，默认）、
 *     `contain`（完整放进框、居中，留白处不画）、`cover`（铺满框、超出的一边被
 *     框裁掉）。三种模式的几何计算在 `ImageFitSpec`（纯函数，可单测）；
 *   - **变换**：与形状同一套语义（绕组件中心），所以文字 / 形状 / 图片三笔
 *     跟着容器一起动。
 *
 * @param shape     组件形状（渲染 / 命中 / AABB 都从它派生）。
 * @param fit       适配模式（配置字段 `"fit"`，见 `CXXCSS.md` §4.1）。
 * @param tintColor 只取它的 **alpha**：颜色过渡时用它做淡入淡出（图片本身不换色）。
 *                  解码不出来（没接 SDL3_image / 字节坏了）时返回 false，
 *                  调用方可以退回"按颜色填形状"。
 * @return 真的画出了图片返回 true。
 */
bool PaintImage(SDL_Renderer* renderer, const ImageBytes& image,
                const ShapeSpec& shape, int width, int height,
                float magnification, float pixelX, float pixelY,
                const TransformSpec& transform, ImageFit fit,
                std::uint32_t tintColor);

/**
 * @brief 放掉这一层持有的全部纹理与字体。**渲染器销毁之前必须调**。
 *
 * 纹理属于渲染器：渲染器一销毁，句柄就成了野指针——那时再 `SDL_DestroyTexture`
 * 是 use-after-free。所以顺序是"先 Release，再 DestroyRenderer"。
 * 幂等，可以重复调。同时会 `TTF_Quit`（与内部那次 `TTF_Init` 配对）。
 *
 * 自检里会反复建 / 销毁渲染器，所以这一层**自己**还有一道保险：发现渲染器换了
 * 人，就把缓存整个丢掉（只丢自己的记账，不去动那些已经随渲染器死掉的纹理）
 * 并且每帧用 `SDL_GetTextureSize` 复核一次句柄还有效。
 */
void ReleaseTextCache() noexcept;

}  // namespace inking::detail

// ---------------------------------------------------------------------------
// `TextBox` 组件的**预留接口**（这一轮只留形状，不接实现）
//
// 为什么不做实现（`TaskGuide.md` T-3 的范围修订）：文本框以后要接**多语言**，
// 那是"字符串从哪来"的另一套配置（占位符 → 各语言的串 → 语言切换时机）。
// 现在把它的数据面定死，等于给那一轮预埋一次推翻。
//
// 所以这里**故意不给出组件类**——空的类比没写更糟（会被当成能用的）。
// 留给那一轮的东西是这份契约：
//
//   1. 它拿 `TextRequest` 当数据面（文本 / 字号 / 字体 / 颜色），
//      用 `MeasureText` 量尺寸、用 `detail::PaintText` 画自己；
//   2. 多语言**换的是上游**：`TextRequest::text` 从"配置里烘死的串"变成
//      "按当前语言查出来的串"。这一层不关心它从哪来，只认最后那个串——
//      所以纹理层不会被多语言的设计推翻（这也是它现在就能落地的原因）；
//   3. 语言切换 = 换一串 `text` → 缓存自然未命中（键里有文本）→ 重烘一次。
//      真要"切语言时不掉帧"，那一轮再谈预烘（把各语言的串都烘一遍）——
//      那是缓存策略，不是这一层的契约；
//   4. 它**默认不吃点击**（`SetHitPassThrough(true)`；基类默认是 `Block`）：
//      标签压在按钮上时不该把点击吃掉。
// ---------------------------------------------------------------------------
