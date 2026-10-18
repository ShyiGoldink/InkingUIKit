#pragma once

// 运行期主题（颜色）。
//
// 生成期（CXXCSS/Theme/*.json）已经把 `$token` 换成了字面量，那份是**默认主题**：
// 不装表时一切都按烘好的字面量走，行为与没有这套东西时逐字节一致。
//
// 这层只负责一件事：**把 token 名字留下来，运行期能重查**。
//
// 为什么不碰字号：字号 token 改的是**几何**（重排 → 重烘命中表 → 重烘字图集），
// 那是几个数量级的成本；颜色只是绘制期数据，换一次就是重发一遍顶点。所以字号
// token 继续在生成期替换，写进文档就是"字号是编译期的"。
//
// 三条和语言表（`text/InkingTextTable.h`）对齐的规矩：
//   1. **版本号**：装表 / 切主题都让版本 +1，组件把版本放进自己的缓存里 ⇒
//      下一帧自然重查。**不要"遍历场景通知所有组件"**——那种通知漏一个就是
//      "有的组件还是旧配色"，而且只在特定界面顺序下现形；
//   2. **查不到就用兜底**：`ResolveThemeColor` 第三个参数就是生成期烘的字面量，
//      于是"某张主题漏了一个 token"表现为"这一处还是旧色"，不是崩、不是空白；
//   3. **全局状态住函数内静态量**（`AGENTS.md` §3.5 硬约定），并且必须落在 .cpp
//      ——头内 inline 静态量每 TU 一份 ⇒ DLL/exe 各持一份，静默失效。
//
// 契约：`ThemeColorEntry::token` 与组件里存的 token 都按**静态生命周期**对待
// （生成器发的是字面量）。要动态拼？`InstallThemeTable` 收的是值，表里的串由
// 运行期自己持有；而组件上的 `themeColor` 指针指向字面量，不指向表。

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace inking {

/// 一条 "token → 颜色"。token 不带 `$`（和主题 json 的键一致）。
struct ThemeColorEntry {
    std::string token;
    std::uint32_t color = 0u;  ///< 0xAARRGGBB
};

/// 一张主题表：名字 + 颜色表。字号 token 不在这里（见文件头）。
struct ThemeTable {
    std::string name;
    std::vector<ThemeColorEntry> colors;

    bool Empty() const noexcept { return name.empty() && colors.empty(); }

    /// 按 token 找颜色；没有返回 `nullptr`。
    const std::uint32_t* Find(const std::string& token) const noexcept;
};

/**
 * @brief 装一张主题表（同名的整张替换）。**不切换**当前主题。
 *
 * 装多张才能"运行期多主题"：`InstallThemeTable(light)` +
 * `InstallThemeTable(dark)`，之后 `SetActiveTheme("dark")` 就是换肤。
 *
 * 生成物里的入口叫 `InstallGeneratedThemes()`（`CxxCSS` 那一套自动生成）。
 */
bool InstallThemeTable(ThemeTable table);

/**
 * @brief 切到某张**已装**的主题。
 *
 * 没有这张表 → 返回 false，**版本不动**（界面不会白闪一下再回到原样）。
 * 切成功 → 版本 +1，所有带 token 的颜色下一帧自动重查。
 */
bool SetActiveTheme(const std::string& name);

/// 当前活跃主题名（没装表时是空串）。
const std::string& ActiveThemeName() noexcept;

/// 装了没有 / 装了几张（观测口）。
bool HasThemeTable(const std::string& name) noexcept;
std::size_t ThemeTableCount() noexcept;

/**
 * @brief 主题版本：装表 / 切主题都 +1，**从 0 开始**。
 *
 * 组件的缓存键里带上它，"换肤"就是自然结果——不需要谁去遍历通知。
 */
std::uint64_t ThemeVersion() noexcept;

/**
 * @brief token → 当前主题里的颜色；没有就用 `fallback`。
 *
 * `token` 为空 / 为 `nullptr`（这个颜色位本来就不是语义色）或**一张表都没装**时，
 * 直接返回 `fallback`——那是生成期烘好的字面量。前两条保证"不用运行期主题的
 * 项目"零成本：一次指针判空，不进任何表。
 */
std::uint32_t ResolveThemeColor(const char* token,
                                std::uint32_t fallback) noexcept;

/// 自检观测口：查表命中 / 未命中次数 + 当前版本。
struct ThemeStats {
    std::uint64_t hits = 0;
    std::uint64_t misses = 0;
    std::uint64_t version = 0;
};

ThemeStats ThemeStatsSnapshot() noexcept;

/// 清空全部主题表与活跃主题（回到"全用生成期字面量"）。自检用。
void ClearThemeTables() noexcept;

}  // namespace inking
