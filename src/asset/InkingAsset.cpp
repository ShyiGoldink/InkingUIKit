#include <asset/InkingAsset.h>

#include <util/InkingJson.h>

#include <detail/InkLog.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

namespace inking {
namespace {

// ---------------------------------------------------------------------------
// 混淆格式（A11）
//
//    0..3    magic "INKA"
//    4       version（现在是 1）
//    5       keyCheck（密钥散列的低字节——用来把"密钥不对"和"文件坏了"分开）
//    6..7    保留（0）
//    8..11   原始长度（uint32 小端）
//    12..    负载：原始字节 XOR 密钥流
//
// **这是轻量混淆，不是加密**：密钥在 exe 里，谁都读得到。它挡的是"资源包的
// 字节被直接拖出来用 / 一眼看出是什么格式"，不是有意逆向的人（见头文件那段）。
// ---------------------------------------------------------------------------

constexpr std::uint8_t kMagic[4] = {'I', 'N', 'K', 'A'};
constexpr std::uint8_t kVersion = 1;
constexpr std::size_t kHeaderSize = 12;
constexpr std::size_t kMaxPayload = 0xFFFFFFFFu;

constexpr std::uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
constexpr std::uint64_t kGoldenGamma = 0x9E3779B97F4A7C15ULL;

std::uint64_t HashBytes(const std::uint8_t* data, std::size_t size,
                        std::uint64_t seed) {
    std::uint64_t hash = seed;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= kFnvPrime;
    }
    return hash;
}

std::uint64_t HashKey(const std::string& key) {
    return HashBytes(reinterpret_cast<const std::uint8_t*>(key.data()),
                     key.size(), kFnvOffset);
}

/// splitmix64：一个够用的混淆混合函数（不需要密码学强度，见上面的说明）。
std::uint64_t Mix(std::uint64_t value) {
    value += kGoldenGamma;
    std::uint64_t z = value;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

/// 密钥流的第 `index` 个字节（同一密钥 + 同一位置永远同一个值 ⇒ 可逆）。
std::uint8_t KeyByte(std::uint64_t keyHash, std::size_t index) {
    const std::uint64_t block = static_cast<std::uint64_t>(index / 8);
    const std::size_t offset = index % 8;
    const std::uint64_t word = Mix(keyHash + block * kGoldenGamma);
    return static_cast<std::uint8_t>((word >> (offset * 8)) & 0xFFu);
}

void ApplyKeystream(std::uint8_t* data, std::size_t size,
                    std::uint64_t keyHash) {
    for (std::size_t i = 0; i < size; ++i) {
        data[i] = static_cast<std::uint8_t>(data[i] ^ KeyByte(keyHash, i));
    }
}

std::uint32_t ReadU32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0])
         | (static_cast<std::uint32_t>(data[1]) << 8)
         | (static_cast<std::uint32_t>(data[2]) << 16)
         | (static_cast<std::uint32_t>(data[3]) << 24);
}

void WriteU32(std::uint8_t* data, std::uint32_t value) {
    data[0] = static_cast<std::uint8_t>(value & 0xFFu);
    data[1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    data[2] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
    data[3] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
}

// ---------------------------------------------------------------------------
// 路径：全程宽字符（AGENTS.md §6 第 53 条）
//
// 资源名是用户写的（可能带中文），所以窄串 → path 必须走 UTF-8 → 宽字符，
// 不能靠 locale 转换（那会在中文目录里抛 filesystem_error）。
// ---------------------------------------------------------------------------

std::filesystem::path ToPath(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
#ifdef _WIN32
    const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                         static_cast<int>(utf8.size()), nullptr, 0);
    if (size <= 0) {
        return std::filesystem::path(utf8);  // 非法 UTF-8：退化成原样（调用方读不到而已）
    }
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()),
                        wide.data(), size);
    return std::filesystem::path(wide);
#else
    return std::filesystem::path(utf8);
#endif
}

