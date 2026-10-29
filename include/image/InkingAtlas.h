#pragma once

// 图集描述 → `SpriteData`（0.6.5 T-16）：**一个具名动画表**。
//
// 解决的是"一张大页里混装多个动作"：`columns × frameWidth` 只能表达等宽等高的网格，
// 而真实素材是"待机 4 帧 + 跑步 8 帧 + 攻击 6 帧，跑步比待机宽"。这一层把
// "这张图里有哪些动画、每个动画的帧矩形与播放参数"落成数据，再由
// `MakeSpriteData()` 转成组件真正吃的那份 `SpriteData`。
//
// ---------------------------------------------------------------------------
// 它**不是加载器**
//
// 字节仍然由资源层给（`AssetLoad` 的 vector、或生成期烘的静态数组），这一层只回答
// "图里的哪块是哪一帧"。所以 `MakeSpriteData` 是**纯函数**：给描述 + 给字节视图，
// 出一份 `SpriteData`，一个字节都不读盘。这条分工与"结构进 exe、字节进 assets"
// 一致（`CXXCSS.md` §4.1），也是"换美术不用重编"的前提。
//
// 描述文件本身在**生成期**被 `inkgen` 读（`CXXCSS/Atlas/*.json`，T-17）：
// 运行期解析器虽然库里有（`util/InkingJson.h`），但"热更描述文件"不在本轮范围
// （任务书 §11），所以这里只提供 C++ 数据面。
//
// ---------------------------------------------------------------------------
// 与网格的关系：**并存的两种表达，不是替代**
//
//   - 一张图一个动画、等宽等高 → 继续用 `sheet.columns / rows`（少写一堆矩形）；
//   - 一页多个动画、帧宽高不等 → 用 `AtlasData`（帧表）；
//   - 两者在**同一份配置里互斥**（`SpriteData::Validate()` 会拦），但不同配置之间
//     随便混用——它们产出的是同一种 `SpriteData`。

#include <image/SpriteData.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace inking {

/// 模块名常量（日志用；约定：每个模块都暴露一个）。
inline constexpr const char* kAtlasModuleName = "InkingAtlas";

/**
 * @brief 图集里的**一个动画**：名字 + 帧矩形 + 播放参数。
 *
 * 帧矩形是**绝对像素**（图集左上角为原点，和 `SpriteSheet::frameRects` 同一套语义），
 * 且**宽高可以不等**——这正是它存在的理由。
 */
struct AtlasAnimation {
    /// 动画名（`MakeSpriteData(atlas, "walk", …)` 用它找）。
    std::string name;

    /// 图集路径：**相对资源根**，同时充当纹理缓存键（同一张图集的多条动画共用一张纹理）。
    std::string source;

    /// 每帧在图集里的像素矩形（顺序 = 播放顺序）。
    std::vector<ShapeBounds> frames;

    double fps = 12.0;

    bool loop = true;

    /// 帧事件（`SpriteData::SpriteEvent`：播到第几帧发个名字）。
    std::vector<SpriteData::SpriteEvent> events;
};

/**
 * @brief 一张图集里的全部动画（具名表）。
 *
 * 重名动画是**必须拦**的错误：`FindIndex()` 命中第一个，第二条会被静默忽略——
 * 那种"某个动作永远播不出来"最难查（`Validate()` 会报）。
 */
struct AtlasData {
    /// 图集名（日志与产物里的标识；不是文件名）。
    std::string name;

    std::vector<AtlasAnimation> animations;

    /// 找不到时的返回值（不要用 `animations.size()` 判定，见 `FindIndex`）。
    static constexpr std::size_t kNotFound = static_cast<std::size_t>(-1);

    /// 按名字找动画下标；找不到返回 `kNotFound`（**不是** 0）。
    std::size_t FindIndex(const std::string& animationName) const noexcept;

    /**
     * @brief 数据校验：返回**错误说明**，空串 = 通过。
     *
     * 判据与 `SpriteData::Validate()` 分工一致：这里只管"图集描述自身"（没有动画、
     * 重名、帧矩形非法、缺路径），`fps <= 0` 这类交给转出来的 `SpriteData` 判——
     * **同一条判据只写一处**（0.6.1 定的规矩）。
     */
    std::string Validate() const;
};

/**
 * @brief 把一条动画转成组件吃的 `SpriteData`（纯函数，不读文件）。
 *
 * @param atlas           图集描述（调用前应已 `Validate()` 通过；不过关也照常转，
 *                        转出来的东西会让 `SpriteData::Validate()` 报同一条错）。
 * @param animationIndex  动画下标（用 `FindIndex(name)` 取）。
 * @param imageBytes      整张图集的字节视图；`nullptr` 表示"运行期自己去读 `source`"
 *                        （此时 `runtimeAsset = true`，由基类在构造时 `LoadAsset`）。
 * @param imageSize       字节数。
 *
 * 三条约定：
 *   - **尺寸默认取第一帧的自然宽高**（四舍五入）：图集不知道该画多大，但"跟第一帧
 *     一样大"是最少意外的默认值；调用方随时可以改 `width/height`（改尺寸会触发
 *     重布局，所以不在这里做）；
 *   - **`source` 只写一处**（`normal.source`）：`sheet.source` 留空即"沿用
 *     `normal.source`"，两处都写就是两份真相；
 *   - 帧表直接灌进 `sheet.frameRects`，因此**不会**同时给出网格字段
 *     （`SpriteData::Validate()` 要求两者互斥）。
 *
 * @return 下标越界时返回一份**非法**配置（`Validate()` 会报"没有可播的帧"），
 *         并记一条 ERROR——"少一个动画"不该让调用方崩掉。
 */
SpriteData MakeSpriteData(const AtlasData& atlas, std::size_t animationIndex,
                          const std::uint8_t* imageBytes, std::size_t imageSize);

}  // namespace inking
