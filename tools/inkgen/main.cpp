// inkgen：CXXCSS 配置 → C++ 代码的生成器。
//
// 用法：
//   inkgen --out-dir <目录> [--namespace inking::cxxcss] [--header button_service.h] <file.json>...
//   inkgen --validate <file.json>...
//
// 它是**构建期工具**：只依赖标准库（连 SDL 都不碰），所以在任何环境下都能编。
// 产物只落 build 目录，源码树保持干净（docs/AGENTS.md §3 第 6 条）。
//
// **组件类型由目录名决定**（CXXCSS.md §1 第 1 条）：`Button/` 下的生成按钮类、
// `Panel/` 下的生成面板类，共用同一套校验 / 重名 / 产物规则。加一个组件类型 =
// 生成器里加一条目录名映射 + 一个 Loader + Emit 里一段
// （要改哪几处写在 tools/inkgen/Emit.h 顶上）。
//
// 只生成**组件**：配置烘成字面量、每个名字一个类，类型即名字，构造不查表。
// **场景不生成**——评估过（TaskGuide 的 T-7 那段记录）：收益用手写就能拿到
// （成员 + `{this}` + 一行场景名常量），而生成它的代价是固定的。

#include "Cxxcss.h"
#include "Emit.h"
#include "Fields.h"
#include "Json.h"
#include "PathUtil.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {

/**
 * `--dir` 的扫描：`<根>/<组件目录>/*.json` + `<根>/config.json`。
 *
 * 三条规矩：
 *   1. **组件目录由 `TryComponentKindFromPath` 判定**（"目录名 = 组件类型"的
 *      唯一来源）——认哪些目录由生成器决定，这里不抄第二份目录名列表；
 *   2. `*.example.json` 跳过（与显式列表同一条约定）；
 *   3. 结果**排序**：扫描顺序在文件系统上是任意的，而重名报错要指到"第一个"
 *      文件——不排序的话同一个工程在不同机器上报不同的文件。
 *
 * 全局配置还认"直接躺在 CXXCSS 根下"那种位置（`CXXCSS.md` §1），所以根下的
 * `config.json` 单独收一条。
 *
 * 扫到的清单由调用方打印（`--dir` 模式必须让人看得见"到底扫了哪些"）。
 */
std::vector<std::string> ScanConfigDir(const std::string& root) {
    namespace fs = std::filesystem;
    std::vector<std::string> files;
    std::error_code error;

    const auto isExample = [](const std::string& path) {
        const std::string stem = inkgen::FileStem(path);
        return stem.size() > 8 && stem.compare(stem.size() - 8, 8, ".example") == 0;
    };
    const auto collectJson = [&files, &isExample](const fs::path& dir) {
        std::error_code inner;
        for (const fs::directory_entry& entry : fs::directory_iterator(dir, inner)) {
            if (!entry.is_regular_file(inner)) {
                continue;
            }
            if (entry.path().extension() != ".json") {
                continue;
            }
            const std::string path = inkgen::ToNarrow(entry.path());
            if (isExample(path)) {
                continue;
            }
            files.push_back(path);
        }
    };

    for (const fs::directory_entry& entry :
         fs::directory_iterator(inkgen::ToPath(root), error)) {
        if (!entry.is_directory(error)) {
            continue;
        }
        inkgen::ComponentKind kind = inkgen::ComponentKind::Button;
        // 探针文件名只是为了拿父目录名（函数只看父目录）。
        if (inkgen::TryComponentKindFromPath(
                inkgen::ToNarrow(entry.path()) + "/probe.json", kind)) {
            collectJson(entry.path());
            continue;
        }
        // 不认识的目录：里面有 json 就提示一句，免得"配置放错目录"被静默跳过。
        std::error_code inner;
        for (const fs::directory_entry& child :
             fs::directory_iterator(entry.path(), inner)) {
            if (child.is_regular_file(inner)
                && child.path().extension() == ".json") {
                std::printf("[提示] %s 不是组件目录（目录名决定组件类型），里面的 "
                            "json 已跳过\n",
                            inkgen::ToNarrow(entry.path()).c_str());
                break;
            }
        }
    }

    const fs::path rootConfig = inkgen::ToPath(root) / "config.json";
    if (fs::is_regular_file(rootConfig, error)) {
        files.push_back(inkgen::ToNarrow(rootConfig));
    }

    std::sort(files.begin(), files.end());
    return files;
}

