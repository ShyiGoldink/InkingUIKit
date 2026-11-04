#pragma once

// 纹理句柄（dev010 T-01）：给"游戏内容"侧一个**跨帧持有**的纹理引用。
//
// 为什么需要它：`InkingStaticImage` / `InkingSprite` 是按**组件**取图的——每挂一个
// 图元就要一个节点，而节点有锚点重算、命中表条目、绘制列表项（实测每节点约
// 1.4 µs/帧，5000 个就是 7.6 ms）。批量提交（`InkingGeometryLayer`）要的是
// "一个句柄 + 一批顶点"，句柄必须在公开面存在。
//
// 为什么是整数 id 而不是指针：
//   - 公开头不许拖 SDL 进来（`AGENTS.md` §6 第 8 条）；
//   - `void*` 包一层 `SDL_Texture*` 的死法最难查——纹理被字节预算淘汰后指针已经
//     野了，`SDL_RenderGeometry` 在 software 后端未必立刻崩，表现是"图少一块"。
//     整数句柄在提交时查表：查不到就是查不到，一条 WARN，画面少一块但不会踩野指针。
//
// **引用语义（重要）**：Load 一次 = 持有一份，**不 Release 就一直占内存**。
// 句柄钉住的纹理不参与字节预算淘汰（`InkingSetTextureBudget` 那套）。
// "跨帧持有"和"随时可丢"不能同时成立，这里选前者，代价写在上面这条。

#include <cstdint>
#include <string>

/// 这个模块的名字（日志里用；和别的模块一个约定）。
inline constexpr const char* InkingTextureModuleName = "InkingTexture";

/// 纹理句柄。`id == 0` 是无效句柄（默认构造），可以安全地存进容器 / 传给绘制。
struct InkingTextureHandle {
    std::uint32_t id = 0;

    bool Valid() const noexcept { return id != 0; }

    friend bool operator==(const InkingTextureHandle&,
                           const InkingTextureHandle&) = default;
};

/**
 * @brief 句柄层的观测口（都是累计值，自检取差值）。
 *
 * "缓存生效了没有""句柄失效了有没有人发现"这两件事在画面上看不出来，
 * 只有计数抓得住（`AGENTS.md` §6 第 40 条）。
 */
struct InkingTextureStats {
    std::uint64_t loads = 0;         ///< 真的读资源 + 建纹理的次数
    std::uint64_t cacheHits = 0;     ///< 同路径重复 Load，复用已有句柄
    std::uint64_t releases = 0;      ///< Release 的调用次数（含无效句柄）
    std::uint64_t liveHandles = 0;   ///< 现在活着几个句柄
    std::uint64_t staleSubmits = 0;  ///< 提交时句柄已失效（换过渲染器 / 已 Release）
    std::uint64_t loadFailures = 0;  ///< 读不到资源、解不出图（含 SVG）
    std::uint64_t pinnedBytes = 0;   ///< 被句柄钉住的纹理字节（RGBA8 上界，口径同纹理账本）
};

/**
 * @brief 载入一张纹理并返回**跨帧持有**的句柄。
 *
 * 路径是**相对资源根**的路径（`InkingAssetConfigure` 的那套口径），与
 * `InkingStaticImage::LoadAsset` 一致。同一个路径重复 Load **返回同一个 id**
 * 且只解码一次（`cacheHits` 记账）。
 *
 * 支持位图（PNG/JPG/BMP…，走 SDL3_image）。**不支持 SVG**：SVG 要按目标尺寸
 * 光栅化，"一张固定纹理"这个模型对它不成立——返回无效句柄 + 一条 WARN。
 *
 * @return 读不到 / 解不出时返回无效句柄（`Valid() == false`）并记 `loadFailures`；
 *         不抛异常、不中止（与资源层的既有约定一致）。
 */
InkingTextureHandle InkingLoadTexture(const std::string& relativeAssetPath);

/**
 * @brief 放掉一份持有。
 *
 * 最后一个句柄释放后，这张纹理**重新参与**字节预算淘汰（不立刻销毁——别的地方
 * 可能还在用同一张）。传无效句柄是空操作。
 */
void InkingReleaseTexture(InkingTextureHandle handle);

/// 这个句柄现在还有效吗（没 Release 完、且渲染器没换过人）。
bool InkingTextureValid(InkingTextureHandle handle);

/// 纹理的像素尺寸。句柄无效时返回 false 并把两个出参置 0。
bool InkingTextureSize(InkingTextureHandle handle, int* width, int* height);

/// 句柄层计数快照（见 `InkingTextureStats`）。
InkingTextureStats InkingTextureStatsSnapshot() noexcept;

/**
 * @brief 全部放掉（换渲染器 / 退出前）。
 *
 * 纹理属于渲染器：**渲染器销毁之前必须调**（`ReleaseTextCache` 里那道保险也会
 * 清掉纹理表，但句柄 id 不会回收，所以调用方手上的旧 id 会一律变失效）。
 * 幂等。
 */
void InkingTextureReleaseAll() noexcept;