std::string ToUtf8(const std::filesystem::path& path) {
#ifdef _WIN32
    const std::wstring wide = path.wstring();
    if (wide.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                         static_cast<int>(wide.size()), nullptr, 0,
                                         nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string utf8(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                        utf8.data(), size, nullptr, nullptr);
    return utf8;
#else
    return path.string();
#endif
}

/**
 * exe 所在的目录。
 *
 * 与 `InkLog::logFilePath()` 那份**同源**（都是 GetModuleFileNameW / _NSGetExecutablePath
 * / readlink 三条分支）：哪天要抽公共 util 就抽这两处。这里不复用它，是因为那条
 * 路属于日志（改它的风险与收益不成比例）。
 */
std::filesystem::path ExecutableDirectory() {
#ifdef _WIN32
    std::vector<wchar_t> buffer(MAX_PATH);
    DWORD size = GetModuleFileNameW(nullptr, buffer.data(),
                                    static_cast<DWORD>(buffer.size()));
    while (size == buffer.size()) {
        buffer.resize(buffer.size() * 2);
        size = GetModuleFileNameW(nullptr, buffer.data(),
                                  static_cast<DWORD>(buffer.size()));
    }
    if (size > 0) {
        return std::filesystem::path(std::wstring(buffer.data(), size)).parent_path();
    }
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
        return std::filesystem::path(buffer.data()).parent_path();
    }
#else
    std::vector<char> buffer(4096);
    const ssize_t size = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (size > 0) {
        buffer[static_cast<std::size_t>(size)] = '\0';
        return std::filesystem::path(buffer.data()).parent_path();
    }
#endif
    return std::filesystem::current_path();
}

struct State {
    std::mutex mutex;
    AssetConfig config;
    std::string resolvedRoot;
    bool rootResolved = false;
    AssetStats stats;
};

/// 进程级记账：**故意不析构**（`AGENTS.md` §6 第 88 条——别赌静态析构顺序）。
State& state() {
    static State* instance = new State();
    return *instance;
}

AssetStats SnapshotStats() {
    State& shared = state();
    std::lock_guard<std::mutex> guard(shared.mutex);
    return shared.stats;
}

void NoteLoad(std::size_t bytes) {
    State& shared = state();
    std::lock_guard<std::mutex> guard(shared.mutex);
    ++shared.stats.loads;
    shared.stats.bytesLoaded += bytes;
}

void NoteDeobfuscated() {
    State& shared = state();
    std::lock_guard<std::mutex> guard(shared.mutex);
    ++shared.stats.deobfuscated;
}

void NoteMiss(const std::string& relative, const char* why) {
    {
        State& shared = state();
        std::lock_guard<std::mutex> guard(shared.mutex);
        ++shared.stats.misses;
    }
    INK_LOG_ERROR(kAssetModuleName,
                  "读不到资源 \"" + relative + "\"：" + why + "（资源根 "
                      + AssetRoot() + "）");
}

}  // namespace

void AssetConfigure(const AssetConfig& config) {
    State& shared = state();
    std::lock_guard<std::mutex> guard(shared.mutex);
    shared.config = config;
    shared.rootResolved = false;  // 根目录要重算
    shared.resolvedRoot.clear();
}

AssetConfig AssetConfigSnapshot() {
    State& shared = state();
    std::lock_guard<std::mutex> guard(shared.mutex);
    return shared.config;
}

void AssetResetConfig() {
    AssetConfigure(AssetConfig{});
}

std::string AssetRoot() {
    State& shared = state();
    std::lock_guard<std::mutex> guard(shared.mutex);
    if (!shared.rootResolved) {
        std::filesystem::path root = ToPath(shared.config.root);
        if (shared.config.root.empty()) {
            root = ExecutableDirectory() / "assets";
        } else if (root.is_relative()) {
            root = ExecutableDirectory() / root;
        }
        shared.resolvedRoot = ToUtf8(root.lexically_normal());
        shared.rootResolved = true;
    }
    return shared.resolvedRoot;
}

std::string AssetResolve(const std::string& relative) {
    const std::filesystem::path given = ToPath(relative);
    if (given.is_absolute()) {
        return ToUtf8(given.lexically_normal());
    }
    return ToUtf8((ToPath(AssetRoot()) / given).lexically_normal());
}

bool AssetExists(const std::string& relative) {
    std::error_code code;
    return std::filesystem::is_regular_file(ToPath(AssetResolve(relative)), code)
        && !code;
}

std::vector<std::uint8_t> AssetLoad(const std::string& relative) {
    const std::filesystem::path path = ToPath(AssetResolve(relative));

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        NoteMiss(relative, "打不开（文件不在？）");
        return {};
    }
    input.seekg(0, std::ios::end);
    const std::streamoff length = input.tellg();
    input.seekg(0);
    if (length < 0) {
        NoteMiss(relative, "读不出长度");
        return {};
    }

    std::vector<std::uint8_t> raw(static_cast<std::size_t>(length));
    if (length > 0) {
        input.read(reinterpret_cast<char*>(raw.data()), length);
        if (!input) {
            NoteMiss(relative, "读了一半就断了");
            return {};
        }
    }

    // 没有混淆头：原样返回（未混淆的资源照样能读——这是有意的兼容）。
    if (!AssetIsObfuscated(raw.data(), raw.size())) {
        NoteLoad(raw.size());
        return raw;
    }

    const AssetConfig config = AssetConfigSnapshot();
    if (config.key.empty()) {
        NoteMiss(relative, "它是混淆过的资源，但没配 key（AssetConfigure）");
        return {};
    }
    std::vector<std::uint8_t> plain
        = AssetDeobfuscate(raw.data(), raw.size(), config.key);
    if (plain.empty()) {
        NoteMiss(relative, "解不开（key 不对，或者文件头坏了）");
        return {};
    }
    NoteLoad(plain.size());
    NoteDeobfuscated();
    return plain;
}

