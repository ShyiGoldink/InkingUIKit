#pragma once

// 批量四边形图层（dev010 T-02）：一个节点，一帧提交 N 个带纹理的四边形。
//
// 为什么需要它：一帧要画几千个精灵，又不能为每个精灵建一个场景节点。
// 实测（任务书 §2.3）：5000 个 `InkingStaticImage` 每帧 4.8 ms，"1 个图层 +
// 5000 个 quad" 是 **0.85 ms**。省下来的**不是**节点框架——空组件 5000 个只要
// 0.10 ms——而是每个组件在 `onRender` 里重复干的那些活：查纹理表、算 fit、
// 逐帧重建网格。
//
// 为什么是**组件**而不是 `SubmitQuads()` 自由函数：
//
//   提交必须落在场景的 `BeginFrame` / `EndFrame` 之间，而且**顺序由树序决定**
//   （`InkingBatch.h` 开头那三条纪律）。自由函数把"什么时候调"交给调用方——
//   调早了一帧就静默丢弃，调错顺序就是"画的和点的不一致"。挂在一棵树上之后，
//   顺序、祖先变换、裁剪栈全部由现有机制负责，图层自己只管"把这批顶点交出去"。
//
// 坐标系：`InkingQuad::dst` 是**设计坐标、相对图层左上角**。把图层挂在
// `(0,0)` 时它就等于世界坐标。图层跟着父容器动（走祖先仿射），和别的组件一致。
//
// 命中：图层**不进命中表**（构造期就置了 pass-through）。几千个物体各占一条
// 命中记录正是这条要消掉的东西；要点击就在业务侧拿 `InkingShapeContains` 判，
// 那本来就是公开的（`dataStruct/InkingShapeSpec.h`）。

#include <asset/InkingTexture.h>
#include <dataStruct/InkingNinePatch.h>
#include <dataStruct/InkingShapeSpec.h>
#include <widget/InkingStaticWidget.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/// 这个模块的名字（日志里用；和别的模块一个约定）。
inline constexpr const char* InkingGeometryModuleName = "InkingGeometryLayer";

/**
 * @brief 一个带纹理的四边形。
 *
 * 字段顺序按"最常改的放前面"排没必要——它是 POD，业务侧一般整块填。
 */
struct InkingQuad {
    /// 纹理句柄（`InkingLoadTexture` 的返回值）。无效句柄会被**跳过**，不整层不画。
    InkingTextureHandle texture;

    /// 取纹理的哪一块（**像素**）。宽高全 0 = 整张纹理（与图集之前的老口径一致）。
    InkingShapeBounds src;

    /// 画在哪（**设计坐标**，相对图层左上角）。
    InkingShapeBounds dst;

    /// 调制色 0xAARRGGBB。默认全白 = 不调制；alpha 参与混合（可以做淡入淡出）。
    std::uint32_t tint = 0xFFFFFFFFu;

    /// 绕 `dst` 中心旋转的角度（**度**，顺时针为正，与 `InkingTransformSpec` 同向）。
    /// 0 = 不转。非 0 时形状仍是四边形（不会为旋转引入额外顶点）。
    float rotation = 0.0f;
};

/**
 * @brief 一个任意顶点（`SetGeometry` 用）。
 *
 * 与 `InkingQuad` 的分工：quad 是"一张贴图按矩形摆"，走最短路径；vertex 是
 * "我自己算好了三角形"，用来做遮罩、自定义拓扑、变形这些 quad 表达不了的东西。
 *
 * 两个坐标口径，**混了就会画歪**：
 *   - `x/y` 是**设计坐标、相对图层左上角**（和 `InkingQuad::dst` 同一套）；
 *   - `u/v` 是 **0~1 归一化**（SDL 的 `tex_coord` 语义，**不是像素**；
 *     写成像素的表现是"整块都是纹理的第一个像素"）。
 */
struct InkingVertex {
    float x = 0.0f;
    float y = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    /// 0xAARRGGBB。alpha 参与混合（遮罩、淡出都靠它）。
    std::uint32_t color = 0xFFFFFFFFu;
};

/**
 * @brief 一帧提交 N 个四边形的图层。
 *
 * 用法：
 *
 * ```cpp
 * auto* layer = static_cast<InkingGeometryLayer*>(
 *     scene.AddChild(std::make_unique<InkingGeometryLayer>(&scene, "sprites")));
 * // 每帧（或物体变化时）重填一批：
 * layer->SetQuads(quads.data(), quads.size());
 * ```
 *
 * `SetQuads` 是**整段替换**（不是追加）：几千个物体的位置每帧都在变，
 * 追加语义会让数组无限涨。要追加用 `AppendQuads`，两者各有各的名字。
 */
class InkingGeometryLayer : public InkingStaticWidget {
public:
    /**
     * @param parent 上级锚点；非拥有。
     * @param name   节点名（`FindByName` / 日志用）。
     */
    InkingGeometryLayer(InkingWidget* parent, const std::string& name);

