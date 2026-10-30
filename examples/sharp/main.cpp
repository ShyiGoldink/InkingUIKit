// ---------------------------------------------------------------------------
// 清晰度验收示例（0.7.0 T-05）：720p 设计尺寸 + 一张真实素材 + 全屏逐档看
//
// 这个示例**不是演示某个功能**，而是把任务书 §3 的预测表变成"能看的实物"：
// 一张 1536×2752 的素材、设计尺寸 1280×720、同一个组件三种 fit，全屏前后来回切，
// 由**人**（Shyi）对照预测表判断"是否符合预测"。
//
// 为什么必须人工：清晰度是主观量。"像素有变化"能自动证明，"可接受"不能。
// 但**结论不能也主观**——所以每次显示状态变化都打印一行诊断（设计 / 素材 /
// 绘制 / 屏幕 / R / σ），验收就变成"读数与预测表是否一致"。
//
// 用法：
//   inking_sharp.exe [素材 png]
//   不给参数就读 exe 旁的 assets/nailong-art-01.png（CMake 会拷过去）。
//
// 键位（都走窗口层，示例不自建事件循环）：
//   1 / 2 / 3   切 fit = Fill / Contain / Cover（**默认 Cover**：全屏铺满、无黑边）
//   F11         全屏（窗口层内置）
//   ESC         退全屏；非全屏时退出（窗口层内置的"先退一层"语义）
//
// ---------------------------------------------------------------------------
// 判读规则（同样是本示例的 stdout 开头那几行）
//
//   σ = 素材像素 ÷ 它实际占的屏幕像素
//     σ = 1        1:1，最清
//     整数且 > 1   干净的 1/k 缩小（信息只剩 1/k²，但没脏像素）
//     非整数       有整列纹素被跳过 ⇒ 运动时会游走 / 闪
//     < 1          放大：线性下软，最近邻下成块
//
// §3.2 的预测（设计 1280×720 / 素材 1536×2752）：
//
// | 显示            | R   | Cover σ | Contain σ | 预期                       |
// | --------------- | --- | ------- | --------- | -------------------------- |
// | 窗口 1280×720   | 1.0 | 1.20    | 3.82      | Cover 接近 1:1 最清晰      |
// | 全屏 1920×1080  | 1.5 | 0.80    | 2.55      | Cover 略软                 |
// | 全屏 2560×1440  | 2.0 | 0.60    | 1.91      | Cover 软                   |
// | 全屏 3840×2160  | 3.0 | 0.40    | 1.27      | Cover 明显软、Contain 最清 |
//
// 一句话：**Cover 越全屏越软、Contain 越全屏越清**。这与 §3.2 一致，也顺带
// 旁证 T-01（栅格化发生在设备分辨率）。若两者都随屏幕变大而变软，说明栅格化
// 发生在逻辑分辨率——那 §3.2 要重写（见任务书 §5 的判读规则）。
//
// 两条**不是缺陷**的观感，先写在这里免得被当成 bug：
//   1. 素材是全身竖图（1536×2752），`Cover` 会裁掉上下大半；
//   2. `Contain` 在设计尺寸不是素材比例时会留出大片底色（这是 contain 的定义）。
// ---------------------------------------------------------------------------

#include <inking.h>

#include <config/window_config.h>
#include <dataStruct/InkingImageScale.h>
#include <dataStruct/InkingShapeSpec.h>
#include <image/ImageData.h>
#include <image/InkingStaticImage.h>
#include <input/Shortcut.h>
#include <input/ShortcutRegistry.h>
#include <scene/InkingScene.h>
#include <window/InkingWindow.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

/// 默认素材（CMake 把它拷到 exe 旁的 assets/）。
const char* kAssetName = "nailong-art-01.png";

/// `std::filesystem::path` → UTF-8 窄串（不能直接用 `path.string()`：MinGW 下它
/// 按 "C" locale 编码，中文路径会抛 `Illegal byte sequence`）。
std::string Utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

/// 读整份文件字节。失败返回空（调用方报一条人话就退出）。
std::vector<std::uint8_t> ReadAllBytes(const std::filesystem::path& path) {
    std::FILE* file = std::fopen(Utf8(path).c_str(), "rb");
    if (file == nullptr) {
        return {};
    }
    std::vector<std::uint8_t> bytes;
    std::uint8_t chunk[64 * 1024];
    std::size_t got = 0;
    while ((got = std::fread(chunk, 1, sizeof(chunk), file)) > 0) {
        bytes.insert(bytes.end(), chunk, chunk + got);
    }
    std::fclose(file);
    return bytes;
}

