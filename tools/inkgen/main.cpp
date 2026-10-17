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
#include "Json.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

void PrintUsage() {
    std::printf(
        "inkgen —— 把 CXXCSS/<组件类型>/*.json 生成成 C++ 代码\n"
        "\n"
        "用法：\n"
        "  inkgen --out-dir <目录> [--namespace <ns>] [--header <名>] <file.json>...\n"
        "  inkgen --validate <file.json>...\n"
        "\n"
        "选项：\n"
        "  --out-dir <目录>   产物目录（约定指向 build/，不存在会创建）\n"
        "  --namespace <ns>   生成代码的命名空间，默认 inking::cxxcss\n"
        "  --header <名>      生成的头文件名，默认 button_service.h\n"
        "  --validate         只校验，不写文件\n"
        "  -h, --help         显示这段帮助\n"
        "\n"
        "组件类型由**目录名**决定（CXXCSS.md §1）：Button/buttons → 按钮，\n"
        "Panel/panels → 面板，Label/labels → 静态标签，Text/texts → 语言表\n"
        "（它不是组件，是「占位符 → 各语言的串」那张全局表，只允许一份），\n"
        "Theme/themes → 主题表（语义色 / 语义字号，是**生成器的输入**，\n"
        "配置里的 $token 在生成期就换成具体值——换主题要重新生成）；\n"
        "别的目录名会报错。\n"
        "退出码：0 全部成功；1 有错误（错误逐条打印）。\n"
        "注意：*.example.json 按约定不参与生成。\n");
}

}  // namespace

int main(int argc, char** argv) {
    inkgen::EmitOptions options;
    bool validateOnly = false;
    bool sawOutDir = false;
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
        if (arg == "--out-dir" || arg == "--namespace" || arg == "--header") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "[错误] %s 后面缺少值\n", arg.c_str());
                return 1;
            }
            const std::string value = argv[++i];
            if (arg == "--out-dir") {
                options.outDir = value;
                sawOutDir = true;
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

    for (const inkgen::CxxcssError& error : loaded.errors) {
        std::fprintf(stderr, "[错误] %s\n", error.Format().c_str());
    }

    if (!loaded.Ok()) {
        std::fprintf(stderr, "\n%d 个文件里共 %zu 条错误，没有生成任何文件。\n",
                     static_cast<int>(inputs.size()), loaded.errors.size());
        return 1;
    }

    if (validateOnly) {
        std::printf("[完成] 校验通过：%zu 个按钮 + %zu 个面板 + %zu 个标签 + "
                    "%zu 张语言表 + %zu 份主题表，未写文件\n",
                    loaded.buttons.size(), loaded.panels.size(),
                    loaded.labels.size(), loaded.textTables.size(),
                    loaded.themes.size());
        return 0;
    }

    const inkgen::EmitResult emitted = inkgen::Emit(loaded, options);
    if (!emitted.ok) {
        std::fprintf(stderr, "[错误] 生成失败：%s\n", emitted.error.c_str());
        return 1;
    }

    std::printf("[完成] 生成 %zu 个按钮 + %zu 个面板 + %zu 个标签 + %zu 张语言表"
                "（主题表 %zu 份，$token 已在生成期替换）：\n  %s\n  %s\n",
                loaded.buttons.size(), loaded.panels.size(),
                loaded.labels.size(), loaded.textTables.size(),
                loaded.themes.size(), emitted.headerPath.c_str(),
                emitted.sourcePath.c_str());
    return 0;
}
