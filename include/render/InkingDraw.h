#pragma once

// 绘制助手：**框架内部共用**，别在业务代码里直接用。
//
// 放在 include/ 下而不是 src/ 下，是因为 include/ 是唯一的头文件搜索根：
// 内部头放 src/ 就得再挂一个 PRIVATE include 目录，而它 include 的 SDL
// 反正已经由 ink_core PUBLIC 传播（SDL3::Headers），不差这一份。
//
// 为什么要有这个文件：`InkingWidget::SubmitToRenderer` 里的 `onRender` 签名
// 只有坐标、没有渲染器——不希望每个组件实现都被迫多接一个参数。所以提交前
// 把渲染器挂到"当前渲染器"上，`onRender` 期间从槽里取。
//
// 这套槽 + 取色的样板原先只长在 InkingAnchor.cpp 里，Button 要按形状填色时
// 就得再抄一遍。抄第二遍的时候抽出来：两份实现各自演化，迟早会分叉
// （例如一份处理了空渲染器、另一份没处理）。

#include <dataStruct/InkingColor.h>
#include <dataStruct/InkingAffine.h>
#include <dataStruct/InkingImageFit.h>
#include <dataStruct/InkingShapeSpec.h>
#include <dataStruct/InkingTransform.h>

#include <SDL3/SDL.h>

#include <cstdint>

