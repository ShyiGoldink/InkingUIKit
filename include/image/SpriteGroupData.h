#pragma once

// 块叠加组的**纯数据**定义（0.9.0 T-04）。
//
// ---------------------------------------------------------------------------
// 它解决的是另一件事，别和 `InkingSpriteData` / `InkingAtlasData` 混
//
//   - `InkingSpriteData`（单 sprite）：一帧**一块**。图集可以等格切，也可以给
//     逐帧矩形表（`frameRects`，宽高可以不等）。
//   - **本类型（块叠加组）**：一帧**多块**，全部来自同一张图集的等格。`frames`
//     每项是一串**格号**，顺序就是叠加顺序（先画的在下面）。
//   - `InkingAtlasData`：帧是矩形表，**宽高可以不等**——与"等格"正好相反。
//
// 所以这里有一个刻意的取舍：**等大是结构保证，不是运行期校验**。只给网格
// （`frameWidth/Height` + `columns/rows` + `margin/spacing`），不给矩形表，
// 于是"同一帧里两块大小不同"这种事**根本配不出来**。要么接受等格，要么用
// `InkingSpriteData` 的 `frameRects`（但那是一帧一块）。
//
// 三个类型并存时必须靠这段注释分清，否则下一个人会问"为什么不能给每个格子
// 单独尺寸"——答案是这个类型代表的"一个 sprite 由多块拼成"在业务上就是等格
// 叠图（身体 + 头 + 武器都在同一套网格里）。
//
// ---------------------------------------------------------------------------
// 三条定死的语义（写在前面，免得后面反复讨论）
//
//   1. **格号允许重复**：同一格可以在同一帧出现两次（故意叠两层做发光）。
//      不查重——查重会把这种用法挡掉，而它并不错。
//   2. **每帧块数允许不同**：不强制槽位固定。配置省一半，代价是顶点数每帧可变
//      （反正每块都要各自 `BuildShapeMesh` 一次）。
//   3. **格号越界归 `Validate()`，不夹取**。"改到哪一格"猜不出来。
//
// ---------------------------------------------------------------------------
// 与"帧事件"的关系：`events` 的帧号是**动画自己的帧号**（0..frames.size()-1），
// 与 `InkingSpriteData::SpriteEvent` 同一个类型、同一套语义（`(previous, current]`
// 区间扫描、慢帧不吞事件）。复用而不是另造，是因为那块逻辑只在
// `InkingSpriteLike::fireEventsBetween` 里有一份。

#include <image/SpriteData.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/// 模块名常量（日志用；约定：每个模块都暴露一个）。
inline constexpr const char* InkingSpriteGroupModuleName = "InkingSpriteGroup";

/**
 * @brief 块叠加组的一个动画：帧 → 一组格号。
 *
 * `frames[i]` 是第 `i` 帧要画的**格号列表**，顺序即叠加顺序。
 */
struct InkingSpriteGroupAnimation {
    /// 动作名（同一个组里不许重名；空名字算配置错）。
    std::string name;

    /// 每帧 = 格号列表；顺序 = 叠加顺序（先画的在下面）。
    std::vector<std::vector<int>> frames;

    /// 帧率。**默认 12**（与 `InkingSpriteData::fps` 同一个默认值，理由也同一份：
    /// 12fps 播得慢但看得见在动，配错一个数量级反而看不出）。
    double fps = 12.0;

    /// 循环吗。非循环播到末帧发一次 `finished`。
    bool loop = true;

    /// 帧事件：帧号是**本动画的帧号**，语义与 `InkingSpriteData::events` 一致。
    std::vector<InkingSpriteData::SpriteEvent> events;
};

/**
 * @brief 块叠加组的配置：**一张图集 + 多个动画 + 每帧一组格号**。
 *
 * 构造后 `Normalize()` → `Validate()` → 取 `initialAnimation`，与
 * `InkingSpriteData` 那条路同一个顺序（T-05 的构造体照抄这三步）。
 */
struct InkingSpriteGroupData {
    /// 图集路径（纹理缓存键）。**必填**——本类型不认"整张纹理"。
    std::string source;

    /**
     * 生成期烘进来的图集字节（0.9.0 T-05 补）。
     *
     * 与 `InkingButtonAppearance::imageBytes` 同一条约定：**非空就用它**、不读盘；
     * 为空则组件按 `runtimeAsset` 那条路从资源根读 `source`。
     *
     * 为什么 T-04 时没写、T-05 补上：组的构造要吃一份 `InkingImageData`，而
     * `InkingSpriteData` 的同名字段就是靠这两个字段撑起"生成期烘图"那条路
     * （`normal.imageBytes` / `normal.imageSize`）。少了它们，组只能读盘，
     * 生成器烘出来的图集用不上——那是真正的能力缺口，不是文档没写。
     */
    const std::uint8_t* imageBytes = nullptr;
    std::size_t imageSize = 0;

    /// 一格的像素宽 / 高（所有格等大，这是结构保证）。
    int frameWidth = 0;
    int frameHeight = 0;

    /// 网格：`columns × rows` 格，行优先。
    int columns = 0;
    int rows = 0;

    /// 图集外边距与格间距（像素，均不许为负）。
    int margin = 0;
    int spacing = 0;

    /// 动画表（顺序 = 声明顺序；第 0 个是默认动作）。
    std::vector<InkingSpriteGroupAnimation> animations;

    /// 找不到时的返回值（**不是** 0：0 是第一个动作）。
    static constexpr std::size_t kNotFound = static_cast<std::size_t>(-1);

    /// 按名字找下标；找不到返回 `kNotFound`。
    std::size_t FindIndex(const std::string& animationName) const noexcept;

    /// 格子总数（`columns × rows`；任一为 0 时返回 0）。本类型没有"要几帧"这回事
    /// ——帧数由每个动画的 `frames.size()` 决定。
    int ResolveCount() const noexcept;

    /**
     * @brief 第 `cell` 格在整张图上的像素矩形（**行优先**）。
     *
     * `x = margin + column × (frameWidth + spacing)`，`y` 同理。
     * 未配置 / 格号为负 / 越界时返回全零矩形（调用方据此跳过）。
     */
    InkingShapeBounds CellRect(int cell) const noexcept;

    /// 按这套网格，整张图应该有多大（供构造期与真实图尺寸比对）。
    int SheetWidth() const noexcept;
    int SheetHeight() const noexcept;

    /**
     * @brief 规范化：**当前没有可自动修正的字段**，所以是幂等的空操作。
     *
     * 为什么不做事（不是漏了）：等格是结构保证、格号越界只能报错（改到哪一格猜
     * 不出来）、`fps <= 0` 也只能报错（猜 12 就是静默兜底）。留着这个方法是为了
     * 与 `InkingSpriteData` 的构造流程对称——T-05 的构造体按同一条顺序走，
     * 哪天真的出现"能修正"的字段，改这里就够，调用方一行不用动。
     */
    void Normalize();

    /**
     * @brief 数据校验：返回**错误说明**，空串 = 通过。
     *
     * 九条，顺序即报告顺序（与 `InkingSpriteData::Validate` 同一个风格：不夹取、
     * 不猜，只报告）：
     *   ① `source` 空；② 网格字段非法；③ 动画表空；④ 动作名为空；⑤ 动作名重复；
     *   ⑥ `fps <= 0`；⑦ 动画没有帧 / 某帧没有格号；⑧ 格号越界；⑨ 事件帧越界。
     */
    std::string Validate() const;
};
