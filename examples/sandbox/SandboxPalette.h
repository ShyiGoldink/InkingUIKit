#pragma once

// ---------------------------------------------------------------------------
// SBOX-01 / SBOX-02 · 2.5D 小沙盒配色常量（**唯一的一处**）
//
// 颜色一律 0xAARRGGBB（alpha 在高字节）。§1..§8 那份规格的常量表逐条落在这里，
// 名字与数值与 `docs/art-palette.md` §8 一致；主理人做跨成员对齐时只核对本文件。
//
// 分工：
//   - 地表 / 树 / 水面 / 天空 / 地面  = 美术规格给的**数据**，不要在这里改语义；
//   - kReliefWhite / kReliefGlint / kOpaque / kTransparent = 工程侧实现用的中性量。
//
// 为什么末尾有 static_assert：规格 §6/§8 给了「四个 Base 的感知亮度两两差 ≥ 30」
// 这条可计算的回归线。放在这里，换色时编译期就红，不会等画面糊了才发现。
// ---------------------------------------------------------------------------

#include <cstdint>

namespace sandbox::palette {

// ---- 地表：四档明度阶梯 202 / 147 / 112 / 79（规格 §2）--------------------
inline constexpr std::uint32_t kSandBase   = 0xFFE6C88Cu;  // Y≈202 全场最亮
inline constexpr std::uint32_t kSandLit    = 0xFFF5DCA6u;
inline constexpr std::uint32_t kSandShade  = 0xFFD3B57Eu;

inline constexpr std::uint32_t kGrassBase  = 0xFF74A44Bu;  // Y≈147
inline constexpr std::uint32_t kGrassLit   = 0xFF8DBE63u;
inline constexpr std::uint32_t kGrassShade = 0xFF5C8439u;

inline constexpr std::uint32_t kStoneBase  = 0xFF6E7073u;  // Y≈112 无彩
inline constexpr std::uint32_t kStoneLit   = 0xFF838587u;
inline constexpr std::uint32_t kStoneShade = 0xFF595B5Eu;

inline constexpr std::uint32_t kWaterBase  = 0xFF26577Eu;  // Y≈79 最暗档
inline constexpr std::uint32_t kWaterLit   = 0xFF316A94u;
inline constexpr std::uint32_t kWaterShade = 0xFF1B4262u;

// ---- 瓦片描边（规格 §5）---------------------------------------------------
inline constexpr std::uint32_t kTileOutline     = 0xFF232A31u;  // BLEND 不可用时的回退实色描边
inline constexpr std::uint32_t kTileOutlineSoft = 0x80202830u;  // 外轮廓启用：半透明中性黑叠在 kGroundColor 上

// ---- 树（规格 §3）---------------------------------------------------------
inline constexpr std::uint32_t kTreeTrunk        = 0xFF7B5230u;
inline constexpr std::uint32_t kTreeTrunkShade   = 0xFF573A21u;
inline constexpr std::uint32_t kTreeCanopy       = 0xFF3E7838u;
inline constexpr std::uint32_t kTreeCanopyLit    = 0xFF5C9B4Fu;
inline constexpr std::uint32_t kTreeCanopyShade  = 0xFF2A5528u;

// ---- 简易水面（规格 §3.1，alpha 0x99 ≈ 60%）------------------------------
inline constexpr std::uint32_t kWaterSurface = 0x994E9FD1u;
inline constexpr std::uint32_t kWaterGlint  = 0xB2CDEBF5u;  // 可选，本切片未用

// ---- 天空 / 地面底色（规格 §4：必须显式覆盖库默认值）-----------------------
inline constexpr std::uint32_t kSkyColor    = 0xFF8FAEC6u;
inline constexpr std::uint32_t kGroundColor = 0xFF31363Du;

// ---- 工程侧中性量 ---------------------------------------------------------
// 这四个都是**中性灰阶**（R=G=B），不含任何地表色。补充对齐项 C：四种地表共用
// 一张白 / 灰阶 relief，颜色只由 InkingFieldTile::tint 给。
/// 不透明 tint（乘性调制的单位元）；树这个物体用它，不走地表 tint。
inline constexpr std::uint32_t kOpaque = 0xFFFFFFFFu;
/// relief 顶面纯白 255 ⇒ tint × 1.0 = kXxxBase，Base 明度阶梯逐位精确。
inline constexpr std::uint32_t kReliefWhite = 0xFFFFFFFFu;
/// relief 下沿暗边 + 2px 厚度（≈ 75% 亮度）。
inline constexpr std::uint32_t kReliefShade = 0xFFC0C0C0u;
/// relief 顶面的中性 1px 点，极简图案（≈ 86% 亮度）。
inline constexpr std::uint32_t kReliefPattern = 0xFFDCDCDCu;
/// 水面层那一格的波纹亮线。
inline constexpr std::uint32_t kReliefGlint = 0xFFE8F2FAu;
/// 透明像素（图集里树格的四角、未使用区域）。
inline constexpr std::uint32_t kTransparent = 0x00000000u;

// ---- 规格 §8 的达标校验线 --------------------------------------------------
/// 加权感知亮度 Y = 0.2126R + 0.7152G + 0.0722B（0–255）。
constexpr int Luma(std::uint32_t argb) noexcept {
    const int r = static_cast<int>((argb >> 16) & 0xFFu);
    const int g = static_cast<int>((argb >> 8) & 0xFFu);
    const int b = static_cast<int>(argb & 0xFFu);
    return static_cast<int>(0.2126 * r + 0.7152 * g + 0.0722 * b + 0.5);
}

static_assert(Luma(kSandBase) - Luma(kGrassBase) >= 30,
              "沙地/草地明度差 < 30：规格 §6 的去色可分辨性不达标");
static_assert(Luma(kGrassBase) - Luma(kStoneBase) >= 30,
              "草地/石砖明度差 < 30：规格 §6 的去色可分辨性不达标");
static_assert(Luma(kStoneBase) - Luma(kWaterBase) >= 30,
              "石砖/水明度差 < 30：规格 §6 的去色可分辨性不达标");
static_assert(((kWaterSurface >> 24) & 0xFFu) != 0u
                  && ((kWaterSurface >> 24) & 0xFFu) != 0xFFu,
              "水面 tint 的 alpha 必须落在 (0, 255)：0 会被 HitObject 剔除，255 会盖住底下瓦片");

}  // namespace sandbox::palette