void PrintUsage() {
    std::printf(
        "inkgen —— 把 CXXCSS/<组件类型>/*.json 生成成 C++ 代码\n"
        "\n"
        "用法：\n"
        "  inkgen --out-dir <目录> [--namespace <ns>] [--header <名>] <file.json>...\n"
        "  inkgen --out-dir <目录> [--namespace <ns>] [--header <名>] --dir <CXXCSS 根>\n"
        "  inkgen --validate <file.json>...\n"
        "  inkgen --validate --dir <CXXCSS 根>\n"
        "  inkgen --dump-fields\n"
        "\n"
        "选项：\n"
        "  --out-dir <目录>   产物目录（约定指向 build/，不存在会创建）\n"
        "  --namespace <ns>   生成代码的命名空间，默认 inking::cxxcss\n"
        "  --header <名>      生成的头文件名，默认 button_service.h\n"
        "  --dir <根>         扫 <根>/<组件目录>/*.json（+ 根下 config.json），\n"
        "                     与显式文件列表**互斥**。19 份配置只占 1 个参数，\n"
        "                     所以工程放在中文路径下也不会撞上 Windows 那条\n"
        "                     8191 字符的命令行上限（CMake 的 .bat 绕行会废掉\n"
        "                     非 ASCII 路径）\n"
        "  --validate         只校验，不写文件\n"
        "  --dump-fields      输出「组件 × 段 × 字段」的 Markdown 表（CXXCSS.md\n"
        "                     §9 就是它生成的；新增字段忘记同步文档时 ctest 会红）\n"
        "  -h, --help         显示这段帮助\n"
        "\n"
        "组件类型由**目录名**决定（CXXCSS.md §1）：Button/buttons → 按钮，\n"
        "Panel/panels → 面板，Label/labels → 静态标签，Text/texts → 语言表\n"
        "（它不是组件，是「占位符 → 各语言的串」那张全局表，只允许一份），\n"
        "Atlas/atlases → 图集描述（一张图多个动画，0.6.5），\n"
        "Theme/themes → 主题表（语义色 / 语义字号，是**生成器的输入**，\n"
        "配置里的 $token 在生成期就换成具体值——换主题要重新生成）；\n"
        "Config/configs → 全局配置（设计画布尺寸，只允许一份；也可以直接\n"
        "放在 CXXCSS/ 根下当 config.json）；\n"
        "别的目录名会报错。\n"
        "退出码：0 全部成功；1 有错误（错误逐条打印）。\n"
        "注意：*.example.json 按约定不参与生成。\n");
}

}  // namespace

