#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace inking {

/// 这个模块的名字（日志里用；和别的模块一个约定）。
inline constexpr const char* kAssetModuleName = "InkingAsset";

/**
 * @brief 资源根目录与**轻量混淆**密钥（A11）。
 *
 * 解决的问题只有一个：**别把所有图片都烘进 exe**。走这条路之后，配置里写的是
 * **相对路径**（相对资源根，通常是 exe 旁边的 `assets/`），文件在**运行期**读进来
 * ——换图不用重编，exe 也不随图片数量膨胀。
 *
 * ```cpp
 * inking::AssetConfigure({.root = "", .key = "my-obfuscation-key"});
 * // root 留空 = exe 目录下的 assets/（getModuleFileNameW 那条宽字符路，见
 * // AGENTS.md §6 第 53 条：中文目录里窄串构造 path 会抛）
 * const std::vector<std::uint8_t> bytes = inking::AssetLoad("images/card.png");
 * ```
 *
 * **`key` 是"轻量混淆"，不是加密**：它是可逆的异或流（+ 一个文件头），挡的是
 * "把资源包的字节直接拖出来用 / 一眼看出是什么格式"，**挡不住有意逆向的人**
 * （密钥在 exe 里，谁都读得到）。要真安全就得走平台加密 / 服务端下发，那不在
 * 这个模块的范围内。**空 key = 不解密**：未混淆的文件照样能读。
 */
struct AssetConfig {
    /// 资源根目录。**空 = exe 目录下的 `assets/`**；相对路径按 exe 目录解析。
    std::string root;

    /// 混淆密钥。空 = 不启用（读未混淆的文件）。
    std::string key;
};

/**
 * 配置资源根与密钥（进程级，幂等）。
 *
 * 它会**忘记**上一次的 `root` 派生值，所以自检里可以反复调。**线程安全**：
 * 内部一把锁（资源加载可能来自异步任务线程，见 `InkingAudio` 的异步加载）。
 */
void AssetConfigure(const AssetConfig& config);

/// 现在的配置（`root` 是**原样**的那份，不是解析后的绝对路径）。
AssetConfig AssetConfigSnapshot();

/// 恢复默认（空 root = exe 下的 `assets/`、空 key）——自检用。
void AssetResetConfig();

/**
 * 资源根的**绝对路径**（已解析：`root` 为空时就是 exe 目录下的 `assets/`）。
 *
 * 注意它**不保证目录存在**：资源是外置的，"没有 assets 目录"是一个正常状态
 * （纯色界面不需要资源），读不到就返回空字节 + 记一次 miss。
 */
std::string AssetRoot();

/// 把配置里的相对路径解析成绝对路径；传进来的**绝对路径原样返回**（便于读外部文件）。
std::string AssetResolve(const std::string& relative);

/// 这个资源在不在（走文件系统查询；用于"缺图时报清楚一点"）。
bool AssetExists(const std::string& relative);

/**
 * @brief 读一个资源（**自动识别混淆头**）。
 *
 * 读不到 / 密钥不对时返回**空 vector**，并记一次 miss —— 不抛异常、不中止：
 * 资源缺失该表现成"这块没画出来 + 日志里一条"，而不是整个程序挂掉。
 * 密钥不对与"文件不存在"在返回上一样，但日志文案不同（那条最难查，见
 * `AssetDeobfuscate`）。
 */
std::vector<std::uint8_t> AssetLoad(const std::string& relative);

/// 读一个文本资源（配置 / json / shader）。读不到返回空串。
std::string AssetLoadText(const std::string& relative);

/// 这段字节带不带混淆头（4 字节 magic `INKA`）。
bool AssetIsObfuscated(const std::uint8_t* data, std::size_t size) noexcept;

/**
 * 混淆一段字节：`INKA` + 版本 + 密钥校验字节 + 原始长度 + 异或流。
 *
 * `key` 为空返回空（"加密得先给密钥"）；`size` 为空或超过 4 GiB 也返回空。
 */
std::vector<std::uint8_t> AssetObfuscate(const std::uint8_t* data,
                                         std::size_t size,
                                         const std::string& key);

/**
 * 还原一段字节：**带 magic 就解**，不带就原样拷贝（兼容未混淆的文件）。
 *
 * 下面三种都返回空 vector（且**分得清**，好写日志）：不是 `INKA` 头但版本对不上、
 * 密钥校验字节不符（**密钥不对**）、长度字段不合法。
 */
std::vector<std::uint8_t> AssetDeobfuscate(const std::uint8_t* data,
                                           std::size_t size,
                                           const std::string& key);

/// 一步打包：读源文件 → 混淆 → 写到目标路径（目录不存在会建）。
bool AssetPackFile(const std::string& sourcePath, const std::string& outPath,
                   const std::string& key);

/// 一步还原：读混淆文件 → 解开 → 写到目标路径。用来核对"打包进去的到底是什么"。
bool AssetUnpackFile(const std::string& sourcePath, const std::string& outPath,
                     const std::string& key);

/**
 * @brief 观测口：加载次数 / 未命中 / 解开过几次 / 读过多少字节。
 *
 * "资源在读"这件事功能上不一定看得出来（比如同一张图命中纹理缓存），
 * 有计数才钉得住——`AGENTS.md` §6 第 40 条那条分工。
 */
struct AssetStats {
    std::uint64_t loads = 0;         ///< `AssetLoad` / `AssetLoadText` 成功次数
    std::uint64_t misses = 0;        ///< 读不到 / 密钥不对 / 头损坏
    std::uint64_t deobfuscated = 0;  ///< 其中带混淆头、且成功解开的次数
    std::uint64_t bytesLoaded = 0;   ///< 成功读出的**明文**字节数
};

AssetStats AssetStatsSnapshot();

/// 计数清零（自检用；不影响配置）。
void AssetResetStats();

}  // namespace inking
