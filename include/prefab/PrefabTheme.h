#pragma once

// 预制配件（C 系列）的**默认调色板与圆角**：一处改、五处跟着变。
//
// 为什么单拎一个头：选项栏 / 菜单栏 / 列表 / 下拉 / 文件选择这五套默认外观本来
// 就该是**同一套设计语言**（同一组底色、描边、主色、圆角）。散在五个 `.cpp` 里
// 各写一遍，迟早出现"菜单是圆的、列表是方的"那种分叉（`AGENTS.md` §6 第 34 条
// 那一族："抄第二遍就会分叉"）。
//
// ---------------------------------------------------------------------------
// 这一版的来处（用户偏好 → 取值）
//
//   - **主色取自他自己那份 Godot 引擎（`D:\Lya`）里的 "White Purple" 预设**：
//     强调色 `Color::html("9380c2")`、基础色 `Color::html("fcfaf8")`
//     （`editor/themes/editor_theme_manager.cpp` 的 preset 分支）——那是那台引擎里
//     **唯一一套"亮色 + 紫"**的配色，正对他"现代化的亮色场景 + 紫色那一套"的口味。
//     （同族的 "Liya 2" 是暗色：紫灰底 `#3D3B45` + 蓝紫强调 `#87ABE3`；第一版按
//     "Liya 3" 的蓝做过一版，用户看过之后指名要紫色的这一套。）
//   - **去掉的圆角**：列表里的**行**不要圆角（用户："好奇怪"）——行是表格单元，
//     直角才排得整齐；其余（面板 / 项 / 按钮）照旧圆润。
//   - **字体**：默认用资源旁的**开源圆体**（资源圆体 / Resource Han Rounded，
//     SIL OFL），拿不到就退回机器默认字体——见 `DefaultFontPath()`。
//
// 值都是 `0xAARRGGBB`（与 `AnchorData::color` 同一套）。

#include <cstdint>
#include <string>

namespace inking::prefab::theme {

// ---------------------------------------------------------------------------
// 颜色
// ---------------------------------------------------------------------------

/// 场景底（浅紫灰）。窗口层用它当清屏色（`InkingWindow::SetBackgroundColor`）。
inline constexpr std::uint32_t kSceneBackground = 0xFFF5F2F8u;
/// 卡片面（面板 / 视口 / 菜单栏）。
inline constexpr std::uint32_t kSurface = 0xFFFFFFFFu;
/// 次级面（比卡片略沉一点）。
inline constexpr std::uint32_t kSurfaceAlt = 0xFFFAF8FCu;
/// **承载面**：装内容的浅紫灰底（列表 / 目录**视口**用它）——这样里面的
/// **白底行**才浮得起来（行是白的、视口比它沉一档，层次靠明度差而不是描边）。
inline constexpr std::uint32_t kSurfaceSunken = 0xFFF1EDF7u;
/// 描边：1px 的浅紫灰（亮色场景里"边框"是让层次站住的关键，所以按钮都带它）。
inline constexpr std::uint32_t kOutline = 0xFFE7E2F0u;
/// 主文字（暖深灰，带一点紫调）。
inline constexpr std::uint32_t kTextPrimary = 0xFF33303Cu;
/// 次文字。
inline constexpr std::uint32_t kTextMuted = 0xFF6E6880u;
/// 禁用文字。
inline constexpr std::uint32_t kTextDisabled = 0xFFA9A4B8u;

/// 主色（White Purple 的紫）。
inline constexpr std::uint32_t kAccent = 0xFF9380C2u;
/// 主色的深版：亮底上的强调文字 / 主按钮的实底。
inline constexpr std::uint32_t kAccentStrong = 0xFF6E5AAEu;
/// **悬停 / 选中的浅紫底**（用户点名要的那一档）。
inline constexpr std::uint32_t kAccentSoft = 0xFFEDE7F8u;
/// 浅紫底（按下时更实一点）。
inline constexpr std::uint32_t kAccentSoftPressed = 0xFFDFD6F2u;
/// 中性悬停底（不带主色的那档）。
inline constexpr std::uint32_t kHoverNeutral = 0xFFF2EFF8u;

/// 滚动条：轨道（半透明黑）/ 滑块（透明主色）/ 悬停滑块。
inline constexpr std::uint32_t kScrollTrack = 0x14000000u;
inline constexpr std::uint32_t kScrollThumb = 0x556E5AAEu;
inline constexpr std::uint32_t kScrollThumbHover = 0x886E5AAEu;

/// 状态色（亮底上要压暗一点才读得清，同 Godot 亮色那一档的做法）。
inline constexpr std::uint32_t kSuccess = 0xFF2E9E52u;
inline constexpr std::uint32_t kWarning = 0xFFC77A0Au;
inline constexpr std::uint32_t kDanger = 0xFFD33B3Bu;

// ---------------------------------------------------------------------------
// 圆角（设计坐标）
//
// 列表里的**行**故意不在这一组里：它用直角（`ShapeSpec::Rect()`），理由见
// `ListView.cpp` 的 `MakeDefaultRowLook`——行是表格单元，圆角看着"好奇怪"。
// ---------------------------------------------------------------------------

inline constexpr float kRadiusPanel = 16.0f;    ///< 面板 / 视口 / 展开的那一栏
inline constexpr float kRadiusCard = 18.0f;     ///< 整块卡片（文件选择器）
inline constexpr float kRadiusItem = 12.0f;     ///< 选项 / 菜单项 / 下拉的收起按钮
inline constexpr float kRadiusButton = 10.0f;   ///< 小按钮（底部按钮 / 快捷位置）
inline constexpr float kRadiusCaption = 8.0f;   ///< 标题栏按钮

// ---------------------------------------------------------------------------
// 字体
// ---------------------------------------------------------------------------

/**
 * @brief 默认字体（**绝对路径**，UTF-8；空串 = 用机器默认字体）。
 *
 * 按顺序在**资源根**（exe 旁 `assets/`，见 `AssetRoot()`）里找这几个候选：
 *   - `font/ResourceHanRoundedCN-Regular.ttf`（资源圆体 CN，SIL OFL，中文圆体首选）
 *   - `font/ZenMaruGothic-Regular.ttf`（禅圆体，OFL）
 *   - `font/MPLUSRounded1c-Regular.ttf`（M PLUS Rounded 1c，OFL）
 *
 * 都找不到就返回空串——那时 `TextRequest::fontPath` 为空，文字层会用机器默认
 * 字体（**不是不画**），所以"没放字体"不该让任何东西变红。
 *
 * 结果在**函数内静态量**里缓存（`AGENTS.md` §6 第 12 条：注册表 / 单例一律这么写），
 * 所以每建一个默认外观调它一次也不会有文件系统开销。
 */
std::string DefaultFontPath();

}  // namespace inking::prefab::theme
