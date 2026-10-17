#include <text/InkingTextTable.h>

#include <utility>

// 多语言的实现（A7）。状态只有一份，放在**函数内静态量**里
// （`AGENTS.md` §3.5：别用类的静态数据成员——静态初始化顺序会让
// "场景活了库还没起来"这种事发生）。
//
// 这里没有任何锁：语言切换是"启动时 / 设置界面"里的动作，和渲染 / 布局同在
// UI 线程。真要在别的线程换语言，那时再谈同步（现在加锁只会掩盖设计问题）。

namespace inking {

namespace {

struct TextTableState {
    TextTable table;
    std::string language;
    std::size_t languageIndex = 0;
    std::uint64_t version = 0;
    std::uint64_t hits = 0;
    std::uint64_t misses = 0;
};

/// 全局语言状态（函数内静态量）。
TextTableState& State() noexcept {
    static TextTableState state;
    return state;
}

}  // namespace

int TextTable::LanguageIndex(const std::string& language) const noexcept {
    for (std::size_t i = 0; i < languages.size(); ++i) {
        if (languages[i] == language) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const TextEntry* TextTable::Find(const std::string& key) const noexcept {
    for (const TextEntry& entry : entries) {
        if (entry.key == key) {
            return &entry;
        }
    }
    return nullptr;
}

bool InstallTextTable(TextTable table, const std::string& language) {
    TextTableState& state = State();

    // 语言代号：给了就用它（必须是表里有的），没给就用第一门。
    std::string chosen = language;
    if (!chosen.empty() && table.LanguageIndex(chosen) < 0) {
        return false;  // 表里没有这门语言：整张表都不装（半装状态更糟）
    }
    if (chosen.empty() && !table.languages.empty()) {
        chosen = table.languages.front();
    }

    state.table = std::move(table);
    state.language = chosen;
    const int index = state.table.LanguageIndex(chosen);
    state.languageIndex = index > 0 ? static_cast<std::size_t>(index) : 0u;
    ++state.version;
    return true;
}

bool SetTextLanguage(const std::string& language) {
    TextTableState& state = State();
    const int index = state.table.LanguageIndex(language);
    if (index < 0) {
        return false;
    }
    if (state.language == language) {
        return false;  // 同一门语言：版本不动（缓存也就不会白作废）
    }
    state.language = language;
    state.languageIndex = static_cast<std::size_t>(index);
    ++state.version;
    return true;
}

const std::string& CurrentTextLanguage() noexcept { return State().language; }

std::size_t CurrentTextLanguageIndex() noexcept {
    return State().languageIndex;
}

std::uint64_t TextLanguageVersion() noexcept { return State().version; }

std::string ResolveText(const std::string& key) {
    TextTableState& state = State();
    if (state.table.entries.empty()) {
        ++state.misses;
        return key;  // 没装表：key 就是字面量
    }
    const TextEntry* entry = state.table.Find(key);
    if (entry == nullptr || state.languageIndex >= entry->translations.size()
        || entry->translations[state.languageIndex].empty()) {
        ++state.misses;
        return key;  // 没这个 key / 这门语言没译文 → 原样显示（不是空白）
    }
    ++state.hits;
    return entry->translations[state.languageIndex];
}

TextTableStats TextTableStatsSnapshot() noexcept {
    const TextTableState& state = State();
    TextTableStats stats;
    stats.hits = state.hits;
    stats.misses = state.misses;
    stats.version = state.version;
    return stats;
}

void ClearTextTable() noexcept {
    TextTableState& state = State();
    state.table = TextTable{};
    state.language.clear();
    state.languageIndex = 0;
    ++state.version;
}

}  // namespace inking
