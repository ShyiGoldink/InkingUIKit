#include <prefab/PrefabTheme.h>

#include <asset/InkingAsset.h>

namespace inking::prefab::theme {

std::string DefaultFontPath() {
    // 函数内静态量：解析一次、之后白拿（也避开静态初始化顺序问题）。
    static const std::string resolved = [] {
        static const char* const kCandidates[] = {
            "font/ResourceHanRoundedCN-Regular.ttf",  // 资源圆体 CN（中文圆体首选）
            "font/ZenMaruGothic-Regular.ttf",         // 禅圆体
            "font/MPLUSRounded1c-Regular.ttf",        // M PLUS Rounded 1c
        };
        for (const char* name : kCandidates) {
            // `AssetExists` / `AssetResolve` 内部全程宽字符（`AGENTS.md` §6 第 53 条）。
            if (AssetExists(name)) {
                return AssetResolve(name);
            }
        }
        return std::string{};  // 没有就用机器默认字体（不是不画）
    }();
    return resolved;
}

}  // namespace inking::prefab::theme
