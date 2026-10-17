#pragma once

// 多语言（A7）：占位符 → 各语言的串 → **切换时机**。
//
// 三件事分得很清（这是任务书要求"一起做"的那部分）：
//
//   1. **串从哪来**：`TextTable`（语言列表 + 占位符表）。它是纯数据，由生成器从
//      `CXXCSS/Text/strings.json` 烘出来，也可以自己装一张进来（运行期拼的界面）；
//   2. **怎么查**：`ResolveText("menu.start")` → 当前语言下的串；
//      **查不到就原样返回 key**——于是"还没做多语言的字面量"照样能显示，
//      不会变成空白（那种表现很难查：字没了，但没有任何报错）；
//   3. **什么时候生效**：**版本号**。换表 / 换语言都让版本 +1，组件把版本放进
//      自己的缓存键里 ⇒ 下一帧自动重查、重排、重烘纹理。
//      这样就不需要"遍历场景里所有标签去通知它们"——那种通知漏一个就是
//      "有的标签还是旧语言"，而且只在特定界面顺序下现形。
//
// 全局状态用**函数内静态量**（`AGENTS.md` §3.5 硬约定），不用类的静态数据成员。

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace inking {

/// 一条占位符在各语言下的串（顺序与 `TextTable::languages` 一一对应）。
struct TextEntry {
    std::string key;
    std::vector<std::string> translations;
};

/// 语言表：有哪些语言 + 占位符表。
struct TextTable {
    std::vector<std::string> languages;  ///< 语言代号，如 `zh` / `en`
    std::vector<TextEntry> entries;

    bool Empty() const noexcept { return languages.empty(); }

    /// 语言代号 → 下标；没有这门语言返回 `-1`。
    int LanguageIndex(const std::string& language) const noexcept;

    /// 按 key 找条目；没有返回 `nullptr`。
    const TextEntry* Find(const std::string& key) const noexcept;
};

/**
 * @brief 装一张语言表（生成期烘出来的那份，或者你自己拼的）。
 *
 * `language` 为空 = 用表里的第一门。返回"版本是否变了"。
 * **替换整张表**（不是合并）：语言这种东西换一整套才是常态，
 * 合并会让"旧表里的 key 还在不在"变成一件要查的事。
 */
bool InstallTextTable(TextTable table, const std::string& language = {});

/// 切到某门语言。没有这门语言返回 `false`（版本不动，界面也不会闪一下）。
bool SetTextLanguage(const std::string& language);

/// 当前语言代号（没装表时是空串）。
const std::string& CurrentTextLanguage() noexcept;

/// 当前语言下标（没装表时是 0）。
std::size_t CurrentTextLanguageIndex() noexcept;

/**
 * @brief 语言版本：换表 / 换语言都 +1，**从 0 开始**。
 *
 * 组件的缓存键里带上它，"切语言要重排"就是自然结果——不需要谁去遍历通知。
 */
std::uint64_t TextLanguageVersion() noexcept;

/// 一个占位符在当前语言下的串；查不到（没装表 / 没这个 key / 这门语言没译文）
/// 时**原样返回 key**。
std::string ResolveText(const std::string& key);

/// 自检观测口：查表命中 / 未命中次数 + 当前版本。
struct TextTableStats {
    std::uint64_t hits = 0;
    std::uint64_t misses = 0;
    std::uint64_t version = 0;
};

TextTableStats TextTableStatsSnapshot() noexcept;

/// 清空语言表（回到"所有 key 都当字面量"）。自检用，业务一般不需要。
void ClearTextTable() noexcept;

}  // namespace inking