/// 从 PNG 的 IHDR 里读宽高——只是两个整数，不必为此拉起图片子系统
/// （渲染器还没建，`IMG_Load` 那条路要 `SDL_Init(IMG)`）。
bool ReadPngSize(const std::vector<std::uint8_t>& bytes, int& width,
                 int& height) {
    static const std::uint8_t kSignature[8] = {0x89, 'P', 'N', 'G',
                                               0x0D, 0x0A, 0x1A, 0x0A};
    if (bytes.size() < 33 || std::memcmp(bytes.data(), kSignature, 8) != 0) {
        return false;
    }
    width = (bytes[16] << 24) | (bytes[17] << 16) | (bytes[18] << 8) | bytes[19];
    height = (bytes[20] << 24) | (bytes[21] << 16) | (bytes[22] << 8) | bytes[23];
    return width > 0 && height > 0;
}

/// 素材文件名（日志 / 纹理缓存键用）。
std::string FileNameOf(const std::filesystem::path& path) {
    return Utf8(path.filename());
}

/// 找一个能读的素材：命令行优先，否则 exe 旁的 `assets/`。
std::filesystem::path ResolveAsset(int argc, char** argv) {
    if (argc > 1 && argv[1] != nullptr && argv[1][0] != '\0') {
        return std::filesystem::path(std::u8string(
            reinterpret_cast<const char8_t*>(argv[1])));
    }
    if (const char* base = SDL_GetBasePath(); base != nullptr) {
        return std::filesystem::path(std::u8string(
                   reinterpret_cast<const char8_t*>(base)))
            / "assets" / kAssetName;
    }
    return std::filesystem::path(kAssetName);
}

/// 把 σ 翻成一句人话（判读规则的落地版）。
const char* DescribeSigma(float sigma) {
    if (sigma <= 0.0f) {
        return "σ 未定义（素材 / 尺寸读不到）";
    }
    if (std::fabs(sigma - 1.0f) < 0.02f) {
        return "1:1，最清";
    }
    if (sigma < 1.0f) {
        return "放大，会软";
    }
    if (inking::ImageScaleSpec::IsCleanRatio(sigma)) {
        return "整数倍缩小，干净（信息只剩 1/k²）";
    }
    return "非整数，有纹素被跳过（运动时会游走 / 闪）";
}

const char* FitName(inking::ImageFit fit) {
    switch (fit) {
        case inking::ImageFit::Fill:
            return "Fill";
        case inking::ImageFit::Contain:
            return "Contain";
        case inking::ImageFit::Cover:
            return "Cover";
    }
    return "?";
}

/**
 * 验收场景：一块铺满设计画布的图片 + 三档 fit + 一行诊断。
 *
 * 为什么 fit 切换要**延后到 onTick**：快捷键回调在窗口的事件派发里跑，而
 * `InkingScene` 明令"回调里不许增删节点"（`AddChild` / `RemoveChild` 会动
 * 命中表与绘制列表）。切 fit 的实现是**重建那个节点**（`ImageData` 没有运行期
 * 改 fit 的入口），所以回调里只记一个意图，真正的重建放在这一帧的 tick 里。
 */
class SharpScene : public inking::InkingScene {
public:
    SharpScene(std::vector<std::uint8_t> png, int assetWidth, int assetHeight,
               std::string assetName)
        : inking::InkingScene("sharp"),
          _png(std::move(png)),
          _assetWidth(assetWidth),
          _assetHeight(assetHeight),
          _assetName(std::move(assetName)) {
        Rebuild();
    }

