#pragma once

#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

// SDL 的类型在**全局**命名空间：这个前向声明必须写在 `namespace inking` 外面，
// 写进去就成了 `inking::SDL_Renderer`——和真类型不是一回事，报的错是
// "cannot convert inking::SDL_Renderer* to SDL_Renderer*"（一眼看不出是命名空间问题）。
struct SDL_Renderer;
struct SDL_Texture;

namespace inking {

/**
 * @brief 字形图集的**打包器**（TaskGuide B4 后半）：把一张张字形位图塞进一张大纹理。
 *
 * 它只算**坐标**，不碰渲染器、不碰字体——所以"会不会叠、放不放得下"这件事能
 * 纯逻辑自检（`ink_glyph_atlas_test`）。渲染那边拿这些坐标把字形贴进图集纹理，
 * 绘制时按 UV 取用。
 *
 * ---------------------------------------------------------------------------
 * 为什么是**货架式（shelf）**而不是最优装箱
 *
 * 字形尺寸分布很窄（同一字号的汉字彼此差不多高），货架式够用且**可预测**：
 * 每层高度由"第一个放进来的字形"决定，往右排、排满换一层。最优装箱（把大小字
 * 混着插）能省几个百分点，代价是每次插入都要搜整个空闲表——在这个量级上不值。
 *
 * **1 像素间隔**（`kPadding`）：相邻字形的抗锯齿边缘会互相渗色（双线性采样会
 * 取到邻居的像素），间隔是最便宜的解法。代价是占用率略降。
 *
 * 放不下时 `Insert` 返回**无效槽**（`valid == false`），调用方自己决定
 * "换一张图集"还是"这次不缓存"——打包器不做淘汰策略（那是缓存层的事）。
 */
class GlyphAtlas {
public:
    /// 图集里的一格字形。
    struct Slot {
        int x = 0;       ///< 图集纹理内的左上角（像素）
        int y = 0;
        int width = 0;   ///< 字形位图尺寸（不含间隔）
        int height = 0;
        bool valid = false;  ///< false = 这张图集放不下了
    };

    /// 字形之间留的间隔（像素）：抗锯齿边缘互相渗色的最小代价。
    static constexpr int kPadding = 1;

    explicit GlyphAtlas(int size = 1024);

    /// 图集边长（正方形）。
    int Size() const noexcept { return size_; }

    /**
     * 申请一块 `width × height`。
     *
     * 尺寸非法（≤ 0 或超过图集）或排不下时返回 `valid == false` 的空槽。
     */
    Slot Insert(int width, int height);

    /// 已经放进去几格（观测口）。
    int SlotCount() const noexcept { return static_cast<int>(slots_.size()); }

    /// 开了几层货架（观测口：层数突然暴涨说明尺寸分布变杂了）。
    int ShelfCount() const noexcept {
        return static_cast<int>(shelves_.size());
    }

    /// 已用**高度**（最后那层的底边）——"这张图集还剩多少"的一个保守观测量。
    int UsedHeight() const noexcept;

    /// 已用面积 / 图集面积（含间隔，所以永远小于 1）。
    double Occupancy() const noexcept;

    /// 这一格和任何**已分配**的格子重叠吗（自检用：打包器最容易错在这里）。
    bool OverlapsAny(const Slot& slot) const noexcept;

    /// 清空（换一张图集时调；已缓存的字形由缓存层负责重新申请）。
    void Reset();

    /// 现在分配过的格子（自检 / 观测用；按分配顺序）。
    const std::vector<Slot>& Slots() const noexcept { return slots_; }

private:
    struct Shelf {
        int y = 0;        ///< 这一层的顶边
        int height = 0;   ///< 这一层有多高（由第一个进来的字形决定）
        int nextX = 0;    ///< 下一个字形往这儿放
    };