    /// 整段替换。`count == 0` 等价于 `ClearQuads()`。会标重绘脏。
    void SetQuads(const InkingQuad* quads, std::size_t count);

    /// 同上，接受现成数组（拷贝一份：调用方的数组通常每帧重建）。
    void SetQuads(const std::vector<InkingQuad>& quads);

    /// 追加。同样拷贝，同样标脏。
    void AppendQuads(const InkingQuad* quads, std::size_t count);

    /// 清空（下一帧这一层什么都不画）。
    void ClearQuads() noexcept;

    /// 现在有几块。
    std::size_t GetQuadCount() const noexcept;

    /**
     * @brief 因为**句柄失效**被跳过多少块（累计）。
     *
     * 为什么单独一个口：一层里几千个物体，一个坏句柄让整屏黑掉是最糟的失败方式，
     * 所以选择"跳过并记账"而不是"整层不画"。这个数是唯一能发现"某几个物体
     * 一直没画出来"的观察口（`AGENTS.md` §6 第 40 条）。
     */
    std::uint64_t GetSkippedQuadCount() const noexcept;

    /**
     * @brief 直接提交一批**自己算好的顶点**（与 `SetQuads` **互斥**）。
     *
     * 调用它就等于"这一层改走任意几何模式"，之前 `SetQuads` 设的四边形会被清掉；
     * 反过来 `SetQuads` 也会清掉几何。两套同时设置是没有意义的，与其让调用方
     * 猜"到底画哪个"，不如让后者赢——这条在 `CheckGeometryLayer()` 里有断言。
     *
     * 三条硬约束（违反的整次调用被拒，**不部分提交**）：
     *
     * | 约束 | 为什么 | 违反时 |
     * | - | - | - |
     * | `count <= 16384` | 提交层的单批上限（`kMaxBatchVertices`） | 拒绝 + ERROR + 计数 |
     * | 每个索引 `< count` | 越界索引画出来是随机的碎片，比不画难查得多 | 拒绝 + ERROR + 计数 |
     * | `indexCount % 3 == 0` | 提交的是三角形列表 | 拒绝 + ERROR + 计数 |
     *
     * 拒绝是**整次**的：截断一半的三角形列表会画出"少一块但看着像对的"画面。
     *
     * @return 接受了才 true。被拒时这一层保持上一次的内容不变。
     */
    bool SetGeometry(InkingTextureHandle texture, const InkingVertex* vertices,
                     std::size_t vertexCount, const std::uint16_t* indices,
                     std::size_t indexCount);

    /**
     * @brief 按一个 SDF 形状把一块图**裁**出来再提交（遮罩的便捷入口）。
     *
     * 等价于"`InkingBuildMaskedQuad(...)` 然后 `SetGeometry(...)`"，只是纹理尺寸
     * 由这一层自己去问，调用方不用记。几何细节与代价见 `image/InkingMask.h`。
     *
     * @param maskWidth,maskHeight 遮罩铺开的尺寸（通常等于 `quad.dst` 的宽高）。
     * @param density 每边细分几格（默认 16 ⇒ 289 顶点）；上限 `InkingMaskMaxDensity`。
     * @return 提交成功才 true（密度被夹不算失败，见 `InkingMaskedQuad::clamped`）。
     */
    bool SetMaskedQuad(InkingTextureHandle texture, const InkingQuad& quad,
                       const InkingShapeSpec& mask, float maskWidth,
                       float maskHeight, int density = 16);

    /**
     * @brief 九宫格：把一张图按边框切成 9 块再提交（边框不变形）。
     *
     * 等价于"`InkingExpandNinePatch(...)` 然后 `SetQuads(...)`"，只是把 tile
     * 转成 quad 的琐碎字段搬进这一层。几何与夹取规则见 `dataStruct/InkingNinePatch.h`。
     *
     * @return 目标或源退化（宽高 <= 0）时 false，且这一层的内容保持不变。
     */
    bool SetNinePatch(InkingTextureHandle texture, const InkingQuad& quad,
                      const InkingNinePatchBorder& border);

    /// 现在提交了多少个顶点（四边形模式下恒 0）。
    std::size_t GetVertexCount() const noexcept;

    /// 因为约束被拒了多少次（累计）。
    std::uint64_t GetRejectedGeometryCount() const noexcept;

    /// 现在是不是几何模式（而不是四边形模式）。
    bool IsGeometryMode() const noexcept;

    /// 当前这批（只读；给自检与调试用）。
    const std::vector<InkingQuad>& GetQuads() const noexcept;

protected:
    void onRender(float pixelX, float pixelY) const override;

private:
    void renderQuads(float pixelX, float pixelY) const;
    void renderGeometry(float pixelX, float pixelY) const;

    std::vector<InkingQuad> _quads;
    std::vector<InkingVertex> _vertices;
    std::vector<int> _indices;  ///< 提交层吃 `const int*`，这里就存 int，省一次转换
    InkingTextureHandle _geometryTexture;
    bool _useGeometry = false;
    mutable std::uint64_t _skippedQuads = 0;
    mutable std::uint64_t _rejectedGeometry = 0;
};