namespace inking::detail {

/**
 * 当前渲染器槽位：提交前设置、提交后清成 nullptr。
 *
 * **槽本体定义在 `src/render/InkingDraw.cpp`，头里只有声明**：它是"整个进程
 * 只有一份"的存储，而头内 `inline` 函数里的函数内静态量在**每个翻译单元各
 * 有一份**——静态库时代无所谓（最终只有一个可执行映像），但本仓库要出
 * **动态库**：exe 与 DLL 各持一份时，"外面设置渲染器、DLL 里的绘制读不到"
 * 会**静默**发生（不报错，只是画不出来）。
 *
 * 槽本体仍是函数内静态量（避开静态初始化顺序问题，AGENTS §6 第 12 条），
 * 只是那个函数现在定义在 .cpp 里。绘制全程在 UI 线程，不需要加锁。
 */
SDL_Renderer*& currentRendererSlot();

/** 当前渲染器；不在提交过程中时为 nullptr。 */
inline SDL_Renderer* currentRenderer() {
    return currentRendererSlot();
}

/**
 * 当前节点的"**祖先子树变换**"槽位（T-7）。
 *
 * `SubmitToRenderer` 在 `onRender` 之前算好（`detail::AncestorAffine(*this)`，
 * 设备像素），绘制完成后清掉。形状填充 / 文字 / 图片都读它——这样"容器动了、
 * 子树跟着动"不需要每个组件各写一遍，也不用改任何组件的签名。
 *
 * 作用点在**提交层**（`AppendLocalGeometry`）：顶点缓存里存的是**本地**坐标，
 * 所以容器移动既不作废缓存、又能让整棵子树跟着走。
 *
 * 与 `currentRendererSlot()` 同理，槽本体定义在 `src/render/InkingDraw.cpp`
 * ——动态库下 exe / DLL 必须共用同一份，否则"组件在 DLL 里读的变换"会和
 * "提交层在别处写的"不是同一个变量。
 */
Affine& currentAncestorAffineSlot();

inline Affine currentAncestorAffine() {
    return currentAncestorAffineSlot();
}

/** 0xAARRGGBB → SDL 的绘制颜色。 */
inline void setDrawColor(SDL_Renderer* renderer, std::uint32_t color) {
    SDL_SetRenderDrawColor(renderer, ColorRed(color), ColorGreen(color),
                           ColorBlue(color), ColorAlpha(color));
}

/**
 * @brief 按形状填一块，边缘**抗锯齿**——这是唯一的填充入口。
 *
 * 形状是唯一真相：几何取的是 `InkingShapeSpec.h` 那一份定义，
 * 和命中用的 `ShapeContains` 同源，所以"画的是圆角、点到的是直角"
 * 这条已知不一致在这里被消掉（早先那个只填包围盒的 `fillShapeBounds`
 * 已经删掉，调用方一处都没改：两条调用都换成了本函数）。
 *
 * 怎么画的：把形状的轮廓解析地采样成多边形，用 `SDL_RenderGeometry`
 * 三角化提交——**不是 CPU 逐像素光栅化**（docs/AGENTS.md §3 第 3 条）。
 * 边界那一圈再切出一条约 1 像素宽的带子：带子外圈顶点 alpha = 0、
 * 内圈顶点 alpha = 源色 alpha，于是边界上是 0→1 的线性过渡。
 *
 * 成立的前提是**绘制混合模式是打开的**：窗口层开过一次
 * （`InkingWindow::Show`），离屏自检也要自己开（AGENTS §6 第 32 条）。
 * 实测（software 渲染器）：`SDL_RenderGeometry` 的逐顶点 alpha 确实参与
 * 混合——alpha=0.5 的蓝叠在不透明红上读出 `0xFF7F0080`；同一三角形里
 * 两个顶点 alpha 不同时中间会插值（`0xFFE41B00` → `0xFF08EE08`）。
 * 混合关掉时 alpha 会被原样写进像素（读出 `0x80FFFFFF`），那时抗锯齿
 * 只剩"半个像素被涂成不透明色"——所以别把混合当可选项。
 *
 * 实现见 `src/render/InkingDraw.cpp`；形状 → 轮廓那一段在
 * `include/render/InkingMesh.h`（纯几何，不碰 SDL）。
 *
 * @param renderer      渲染器；传 nullptr 是空操作。
 * @param shape         形状定义（`rect` / `roundedRect` / `circle` / `ellipse`
 *                      / `capsule` / `ring` / `line` / `polygon`）。
 * @param width,height  组件尺寸，**设计坐标**。
 * @param magnification 展示倍率（设计单位 → 设备像素）；几何与羽化带都乘它。
 * @param pixelX,pixelY 左上角，**设备像素**（调用方已经换算过）。
 * @param transform     变换通道（平移 / 旋转 / 缩放），绕组件中心应用；
 *                      传 `TransformSpec{}` 就是不动。它**只动这一笔的位置与朝向**，
 *                      不改形状、尺寸，也不改命中表（docs/InputDesign.md §11）。
 * @param color         填充色 0xAARRGGBB；alpha 会参与合成。
 */
void fillShape(SDL_Renderer* renderer, const ShapeSpec& shape, int width,
               int height, float magnification, float pixelX, float pixelY,
               const TransformSpec& transform, std::uint32_t color);

/**
 * @brief 沿形状**边界**画一圈描边（B3）：宽度一半在形状外、一半在形状内。
 *
 * 与 `fillShape` 共用同一条轮廓采样（`AppendContour`），只是它取的是
 * "形状外扩 w/2"与"形状内缩 w/2"两条轮廓之间的**环带**——没有中心扇形，
 * 也不加中心点。于是：
 *   - 描边跟着形状走（圆角矩形的描边也圆角、跟着变换转）；
 *   - 描边**不参与命中**（命中判的是形状本体，`ShapeContains`）。
 *
 * 两条实现上的取舍：
 *   - **`ring` / `arc` 不描边**：它们本身就是一条细带（两条边界），"沿边界描边"
 *     会变成四条线；现在直接不画（配置里写了也不报错，见 `UsageReport.md`）；
 *   - **不进顶点缓存**：缓存键要再带描边宽度与颜色，而一个节点只有一个缓存槽。
 *
 * @param strokeWidth 描边宽度，**设计坐标**（内部乘展示倍率）；<= 0 是空操作。
 */
void strokeShape(SDL_Renderer* renderer, const ShapeSpec& shape, int width,
                 int height, float magnification, float pixelX, float pixelY,
                 const TransformSpec& transform, float strokeWidth,
                 std::uint32_t color);

/**
 * @brief 按形状填一张**纹理**（B1）：形状怎么剪，纹理就怎么被剪。
 *
 * 与 `fillShape` 的关系：**同一份形状网格生成**（`BuildShapeMesh`）——轮廓采样、
 * 三角化、边缘羽化都一样，区别只有两条：
 *   1. 每个顶点多带一个**纹理坐标**（由 `fit` 从本地坐标算出来）；
 *   2. 提交时带纹理（`SDL_RenderGeometry` 传 texture），而不是纯顶点色。
 *
 * 于是"圆角按钮配图片"这件事自然成立：圆角外的顶点根本不生成，
 * 图片的四个角跟着形状一起被剪掉，而边缘那一圈羽化仍然是同一条 0→1 过渡。
 *
 * 三条实现上的取舍：
 *   - **不走几何批**：批里只有"无纹理"顶点。它会先 `NoteImmediateDraw()` 把批刷出去
 *     （纹理绘制本来就是批次边界，AGENTS §6 第 49 条）；
 *   - **不进顶点缓存**：缓存键得再带纹理句柄 + 适配模式 + 图尺寸，而一个节点只有
 *     一个缓存槽（形状与纹理会互相顶掉）。那是"静态烘焙第二档"那一轮的事；
 *   - **位置换算与批次那条路共用**（`ApplyAncestorAffineToVertices`）：容器动了，
 *     纹理这一笔也跟着走——两份各写一遍就会出现"颜色跟着动、图片留在原地"。
 *
 * @param texture   已经建好的纹理（调用方负责解码与缓存，见 `PaintImage`）。
 * @param fit       适配模式 + 框尺寸 + 图尺寸（`ImageFitSpec`）。它决定 UV 与
 *                  `contain` 的留白。
 * @param tintColor 只取 alpha：过渡时做淡入淡出（图片本身不换色）。
 * @param hasAlpha  这张纹理带不带 alpha 通道（**调用方从建纹理时的 surface
 *                  格式记下来**，别在这里问 `SDL_GetTextureProperties`——见
 *                  `InkingText.cpp` 里 `TextureEntry::hasAlpha` 的注释）。
 *                  它只影响**矩形 + Fill** 那条快路：带 alpha 的图必须绕开
 *                  几何批直接贴图，否则透明区会被画成黑色。
 */
void fillShapeTextured(SDL_Renderer* renderer, const ShapeSpec& shape, int width,
                       int height, float magnification, float pixelX,
                       float pixelY, const TransformSpec& transform,
                       SDL_Texture* texture, bool hasAlpha,
                       const ImageFitSpec& fit, std::uint32_t tintColor);

}  // namespace inking::detail