std::string AssetLoadText(const std::string& relative) {
    const std::vector<std::uint8_t> bytes = AssetLoad(relative);
    return std::string(bytes.begin(), bytes.end());
}

// ---------------------------------------------------------------------------
// `inking::json::ParseAsset`：读资源根里的 json 再解析（0.6.5 T-15）
// ---------------------------------------------------------------------------
//
// **为什么定义在资产层而不是 `src/util/InkingJson.cpp`**：那份 util 源也被生成器
// 编一份（`cmake/inkgen.cmake`，工具不链接 DLL），而生成器**不该**为了一个它不调的
// 包装函数去拖资产层（那里有 SDL 的文件 IO）。放这里，边界就干净：
// `util/` 只认字符串，谁要"从资源根读"谁自己承担资产层。

// 注意：这里已经在 `namespace inking` 里面，所以写 `namespace json {` 就是
// `inking::json`。写成 `namespace inking::json` 会变成 inking::inking::json
// （GCC 不报错，只在用到名字时才暴露——第一次就是这么红的）。
namespace json {

Result ParseAsset(const std::string& relative) {
    Result result;
    const std::string text = AssetLoadText(relative);
    if (text.empty()) {
        // 读不到与"文件是空的"在这里分不开（`AssetLoadText` 两种都返回空串），
        // 所以文案把两种可能都说出来——猜一个反而误导。
        result.ok = false;
        result.error = "读不到资源或文件是空的：" + relative + "（资源根 "
                       + AssetRoot() + "）";
        return result;
    }

    result = Parse(text);
    if (!result.ok) {
        // 与工具的 `LoadJsonFile` 同一条规矩：错误里已有行列号，只补来源前缀。
        result.error = relative + "：" + result.error;
    }
    return result;
}

}  // namespace json

bool AssetIsObfuscated(const std::uint8_t* data, std::size_t size) noexcept {    if (data == nullptr || size < kHeaderSize) {
        return false;
    }
    return std::memcmp(data, kMagic, sizeof(kMagic)) == 0;
}

std::vector<std::uint8_t> AssetObfuscate(const std::uint8_t* data,
                                         std::size_t size,
                                         const std::string& key) {
    if (data == nullptr || size == 0 || size > kMaxPayload) {
        return {};
    }
    if (key.empty()) {
        // "加密得先给密钥"：空密钥会让输出看起来像"混淆过了"，但任何人拿空 key
        // 都能解开——那种虚假的安全感比不加密更糟。
        INK_LOG_ERROR(kAssetModuleName, "混淆需要非空 key（空 key 等于没混淆）");
        return {};
    }

    const std::uint64_t keyHash = HashKey(key);
    std::vector<std::uint8_t> out(kHeaderSize + size, 0);
    std::memcpy(out.data(), kMagic, sizeof(kMagic));
    out[4] = kVersion;
    out[5] = static_cast<std::uint8_t>(keyHash & 0xFFu);
    out[6] = 0;
    out[7] = 0;
    WriteU32(out.data() + 8, static_cast<std::uint32_t>(size));
    std::memcpy(out.data() + kHeaderSize, data, size);
    ApplyKeystream(out.data() + kHeaderSize, size, keyHash);
    return out;
}

