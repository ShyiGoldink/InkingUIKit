#include <asset/InkingTexture.h>

#include <asset/InkingAsset.h>
#include <detail/InkLog.h>
#include <render/InkingText.h>
#include <render/InkingTextureResolve.h>

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <utility>
#include <vector>

// 句柄注册表（dev010 T-01）。
//
// 这一层**只管三件事**：id ↔ 路径的映射、字节的生命周期、调用计数。纹理怎么建、
// 怎么钉住、怎么被淘汰，全在 `render/InkingText.cpp`（那是唯一知道纹理表结构的地方）。
//
// 为什么字节留在这里而不是交给纹理层：`AcquireImageTexture` 的缓存键是"名字 + 字节数"，
// 它**不拥有**字节；纹理被淘汰之后要重建就得重新有字节。留在注册表里，"重新建出来"
// 是一次内存拷贝能解决的事，不用再读一遍磁盘。

namespace {

struct PinnedEntry {
    std::string path;                ///< 资源相对路径（句柄的键，也是缓存键的一半）
    std::vector<std::uint8_t> bytes; ///< 自己拥有：淘汰后重建要用
    int width = 0;
    int height = 0;
    /**
     * 还有几份持有。同路径 Load 返回同一个 id，但**每次 Load 都是一份持有**
     * （公开头的契约：Load 一次 = 持有一份，Release 一次放掉一份）。
     * 减到 0 才真正出表——否则"两次 Load、先放掉一次"会把另一个调用方手上的
     * 句柄一起废掉。
     */
    int refs = 1;
};

struct Registry {
    std::unordered_map<std::uint32_t, PinnedEntry> entries;
    std::unordered_map<std::string, std::uint32_t> byPath;
    std::uint32_t nextId = 1;  ///< **不回收**：旧 id 撞上新纹理比失效更难查
    InkingTextureStats stats;
};

/// 进程唯一槽位：函数内静态量 + 定义在 .cpp（`AGENTS.md` §6 第 12 条的动态库那条）。
Registry& registry() {
    static Registry instance;
    return instance;
}

/**
 * 头 512 字节里找 `<svg`：SVG 是文本格式，根元素一定在开头。
 *
 * 有意的粗判——精确的做法是走 XML 解析，而这里唯一要回答的问题是
 * "要不要直接拒绝"，粗判错判的后果也只是一条 WARN。
 */
bool LooksLikeSvgBytes(const std::vector<std::uint8_t>& bytes) noexcept {
    if (bytes.empty()) {
        return false;
    }
    // 拷进带终止符的栈缓冲：直接对 `bytes.data()` 用 `strstr` 会越过末尾读
    // （PNG 前几个字节里没有 '\0' 是常态）。
    const std::size_t limit = std::min<std::size_t>(bytes.size(), 512);
    char head[513];
    std::memcpy(head, bytes.data(), limit);
    head[limit] = '\0';

    std::size_t start = 0;
    if (limit >= 3 && static_cast<std::uint8_t>(head[0]) == 0xEF
        && static_cast<std::uint8_t>(head[1]) == 0xBB
        && static_cast<std::uint8_t>(head[2]) == 0xBF) {
        start = 3;
    }
    while (start < limit
           && (head[start] == ' ' || head[start] == '\t' || head[start] == '\r'
               || head[start] == '\n')) {
        ++start;
    }
    return std::strstr(head + start, "<svg") != nullptr;
}

/// `pinnedBytes` 是派生量：每次增删后重算，比在四处分别加减更不容易错。
void refreshPinnedBytes() {
    Registry& reg = registry();
    std::uint64_t total = 0;
    for (const auto& item : reg.entries) {
        // 口径与纹理账本一致：宽 × 高 × 4（RGBA8 上界），见 `TextureEntry::bytes`。
        total += static_cast<std::uint64_t>(item.second.width)
               * static_cast<std::uint64_t>(item.second.height) * 4ull;
    }
    reg.stats.pinnedBytes = total;
}

}  // namespace