int main(int argc, char** argv) {
    inkgen::EmitOptions options;
    bool validateOnly = false;
    bool dumpFields = false;
    bool sawOutDir = false;
    std::string dirPath;
    bool sawDir = false;
    std::vector<std::string> inputs;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            PrintUsage();
            return 0;
        }
        if (arg == "--validate") {
            validateOnly = true;
            continue;
        }
        if (arg == "--dump-fields") {
            // 纯查询：不需要输入文件，也不写文件。所以它必须在"没有输入"的
            // 校验**之前**返回（T-08）。
            dumpFields = true;
            continue;
        }
        if (arg == "--out-dir" || arg == "--namespace" || arg == "--header"
            || arg == "--dir") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "[错误] %s 后面缺少值\n", arg.c_str());
                return 1;
            }
            const std::string value = argv[++i];
            if (arg == "--out-dir") {
                options.outDir = value;
                sawOutDir = true;
            } else if (arg == "--dir") {
                dirPath = value;
                sawDir = true;
            } else if (arg == "--namespace") {
                options.nameSpace = value;
            } else {
                options.headerName = value;
            }
            continue;
        }
        if (!arg.empty() && arg[0] == '-' && arg != "-") {
            std::fprintf(stderr, "[错误] 不认识的选项：%s\n", arg.c_str());
            PrintUsage();
            return 1;
        }
        inputs.push_back(arg);
    }

    if (dumpFields) {
        // Markdown 表直接打到 stdout（ctest 拿它与 CXXCSS.md 的锚点块比对）。
        std::printf("%s", inkgen::fields::DumpFieldTable().c_str());
        return 0;
    }

    // `--dir` 与显式文件列表**互斥**：同时给的话"到底扫了哪些"说不清，
    // 而生成物的差异（多一个 / 少一个组件）在运行期很难反推。宁可报错。
    if (sawDir && !inputs.empty()) {
        std::fprintf(stderr,
                     "[错误] --dir 与显式文件列表不能同时给（一个是扫目录、"
                     "一个是点名，混用之后\"到底生成了哪些\"说不清）\n");
        return 1;
    }
    if (sawDir) {
        const std::filesystem::path dir = inkgen::ToPath(dirPath);
        std::error_code error;
        if (!std::filesystem::is_directory(dir, error)) {
            std::fprintf(stderr, "[错误] --dir 指的不是目录：%s\n",
                         dirPath.c_str());
            return 1;
        }
        inputs = ScanConfigDir(dirPath);
        if (inputs.empty()) {
            std::fprintf(stderr,
                         "[错误] %s 下没有扫到任何配置（认的目录：Button / Panel "
                         "/ Label / Image / Atlas / Text / Theme，外加根下 config.json；"
                         "*.example.json 不参与生成）\n",
                         dirPath.c_str());
            return 1;
        }
        // 打印实际扫描清单（`--dir` 的风险就是"扫到的比你以为的多"）。
        std::printf("[扫描] %s：%zu 份配置\n", dirPath.c_str(), inputs.size());
        for (const std::string& path : inputs) {
            std::printf("        %s\n", path.c_str());
        }
    }

    if (inputs.empty()) {
        std::fprintf(stderr, "[错误] 没有输入文件\n\n");
        PrintUsage();
        return 1;
    }
    if (!validateOnly && !sawOutDir) {
        std::fprintf(stderr,
                     "[错误] 生成模式必须给 --out-dir（只校验请加 --validate）\n\n");
        PrintUsage();
        return 1;
    }

    // 用户单独点着示例文件说"生成这个"时提醒一句：它会被跳过。
    for (const std::string& path : inputs) {
        const std::string stem = inkgen::FileStem(path);
        if (stem.size() > 8 && stem.compare(stem.size() - 8, 8, ".example") == 0) {
            std::printf("[提示] %s 以 .example.json 结尾，按约定不参与生成，已跳过\n",
                        path.c_str());
        }
    }

    const inkgen::LoadResult loaded = inkgen::LoadConfigs(inputs);

    for (const inkgen::ButtonConfig& button : loaded.buttons) {
        std::printf("[配置] %s  %dx%d  %s  shape=%d  显式外观段=%d\n",
                    button.name.c_str(), button.data.width,
                    button.data.height,
                    button.dynamic ? "动态按钮" : "静态按钮",
                    static_cast<int>(button.data.shape.kind),
                    (button.data.hoverInheritsNormal ? 1 : 2));
        for (const std::string& warning : button.warnings) {
            std::printf("[警告] %s：%s\n", button.filePath.c_str(),
                        warning.c_str());
        }
    }
    for (const inkgen::PanelConfig& panel : loaded.panels) {
        std::printf("[配置] %s  %dx%d  静态面板  startOpen=%d  clipChildren=%d\n",
                    panel.name.c_str(), panel.data.width, panel.data.height,
                    panel.data.startOpen ? 1 : 0,
                    panel.data.clipChildren ? 1 : 0);
        for (const std::string& warning : panel.warnings) {
            std::printf("[警告] %s：%s\n", panel.filePath.c_str(),
                        warning.c_str());
        }
    }
    for (const inkgen::LabelConfig& label : loaded.labels) {
        std::printf("[配置] %s  %dx%d  静态标签  text=\"%s\"  "
                    "align=%d  wrap=%d  overflow=%d  maxLines=%d\n",
                    label.name.c_str(), label.data.width, label.data.height,
                    label.data.text.c_str(),
                    static_cast<int>(label.data.style.align),
                    static_cast<int>(label.data.wrap),
                    static_cast<int>(label.data.overflow),
                    label.data.maxLines);
        for (const std::string& warning : label.warnings) {
            std::printf("[警告] %s：%s\n", label.filePath.c_str(),
                        warning.c_str());
        }
    }
    for (const inkgen::AtlasConfig& atlas : loaded.atlases) {
        std::printf("[配置] %s  图集 %zu 个动画  %s\n", atlas.name.c_str(),
                    atlas.data.animations.size(), atlas.identifier.c_str());
        for (const inking::AtlasAnimation& animation : atlas.data.animations) {
            std::printf("         · %-8s %zu 帧  %.0ffps  loop=%d  source=%s\n",
                        animation.name.c_str(), animation.frames.size(),
                        animation.fps, animation.loop ? 1 : 0,
                        animation.source.c_str());
        }
        for (const std::string& warning : atlas.warnings) {
            std::printf("[警告] %s：%s\n", atlas.filePath.c_str(), warning.c_str());
        }
    }
    for (const inkgen::TextConfig& text : loaded.textTables) {
        std::printf("[配置] %s  语言表：", text.name.c_str());
        for (std::size_t i = 0; i < text.table.languages.size(); ++i) {
            std::printf("%s%s", i == 0 ? "" : " / ",
                        text.table.languages[i].c_str());
        }
        // 缺省语言：json 的 default；没写就是表里第一门（与运行期行为一致）。
        const std::string defaultLanguage
            = !text.defaultLanguage.empty()
                  ? text.defaultLanguage
                  : (text.table.languages.empty() ? std::string("（没写）")
                                                  : text.table.languages.front());
        std::printf("（%zu 门），%zu 条占位符，缺省语言 %s\n",
                    text.table.languages.size(), text.table.entries.size(),
                    defaultLanguage.c_str());
        for (const std::string& warning : text.warnings) {
            std::printf("[警告] %s：%s\n", text.filePath.c_str(),
                        warning.c_str());
        }
    }

    for (const inkgen::ThemeConfig& theme : loaded.themes) {
        std::printf("[配置] %s  主题表：%zu 个颜色 token，%zu 个字号 token\n",
                    theme.name.c_str(), theme.colors.size(),
                    theme.fontSizes.size());
        std::printf("        颜色：");
        for (std::size_t i = 0; i < theme.colors.size(); ++i) {
            std::printf("%s$%s", i == 0 ? "" : " / ",
                        theme.colors[i].first.c_str());
        }
        std::printf("\n        字号：");
        for (std::size_t i = 0; i < theme.fontSizes.size(); ++i) {
            std::printf("%s$%s", i == 0 ? "" : " / ",
                        theme.fontSizes[i].first.c_str());
        }
        std::printf("\n");
        for (const std::string& warning : theme.warnings) {
            std::printf("[警告] %s：%s\n", theme.filePath.c_str(),
                        warning.c_str());
        }
    }

    for (const inkgen::GlobalConfig& config : loaded.configs) {
        std::printf("[配置] 全局：设计画布 %dx%d（%s）\n", config.width,
                    config.height, config.filePath.c_str());
        if (config.borderless.has_value()) {
            std::printf("        无边框=%s", *config.borderless ? "是" : "否");
        }
        if (config.roundedCorners.has_value()) {
            std::printf("  圆角=%s", *config.roundedCorners ? "请求" : "关闭");
        }
        if (config.transparent.has_value()) {
            std::printf("  透明窗口=%s", *config.transparent ? "是" : "否");
        }
        if (config.fullscreen.has_value()) {
            std::printf("  启动全屏=%s", *config.fullscreen ? "是" : "否");
        }
        if (config.background.has_value()) {
            std::printf("  清底色=0x%08X", *config.background);
        }
        if (config.startScene.has_value()) {
            std::printf("  起始场景=\"%s\"", config.startScene->c_str());
        }
        std::printf("\n");
        for (const std::string& warning : config.warnings) {
            std::printf("[警告] %s：%s\n", config.filePath.c_str(),
                        warning.c_str());
        }
    }

    for (const inkgen::CxxcssError& error : loaded.errors) {
        std::fprintf(stderr, "[错误] %s\n", error.Format().c_str());
    }

    if (!loaded.Ok()) {
        std::fprintf(stderr, "\n%d 个文件里共 %zu 条错误，没有生成任何文件。\n",
                     static_cast<int>(inputs.size()), loaded.errors.size());
        return 1;
    }

    // 图片与帧动画分开报数：一分配了 `frames` / `sheet` 的 Image 配置生成出来是
    // `InkingSprite`，那正是"到底有没有认到我的帧"最容易搞混的地方（0.6.1 T-06）。
    std::size_t spriteCount = 0;
    for (const inkgen::ImageConfig& image : loaded.images) {
        if (image.isSprite) {
            ++spriteCount;
        }
    }

    if (validateOnly) {
        std::printf("[完成] 校验通过：%zu 个按钮 + %zu 个面板 + %zu 个标签 + "
                    "%zu 张图片（其中 %zu 张帧动画）+ %zu 张语言表 + "
                    "%zu 份主题表 + %zu 份全局配置，未写文件\n",
                    loaded.buttons.size(), loaded.panels.size(),
                    loaded.labels.size(), loaded.images.size(), spriteCount,
                    loaded.textTables.size(), loaded.themes.size(),
                    loaded.configs.size());
        return 0;
    }

    const inkgen::EmitResult emitted = inkgen::Emit(loaded, options);
    if (!emitted.ok) {
        std::fprintf(stderr, "[错误] 生成失败：%s\n", emitted.error.c_str());
        return 1;
    }

    std::printf("[完成] 生成 %zu 个按钮 + %zu 个面板 + %zu 个标签 + %zu 张图片"
                "（其中 %zu 张帧动画）+ %zu 张语言表"
                "（主题表 %zu 份，$token 已在生成期替换；全局配置 %zu 份）：\n"
                "  %s\n  %s\n",
                loaded.buttons.size(), loaded.panels.size(),
                loaded.labels.size(), loaded.images.size(), spriteCount,
                loaded.textTables.size(),
                loaded.themes.size(), loaded.configs.size(),
                emitted.headerPath.c_str(), emitted.sourcePath.c_str());
    return 0;
}