    int size_ = 1024;
    std::vector<Shelf> shelves_{};
    std::vector<Slot> slots_{};
};

// ---------------------------------------------------------------------------
// 逐字形排版
// ---------------------------------------------------------------------------

/// 一个字形在行内的落点。
struct GlyphPlacement {
    std::size_t offset = 0;  ///< 在源串里的**字节**起点（码点边界上）
    std::size_t length = 0;  ///< 这个字形的字节长度（1~4）
    float x = 0.0f;          ///< 左边界（设计坐标）
    float advance = 0.0f;    ///< 前进量：下一个字形的 `x = x + advance`
};

/**
 * 量一段 UTF-8 有多宽（设计坐标）。
 *
 * **注入**测量是这一层的既有套路（A7 的排版同一个思路）：真实路径传"问字体"，
 * 自检传假的（每码点固定宽），于是"推进累加对不对"能纯数字验。
 */
using GlyphMeasure = std::function<float(std::string_view)>;

/**
 * @brief 把**一行**文字排成"一串字形落点"（B4 后半）。
 *
 * 为什么需要它：现在的文字绘制是"**一段文字一张纹理**"（`TTF_RenderText_Blended`
 * → 一张纹理），而字形图集要的是"**每个字形一张小图** + 一个位置"——位置从哪来，
 * 就是这里算的。一旦有了"每个字形的 x 与前进量"，绘制那边才能逐字形取图、
 * 生成 quad、按 UV 从图集里采。
 *
 * 三条约定：
 *   - 只处理**一行**（不含 `\n`）：换行 / 省略号 / 对齐都是 A7 排版层的事，
 *     它给出每行的字节区间，这里把行内切成字形；
 *   - **按码点**切，不按字节：中文一个字三字节，按字节切会把汉字劈成两半；
 *   - `advance` 由测量给出（真实路径是"这个字形在字体里的前进量"）。测量为空时
 *     退化成"每个码点 1 单位宽"——那只是让自检能写，不是能用的一档。
 *
 * @param text   一行 UTF-8
 * @param startX 第一个字形的左边界（设计坐标）
 * @param measure 测量回调；为空时按每码点 1 单位退化
 * @return 每个码点一条；空串返回空表
 */
std::vector<GlyphPlacement> LayOutGlyphs(std::string_view text, float startX,
                                         const GlyphMeasure& measure);

// ---------------------------------------------------------------------------
// 运行时的字形图集（B4 后半：真正的"逐字形"那一层）
// ---------------------------------------------------------------------------

namespace detail {

/// 一个字形在图集里的落点 + 排版需要的度量。
struct GlyphBitmap {
    bool valid = false;    ///< false = 这次没取到（没字体 / 图集满了）
    int atlasX = 0;        ///< 图集纹理里的左上角（**像素**）
    int atlasY = 0;
    int width = 0;         ///< 位图尺寸（像素）
    int height = 0;
    float advance = 0.0f;  ///< 前进量（**设备像素**——和 `deviceSize` 同一套；
                           ///< 设计坐标要自己除倍率）
};

/// 字形图集的记账（观测口：自检与排错用）。
struct GlyphAtlasStats {
    int rasterized = 0;  ///< 真正栅格化了几次
    int reused = 0;      ///< 命中缓存几次（省下的就是这里）
    int uploaded = 0;    ///< 贴进图集几次
    int failed = 0;      ///< 取不到几次（调用方**退回整段渲染**）
    int slots = 0;       ///< 图集里已经有多少格
};

/**
 * @brief 取一个字形（B4 后半）：已经栅格化过就复用，否则栅格化并贴进图集。
 *
 * 这是"逐字形"与"整段渲染"的分界点：拿到 `GlyphBitmap` 之后，绘制那边就能
 * 按 `atlasX/atlasY/width/height` 从**图集**里取这一格，按 `advance` 往前推进。
 *
 * 几个必须说清的行为（都是踩过的方向）：
 *   - **失败要能退回**：没字体、字号太小、图集满 —— 一律 `valid == false`，
 *     调用方（`PaintText`）**退回整段渲染**。宁可这一笔慢一点，也不能让字消失；
 *   - **贴图集要存/恢复状态**：贴字形时要把渲染目标切到图集纹理，而裁剪矩形、
 *     原目标都是"切换前"的。切回来必须原样恢复，否则会表现成"某些组件被剪、
 *     某些没被剪，还和叠放顺序有关"（`AGENTS.md` §6 第 61 条那条的同族）；
 *   - **位图是白字**：颜色留给纹理调制（和整段那条路一致，颜色不进缓存键）；
 *   - **键 = 字体 + 字号 + 字形**：同一字号下每个字形只栅格化一次——这正是
 *     "一个字符串一张纹理"换成"一个字形一份位图"要省的东西。
 */
GlyphBitmap AcquireGlyph(SDL_Renderer* renderer, const std::string& fontPath,
                         float deviceSize, std::string_view glyph);

/// 记账快照（自检靠它断言"第二次是复用、不是重新栅格化"）。
GlyphAtlasStats GlyphAtlasSnapshot();

/**
 * 开关：走不走"逐字形"那条路（**默认走**）。关掉 = 回到"一段文字一张纹理"。
 *
 * 存在的理由是**对账**：两条路的像素差多少必须能量出来（自检里就是同一段文字
 * 各画一次、逐像素比），而不是"看着差不多"。哪条路出问题也能一键切回去。
 */
void SetGlyphAtlasEnabled(bool enabled);
bool IsGlyphAtlasEnabled();

/**
 * 图集纹理的句柄（观测 / 自检用；**不要**拿去改内容）。
 *
 * 存在的理由很具体：字形"贴进去了没有"是这一层唯一没法从记账里看出来的事
 * （计数全对、图集却是空的——被这个坑过一次）。自检可以把它设成渲染目标读一格
 * 像素，看是不是白的。
 */
SDL_Texture* GlyphAtlasTexture();

/// 丢掉图集与字形表（由 `ReleaseTextCache` 调，销毁渲染器之前必须）。
void ReleaseGlyphAtlas();

}  // namespace detail

}  // namespace inking