    /// 只记意图（可以在回调里调）；真正的重建与打印发生在下一帧的 `onTick`。
    void RequestFit(inking::ImageFit fit) noexcept {
        _pendingFit = fit;
        _hasPending = true;
    }

protected:
    void onTick(double /*fixedStepSeconds*/) override {
        if (_hasPending && _pendingFit != _fit) {
            _fit = _pendingFit;
            Rebuild();
            Report(true);
        }
        _hasPending = false;
        // 全屏 / 拖窗口都不发通知，只能每帧比一次（代价是两个整数比较）。
        Report(false);
    }

private:
    /// 按当前 fit 重建图片节点（`ImageData` 没有运行期改 fit 的入口）。
    void Rebuild() {
        if (_image != nullptr) {
            RemoveChild(_image);  // 拥有型：返回的 unique_ptr 直接丢掉
            _image = nullptr;
        }
        inking::ImageData data;
        data.name = "sharp_probe";
        data.width = inking::DesignWidth();
        data.height = inking::DesignHeight();
        data.normal.type = inking::AppearanceType::Image;
        data.normal.imageFit = _fit;
        data.normal.source = _assetName;
        data.selfAnchor = inking::InkingChangeAnchor::LeftTop;
        data.traceAnchor = inking::InkingChangeAnchor::LeftTop;
        data.offsetX = 0.0f;
        data.offsetY = 0.0f;
        // 铺满整块画布、不参与命中：它只是"被看的那张图"。
        data.hitPassThrough = true;
        auto node = std::make_unique<inking::InkingStaticImage>(this, data);
        // 字节直接给（不走 `runtimeAsset`：那条路要求 source 是相对资源根的路径，
        // 会把示例绑死在资产目录约定上；这里字节已经在手上）。
        node->SetImageBytes(_png.data(), _png.size(), _assetName);
        _image = static_cast<inking::InkingStaticImage*>(AddChild(std::move(node)));
    }

    /**
     * 打一行诊断：设计 / 素材 / 绘制（设计 px）/ 屏幕像素 / R / σ。
     *
     * @param force 切 fit 时强制打；否则只有"显示状态变了"才打（不然每帧刷屏）。
     *
     * 三个量都是**算术**，没有一个是"看着像"：
     *   - `R` = min(窗口宽 / 设计宽, 窗口高 / 设计高)（letterbox 倍率）；
     *   - 绘制矩形来自 `ImageFitSpec::DrawnRect()`（与绘制层同一个纯函数）；
     *   - σ 来自 `ImageScaleSpec::SamplingRatio()`（与任务书 §3 同一个纯函数）。
     */
    void Report(bool force) {
        const inking::InkingWindow& window = inking::InkingWindow::Instance();
        const int designWidth = inking::DesignWidth();
        const int designHeight = inking::DesignHeight();
        const int windowWidth = window.GetWindowWidth();
        const int windowHeight = window.GetWindowHeight();
        if (windowWidth <= 0 || windowHeight <= 0 || designWidth <= 0
            || designHeight <= 0) {
            return;
        }
        const float ratio = std::min(
            static_cast<float>(windowWidth) / static_cast<float>(designWidth),
            static_cast<float>(windowHeight) / static_cast<float>(designHeight));

        const bool changed = force || _fit != _lastFit
            || windowWidth != _lastWindowWidth
            || windowHeight != _lastWindowHeight;
        if (!changed) {
            return;
        }
        _lastFit = _fit;
        _lastWindowWidth = windowWidth;
        _lastWindowHeight = windowHeight;

        // 绘制矩形：与绘制层**同一个** `ImageFitSpec`（不是另写一份等比算术）。
        inking::ImageFitSpec fitSpec;
        fitSpec.fit = _fit;
        fitSpec.frameWidth = static_cast<float>(designWidth);
        fitSpec.frameHeight = static_cast<float>(designHeight);
        fitSpec.imageWidth = static_cast<float>(_assetWidth);
        fitSpec.imageHeight = static_cast<float>(_assetHeight);
        const inking::ShapeBounds drawn = fitSpec.DrawnRect();

        // σ：素材像素 ÷ 它实际占的屏幕像素（= 绘制宽 × R）。
        inking::ImageScaleSpec scale;
        scale.designWidth = static_cast<int>(std::lround(drawn.width));
        scale.depth = 1.0f;
        scale.ratioMax = ratio;
        const float sigma = scale.SamplingRatio(_assetWidth, ratio);

        std::printf(
            "\n[sharp] 窗口 %d×%d / 设计 %d×%d / 素材 %s %d×%d / fit=%s\n"
            "        绘制 %.1f×%.1f（设计 px）⇒ 屏幕 %.1f×%.1f px，R=%.3f，"
            "σ=%.3f\n"
            "        σ 判读：%s\n",
            windowWidth, windowHeight, designWidth, designHeight,
            _assetName.c_str(), _assetWidth, _assetHeight, FitName(_fit),
            static_cast<double>(drawn.width), static_cast<double>(drawn.height),
            static_cast<double>(drawn.width * ratio),
            static_cast<double>(drawn.height * ratio),
            static_cast<double>(ratio), static_cast<double>(sigma),
            DescribeSigma(sigma));
    }

