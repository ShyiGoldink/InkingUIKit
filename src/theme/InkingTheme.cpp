#include <theme/InkingTheme.h>

// 主题的全局状态放在**函数内静态量**里，而且必须落在这个 .cpp：
// 头内 inline 静态量每个 TU 一份，DLL 与 exe 会各持一份（`AGENTS.md` §2-4），
// 表现是"切了主题，界面上有一半组件没变"——静默失效那一类。

namespace inking {

namespace {

/**
 * 主题的运行期状态：装了哪几张、当前是哪张、版本、命中统计。
 *
 * 一个结构体装全部状态而不是四个静态量：`ClearThemeTables` 与"装表就清空统计"
 * 这类操作要一起成立，分开写迟早漏一个。
 */
struct ThemeState {
    std::vector<ThemeTable> tables;
    std::string activeName;
    std::uint64_t version = 0;
    std::uint64_t hits = 0;
    std::uint64_t misses = 0;
};

ThemeState& themeState() {
    static ThemeState state;
    return state;
}

/// 当前活跃那张表；没装 / 没切 / 名字失效时返回 `nullptr`。
const ThemeTable* activeTable() noexcept {
    const ThemeState& state = themeState();
    if (state.activeName.empty()) {
        return nullptr;
    }
    for (const ThemeTable& table : state.tables) {
        if (table.name == state.activeName) {
            return &table;
        }
    }
    return nullptr;
}

}  // namespace

const std::uint32_t* ThemeTable::Find(const std::string& token) const noexcept {
    if (token.empty()) {
        return nullptr;
    }
    for (const ThemeColorEntry& entry : colors) {
        if (entry.token == token) {
            return &entry.color;
        }
    }
    return nullptr;
}

bool InstallThemeTable(ThemeTable table) {
    if (table.name.empty()) {
        return false;
    }
    ThemeState& state = themeState();
    for (ThemeTable& existing : state.tables) {
        if (existing.name == table.name) {
            // 同名整张替换：换一整套才是常态，合并会让"旧表里的 token 还在不在"
            // 变成一件要查的事（和语言表同一条理由）。
            existing = std::move(table);
            ++state.version;
            return true;
        }
    }
    state.tables.push_back(std::move(table));
    ++state.version;
    return true;
}

bool SetActiveTheme(const std::string& name) {
    ThemeState& state = themeState();
    if (state.activeName == name) {
        return true;  // 幂等：**不**动版本，否则每帧调一次就是"永远在重查"
    }
    bool found = false;
    for (const ThemeTable& table : state.tables) {
        if (table.name == name) {
            found = true;
            break;
        }
    }
    if (!found) {
        return false;  // 版本不动：界面不会白闪一下再回到原样
    }
    state.activeName = name;
    ++state.version;
    return true;
}

const std::string& ActiveThemeName() noexcept { return themeState().activeName; }

bool HasThemeTable(const std::string& name) noexcept {
    for (const ThemeTable& table : themeState().tables) {
        if (table.name == name) {
            return true;
        }
    }
    return false;
}

std::size_t ThemeTableCount() noexcept { return themeState().tables.size(); }

std::uint64_t ThemeVersion() noexcept { return themeState().version; }

std::uint32_t ResolveThemeColor(const char* token,
                                std::uint32_t fallback) noexcept {
    // ① 这个颜色位不是语义色 —— 绝大多数调用走这条。
    if (token == nullptr || token[0] == '\0') {
        return fallback;
    }
    // ② 一张表都没装：生成期烘的是什么就用什么（"不用运行期主题"的零成本路径）。
    const ThemeTable* table = activeTable();
    if (table == nullptr) {
        return fallback;
    }

    const std::uint32_t* found = table->Find(token);
    ThemeState& state = themeState();
    if (found == nullptr) {
        // ③ 表里没有这个 token：**不报错、不空白**，用兜底色。
        //    生成期已经保证"用到的 token 每张表都有"，走到这里只可能是有人
        //    运行期自己拼了一张不全的表。计数留在观测口里，别静默。
        ++state.misses;
        return fallback;
    }
    ++state.hits;
    return *found;
}

ThemeStats ThemeStatsSnapshot() noexcept {
    const ThemeState& state = themeState();
    ThemeStats stats;
    stats.hits = state.hits;
    stats.misses = state.misses;
    stats.version = state.version;
    return stats;
}

void ClearThemeTables() noexcept {
    ThemeState& state = themeState();
    state.tables.clear();
    state.activeName.clear();
    ++state.version;  // 清空也是一次"主题变了"，组件要重查到兜底色
    state.hits = 0;
    state.misses = 0;
}

}  // namespace inking