std::vector<std::uint8_t> AssetDeobfuscate(const std::uint8_t* data,
                                           std::size_t size,
                                           const std::string& key) {
    if (data == nullptr || size == 0) {
        return {};
    }

    // 没有 magic：**原样拷贝**（"未混淆也能读"这条兼容路）。
    if (!AssetIsObfuscated(data, size)) {
        return std::vector<std::uint8_t>(data, data + size);
    }
    if (data[4] != kVersion) {
        INK_LOG_ERROR(kAssetModuleName,
                      "混淆版本不认识（文件是 " + std::to_string(data[4])
                          + "，本程序认得 " + std::to_string(kVersion) + "）");
        return {};
    }
    if (key.empty()) {
        return {};
    }

    const std::uint64_t keyHash = HashKey(key);
    if (data[5] != static_cast<std::uint8_t>(keyHash & 0xFFu)) {
        // 密钥校验字节不符：**这是"密钥不对"**，不是"文件坏了"——分开报，
        // 因为两者的修法完全不同（换 key vs 重新打包）。
        INK_LOG_ERROR(kAssetModuleName, "key 不对（文件头里的校验字节不匹配）");
        return {};
    }

    const std::uint32_t expected = ReadU32(data + 8);
    if (kHeaderSize + static_cast<std::size_t>(expected) != size) {
        INK_LOG_ERROR(kAssetModuleName,
                      "长度字段不合法（文件头写 " + std::to_string(expected)
                          + "，实际负载 "
                          + std::to_string(size - kHeaderSize) + "）");
        return {};
    }

    std::vector<std::uint8_t> plain(data + kHeaderSize, data + size);
    ApplyKeystream(plain.data(), plain.size(), keyHash);
    return plain;
}

bool AssetPackFile(const std::string& sourcePath, const std::string& outPath,
                   const std::string& key) {
    std::ifstream input(ToPath(sourcePath), std::ios::binary);
    if (!input) {
        INK_LOG_ERROR(kAssetModuleName, "打包：打不开源文件 " + sourcePath);
        return false;
    }
    input.seekg(0, std::ios::end);
    const std::streamoff length = input.tellg();
    input.seekg(0);
    if (length <= 0) {
        INK_LOG_ERROR(kAssetModuleName, "打包：源文件是空的（或读不出长度）"
                                            + sourcePath);
        return false;
    }
    std::vector<std::uint8_t> raw(static_cast<std::size_t>(length));
    input.read(reinterpret_cast<char*>(raw.data()), length);
    if (!input) {
        INK_LOG_ERROR(kAssetModuleName, "打包：读源文件失败 " + sourcePath);
        return false;
    }
    input.close();

    const std::vector<std::uint8_t> packed
        = AssetObfuscate(raw.data(), raw.size(), key);
    if (packed.empty()) {
        return false;
    }

    const std::filesystem::path out = ToPath(outPath);
    std::error_code code;
    if (out.has_parent_path()) {
        std::filesystem::create_directories(out.parent_path(), code);
    }
    std::ofstream output(out, std::ios::binary);
    if (!output) {
        INK_LOG_ERROR(kAssetModuleName, "打包：写不出目标文件 " + outPath);
        return false;
    }
    output.write(reinterpret_cast<const char*>(packed.data()),
                 static_cast<std::streamsize>(packed.size()));
    if (!output) {
        INK_LOG_ERROR(kAssetModuleName, "打包：写目标文件失败 " + outPath);
        return false;
    }
    INK_LOG_PASS(kAssetModuleName,
                 "打包完成：" + sourcePath + " → " + outPath + "（"
                     + std::to_string(raw.size()) + " 字节明文 → "
                     + std::to_string(packed.size()) + " 字节混淆）");
    return true;
}

bool AssetUnpackFile(const std::string& sourcePath, const std::string& outPath,
                     const std::string& key) {
    std::ifstream input(ToPath(sourcePath), std::ios::binary);
    if (!input) {
        INK_LOG_ERROR(kAssetModuleName, "还原：打不开源文件 " + sourcePath);
        return false;
    }
    input.seekg(0, std::ios::end);
    const std::streamoff length = input.tellg();
    input.seekg(0);
    if (length <= 0) {
        INK_LOG_ERROR(kAssetModuleName, "还原：源文件是空的 " + sourcePath);
        return false;
    }
    std::vector<std::uint8_t> raw(static_cast<std::size_t>(length));
    input.read(reinterpret_cast<char*>(raw.data()), length);
    input.close();

    const std::vector<std::uint8_t> plain
        = AssetDeobfuscate(raw.data(), raw.size(), key);
    if (plain.empty()) {
        return false;
    }

    const std::filesystem::path out = ToPath(outPath);
    std::error_code code;
    if (out.has_parent_path()) {
        std::filesystem::create_directories(out.parent_path(), code);
    }
    std::ofstream output(out, std::ios::binary);
    if (!output) {
        INK_LOG_ERROR(kAssetModuleName, "还原：写不出目标文件 " + outPath);
        return false;
    }
    output.write(reinterpret_cast<const char*>(plain.data()),
                 static_cast<std::streamsize>(plain.size()));
    return static_cast<bool>(output);
}

AssetStats AssetStatsSnapshot() { return SnapshotStats(); }

void AssetResetStats() {
    State& shared = state();
    std::lock_guard<std::mutex> guard(shared.mutex);
    shared.stats = AssetStats{};
}

}  // namespace inking