    std::vector<std::uint8_t> _png;  ///< 素材字节（自己持有一份，只拷进节点一次）
    int _assetWidth = 0;
    int _assetHeight = 0;
    std::string _assetName;
    inking::InkingStaticImage* _image = nullptr;

    inking::ImageFit _fit = inking::ImageFit::Cover;  ///< 默认 Cover：真实用法
    inking::ImageFit _pendingFit = inking::ImageFit::Cover;
    bool _hasPending = false;

    // 上一次打印时的显示状态（只为"变了才打"）。
    inking::ImageFit _lastFit = inking::ImageFit::Cover;
    int _lastWindowWidth = -1;
    int _lastWindowHeight = -1;
};

}  // namespace

int main(int argc, char** argv) {
    // ---------------------------------------------------------------------
    // 设计尺寸**必须是第一条**：第一个窗口 / 场景一构造就定型，之后
    // `SetDesignSize` 会被拒绝（那时画面已经按旧尺寸算过一轮几何了）。
    // ---------------------------------------------------------------------
    if (!inking::SetDesignSize(1280, 720)) {
        std::printf("[sharp] 设定设计尺寸 1280×720 被拒绝（已经定型？）\n");
        return 1;
    }

    std::printf("[sharp] Inking %s-%s / 链接到的 SDL3 %s\n", inking::kVersion,
                inking::kVersionStage, inking::sdl3_version().c_str());
    std::printf("[sharp] 设计尺寸 1280×720；键位：1=Fill 2=Contain 3=Cover"
                " / F11=全屏 / ESC=退全屏或退出\n");
    std::printf("[sharp] 判读规则：σ=1 最清；整数>1 干净缩小；非整数 脏；<1 放大发软\n");
    std::printf("[sharp] 预测：Cover 越全屏越软、Contain 越全屏越清"
                "（两者都变软 = 栅格化发生在逻辑分辨率，§3.2 要重写）\n");

    // `SDL_GetBasePath` 需要文件系统起来（这一步不建窗口）。
    SDL_Init(0);
    const std::filesystem::path assetPath = ResolveAsset(argc, argv);
    const std::vector<std::uint8_t> bytes = ReadAllBytes(assetPath);
    if (bytes.empty()) {
        std::printf("[sharp] 读不到素材：%s\n"
                    "        把 PNG 路径作为第一个参数传进来，或者让 exe 旁的"
                    " assets/ 里放着它。\n",
                    Utf8(assetPath).c_str());
        SDL_Quit();
        return 2;
    }
    int assetWidth = 0;
    int assetHeight = 0;
    if (!ReadPngSize(bytes, assetWidth, assetHeight)) {
        std::printf("[sharp] %s 不是可解析的 PNG（读不出宽高）\n",
                    Utf8(assetPath).c_str());
        SDL_Quit();
        return 2;
    }
    std::printf("[sharp] 素材：%s（%d×%d，%.2f MB）\n",
                Utf8(assetPath).c_str(), assetWidth, assetHeight,
                static_cast<double>(bytes.size()) / (1024.0 * 1024.0));

    SharpScene scene(bytes, assetWidth, assetHeight, FileNameOf(assetPath));
    if (!scene.IsRegistered()) {
        std::printf("[sharp] 场景登记被驳回（名字 \"" "sharp" "\" 被占）\n");
        SDL_Quit();
        return 1;
    }

    // 1/2/3 走窗口层的**全局快捷键表**（焦点无关）。回调里只改意图，重建在 tick 里。
    inking::InkingWindow& window = inking::InkingWindow::Instance();
    window.GetShortcuts().Bind(inking::Shortcut::Parse("1"),
                               [&scene] { scene.RequestFit(inking::ImageFit::Fill); },
                               "sharp:fit=fill");
    window.GetShortcuts().Bind(
        inking::Shortcut::Parse("2"),
        [&scene] { scene.RequestFit(inking::ImageFit::Contain); }, "sharp:fit=contain");
    window.GetShortcuts().Bind(
        inking::Shortcut::Parse("3"),
        [&scene] { scene.RequestFit(inking::ImageFit::Cover); }, "sharp:fit=cover");

    // 按设计尺寸开窗；`Show()` 是同步的（建窗 + 主循环，直到关窗 / ESC）。
    window.setWindowWidth(1280);
    window.setWindowHeight(720);
    window.Show("sharp");

    std::printf("\n[sharp] 主循环结束。人工结论请按预测表记一条：符合 / 不符合 / 不可接受。\n");
    SDL_Quit();
    return 0;
}