InkingTextureHandle InkingLoadTexture(const std::string& relativeAssetPath) {
    Registry& reg = registry();
    if (relativeAssetPath.empty()) {
        ++reg.stats.loadFailures;
        INK_LOG_ERROR(InkingTextureModuleName, "纹理句柄：路径为空");
        return InkingTextureHandle{};
    }

    if (const auto found = reg.byPath.find(relativeAssetPath);
        found != reg.byPath.end()) {
        ++reg.stats.cacheHits;
        const auto entry = reg.entries.find(found->second);
        if (entry != reg.entries.end()) {
            ++entry->second.refs;  // 同一张图、多一份持有
            ++reg.stats.liveHandles;
            return InkingTextureHandle{found->second};
        }
        // 表里路径还在、条目却没了（理论上不会发生）：清掉脏键继续走新建。
        reg.byPath.erase(found);
    }

    std::vector<std::uint8_t> bytes = InkingAssetLoad(relativeAssetPath);
    if (bytes.empty()) {
        ++reg.stats.loadFailures;
        INK_LOG_ERROR(InkingTextureModuleName,
                      "纹理句柄：读不到资源 " + relativeAssetPath);
        return InkingTextureHandle{};
    }
    if (LooksLikeSvgBytes(bytes)) {
        // 不是"没实现"，是**模型不成立**：SVG 要按目标尺寸光栅化，
        // 而句柄承诺的是一张固定纹理。宽度一变就得重烘一张，那件事归组件层
        // （`InkingStaticImage` 的 SVG 路径按组件框烘，见 `PaintImage`）。
        ++reg.stats.loadFailures;
        INK_LOG_WARN(InkingTextureModuleName,
                     "纹理句柄不支持 SVG（需要按目标尺寸光栅化）："
                         + relativeAssetPath);
        return InkingTextureHandle{};
    }

    // Load 阶段就把"这张图能不能解"验证掉：等到第一次提交才发现，
    // 表现是"画面上少一块"，而那时调用方已经拿着一个看起来有效的句柄跑了几帧。
    InkingImageBytes view{bytes.data(), bytes.size(), relativeAssetPath};
    int width = 0;
    int height = 0;
    if (!inking_detail::ProbeImageSize(view, &width, &height) || width <= 0
        || height <= 0) {
        ++reg.stats.loadFailures;
        INK_LOG_ERROR(InkingTextureModuleName,
                      "纹理句柄：解不出图片 " + relativeAssetPath);
        return InkingTextureHandle{};
    }

    const std::uint32_t id = reg.nextId++;
    PinnedEntry entry;
    entry.path = relativeAssetPath;
    entry.bytes = std::move(bytes);
    entry.width = width;
    entry.height = height;
    reg.entries.emplace(id, std::move(entry));
    reg.byPath.emplace(relativeAssetPath, id);

    ++reg.stats.loads;
    ++reg.stats.liveHandles;
    refreshPinnedBytes();
    return InkingTextureHandle{id};
}

void InkingReleaseTexture(InkingTextureHandle handle) {
    Registry& reg = registry();
    ++reg.stats.releases;
    if (!handle.Valid()) {
        return;
    }
    const auto found = reg.entries.find(handle.id);
    if (found == reg.entries.end()) {
        return;  // 已经释放过 / 是别的进程的 id：空操作
    }
    if (--found->second.refs > 0) {
        // 还有别人拿着同一份：**不动纹理、不解钉**，只减这一份持有。
        if (reg.stats.liveHandles > 0) {
            --reg.stats.liveHandles;
        }
        return;
    }
    // 最后一份：先解钉再删。纹理表里那条还在（别处可能还在用同一张图），
    // 只是重新可淘汰。
    InkingImageBytes view{found->second.bytes.data(), found->second.bytes.size(),
                          found->second.path};
    inking_detail::UnpinTexture(view);

    reg.byPath.erase(found->second.path);
    reg.entries.erase(found);
    if (reg.stats.liveHandles > 0) {
        --reg.stats.liveHandles;
    }
    refreshPinnedBytes();
}

bool InkingTextureValid(InkingTextureHandle handle) {
    if (!handle.Valid()) {
        return false;
    }
    return registry().entries.find(handle.id) != registry().entries.end();
}

bool InkingTextureSize(InkingTextureHandle handle, int* width, int* height) {
    if (width != nullptr) {
        *width = 0;
    }
    if (height != nullptr) {
        *height = 0;
    }
    const auto found = registry().entries.find(handle.id);
    if (found == registry().entries.end()) {
        return false;
    }
    if (width != nullptr) {
        *width = found->second.width;
    }
    if (height != nullptr) {
        *height = found->second.height;
    }
    return true;
}

InkingTextureStats InkingTextureStatsSnapshot() noexcept {
    return registry().stats;
}

void InkingTextureReleaseAll() noexcept {
    Registry& reg = registry();
    for (auto& item : reg.entries) {
        InkingImageBytes view{item.second.bytes.data(), item.second.bytes.size(),
                              item.second.path};
        inking_detail::UnpinTexture(view);
    }
    reg.entries.clear();
    reg.byPath.clear();
    reg.stats.liveHandles = 0;
    reg.stats.pinnedBytes = 0;
}

namespace inking_detail {

SDL_Texture* ResolveTextureHandle(std::uint32_t id, SDL_Renderer* renderer,
                                  int* width, int* height) {
    if (width != nullptr) {
        *width = 0;
    }
    if (height != nullptr) {
        *height = 0;
    }
    if (renderer == nullptr || id == 0) {
        return nullptr;
    }
    Registry& reg = registry();
    const auto found = reg.entries.find(id);
    if (found == reg.entries.end()) {
        // 已释放 / 换过渲染器：**不报错**。一层里几千个图元，一个坏句柄
        // 让整屏黑掉是最糟的失败方式。调用方（提交层）自己决定要不要提醒。
        ++reg.stats.staleSubmits;
        return nullptr;
    }

    const InkingImageBytes view{found->second.bytes.data(),
                                found->second.bytes.size(), found->second.path};
    int textureWidth = 0;
    int textureHeight = 0;
    SDL_Texture* texture = AcquirePinnedTexture(renderer, view, &textureWidth,
                                                &textureHeight);
    if (texture == nullptr) {
        ++reg.stats.loadFailures;
        return nullptr;
    }
    if (width != nullptr) {
        *width = textureWidth;
    }
    if (height != nullptr) {
        *height = textureHeight;
    }
    return texture;
}

}  // namespace inking_detail
