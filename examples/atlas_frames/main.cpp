// ---------------------------------------------------------------------------
// 图集帧动画测试（sunflower_idle）
//
// 验的是**图集模式**这一条路（`InkingSpriteData::sheet`，0.6.1 T-05 起）：
// 一张 3072×2560 的大图按 6 列 × 5 行切成 30 帧，换帧只改源矩形——
// 纹理**一份**、解码**一次**，与"30 张散图"（`frames`）是两条不同的实现。
//
// 素材：`atlas.png`（6×5，单帧 512×512，15fps，2 秒一圈）。
// 窗口：512×512、无边框、透明、置顶——桌面上只有向日葵本身。
//
// 用法：
//   inking_atlas_frames.exe [图集路径] [fps]
//   不给参数就用 exe 同级的 `atlas.png`（打包后的 zip 就是这个布局）。
//
// 操作：
//   Space        暂停 / 继续（暂停后 ←/→ 逐帧看，图集帧序对不对一眼就知道）
//   ← / →        上一帧 / 下一帧（自动暂停）
//   R            回到第 0 帧重新播
//   左键拖动      移动窗口（透明区也吃点击，是这一版的能力边界，见下）
//   ESC          退出（窗口层内置：先退全屏、再退程序）
//
// 排错开关（正常跑不看）：
//   INK_ATLAS_DEBUG=1     每 15 帧打一行帧号 / 推进次数 / 纹理数
//   INK_ATLAS_SHOT=<png>  第 2 帧离屏渲一份 PNG
//                         屏幕抓图对 GPU 合成窗口不可靠，只有这条可信
//   INK_ATLAS_OPAQUE=1    关掉透明，用来分辨"没画"还是"没合成"
// ---------------------------------------------------------------------------

#include <config/window_config.h>
#include <image/InkingSprite.h>
#include <input/Shortcut.h>
#include <render/InkingText.h>  // 诊断用：纹理计数
#include <scene/InkingScene.h>
#include <window/InkingWindow.h>

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>

namespace {

// ---- 图集参数（与 manifest.json 一致）----
//
// 单帧 512×512、6 列 × 5 行、30 帧、15fps。**帧尺寸与格数是两件事**：
// `frameWidth × columns` 必须等于图宽，否则 `InkingSprite` 构造期会报 ERROR
// （见 `validateSheetGeometry()`）——这里在起窗之前先自己量一遍，报得更早。
constexpr int kFrameWidth = 512;
constexpr int kFrameHeight = 512;
constexpr int kColumns = 6;
constexpr int kRows = 5;
constexpr int kFrameCount = kColumns * kRows;  // 30
constexpr double kDefaultFps = 15.0;

/// `std::filesystem::path` → UTF-8 窄串。
///
/// 不用 `path.string()`：MinGW 下它按 "C" locale 编码，中文路径会抛
/// `Illegal byte sequence`（`AGENTS.md` §6）。资源层收的正是 UTF-8。
std::string Utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

/// 从 PNG 的 IHDR 读宽高（前 33 字节够）。
///
/// 不借 SDL：这一步在主循环之前、渲染器还没建，读 8 个字节比拉起图片子系统便宜。
bool ReadPngSize(const std::string& path, int& width, int& height) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return false;
    }
    unsigned char header[33] = {};
    const std::size_t got = std::fread(header, 1, sizeof(header), file);
    std::fclose(file);

    // 8 字节签名 + 4 字节长度 + "IHDR" + width(4) + height(4)
    static const unsigned char kSignature[8] = {0x89, 'P', 'N', 'G',
                                                0x0D, 0x0A, 0x1A, 0x0A};
    if (got < sizeof(header) || std::memcmp(header, kSignature, 8) != 0) {
        return false;
    }
    width = (header[16] << 24) | (header[17] << 16) | (header[18] << 8)
            | header[19];
    height = (header[20] << 24) | (header[21] << 16) | (header[22] << 8)
             | header[23];
    return width > 0 && height > 0;
}

/// exe 所在目录（UTF-8，带尾部斜杠）。取不到就退回当前目录。
std::string ExecutableDir() {
    const char* base = SDL_GetBasePath();
    if (base == nullptr) {
        return "./";
    }
    std::string dir(base);
    SDL_free(const_cast<char*>(base));
    return dir;
}

/// 默认图集路径：先找 exe 同级的 `atlas.png`，再找 `assets/atlas.png`。
///
/// 两条都试是为了让"解压后随手摆"也能跑：打包的 zip 用第一种，
/// 有人把素材放 assets/ 也不至于直接报找不到文件。
std::string DefaultAtlasPath() {
    const std::string dir = ExecutableDir();
    const std::string beside = dir + "atlas.png";
    std::FILE* probe = std::fopen(beside.c_str(), "rb");
    if (probe != nullptr) {
        std::fclose(probe);
        return beside;
    }
    return dir + "assets/atlas.png";
}

/// 图集 → `InkingSpriteData`（`sheet` 分支；与 `frames` 互斥）。
InkingSpriteData MakeAtlasData(const std::string& atlasPath, double fps) {
    InkingSpriteData data;
    data.name = "sunflower_idle";

    // 显示尺寸 = 单帧尺寸：1:1 出像素，缩放交给窗口层。
    data.width = kFrameWidth;
    data.height = kFrameHeight;
    data.selfAnchor = InkingChangeAnchor::LeftTop;
    data.traceAnchor = InkingChangeAnchor::LeftTop;
    data.offsetX = 0.0f;
    data.offsetY = 0.0f;

    // 字节在运行期从 exe 旁读（换美术不用重编）。图集模式下这份字节就是
    // **整张大图**：所有帧共用一张纹理，换帧只改源矩形。
    data.runtimeAsset = true;
    data.normal.type = InkingAppearanceType::Image;
    data.normal.source = atlasPath;

    data.sheet.frameWidth = kFrameWidth;
    data.sheet.frameHeight = kFrameHeight;
    data.sheet.columns = kColumns;
    data.sheet.rows = kRows;
    data.sheet.count = kFrameCount;  // 30 格全用，不留空格

    data.fps = fps;
    data.loop = true;
    data.autoplay = true;
    return data;
}

/// 一个只有向日葵的场景。场景尺寸 = 设计尺寸 = 单帧尺寸，所以贴图 1:1。
class AtlasScene : public InkingScene {
public:
    explicit AtlasScene(const InkingSpriteData& data)
        : InkingScene("atlas_frames") {
        _sprite = static_cast<InkingSprite*>(
            AddChild(std::make_unique<InkingSprite>(this, data)));
    }

    InkingSprite* GetSprite() const noexcept { return _sprite; }

private:
    InkingSprite* _sprite = nullptr;
};

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    const std::string atlasPath = argc > 1 ? argv[1] : DefaultAtlasPath();
    const double fps = argc > 2 ? std::atof(argv[2]) : kDefaultFps;

    // ---- 1. 素材：先自己量一遍图，把"格数与图宽不符"挡在起窗之前 ----
    int atlasWidth = 0;
    int atlasHeight = 0;
    if (!ReadPngSize(atlasPath, atlasWidth, atlasHeight)) {
        std::printf("读不出图集：%s\n", atlasPath.c_str());
        std::printf("用法：inking_atlas_frames.exe [图集路径] [fps]\n");
        std::printf("（把 atlas.png 放在 exe 旁边，或把路径作为第一个参数传进来）\n");
        return 1;
    }
    std::printf("图集：%s\n", atlasPath.c_str());
    std::printf("      %d×%d，按 %d 列 × %d 行切，单帧 %d×%d，%d 帧 %.1ffps（一圈 %.2fs）\n",
                atlasWidth, atlasHeight, kColumns, kRows, kFrameWidth,
                kFrameHeight, kFrameCount, fps,
                static_cast<double>(kFrameCount) / fps);
    if (atlasWidth != kFrameWidth * kColumns
        || atlasHeight != kFrameHeight * kRows) {
        std::printf("警告：图集尺寸与 %d×%d 的切法不符（应 %d×%d）——"
                    "画面会错位，先核对 columns / rows / 单帧尺寸\n",
                    kColumns, kRows, kFrameWidth * kColumns,
                    kFrameHeight * kRows);
    }

    // ---- 2. 设计尺寸 = 单帧尺寸 ----
    //
    // **必须是第一件事**：设计尺寸在窗口 / 场景构造那一刻就烘进根锚点与
    // letterbox 逻辑尺寸里，晚了 `InkingSetDesignSize` 会拒绝（`window_config.h`）。
    if (!InkingSetDesignSize(kFrameWidth, kFrameHeight)) {
        std::printf("设计尺寸被拒（%d×%d）：窗口或场景已经定型？\n", kFrameWidth,
                    kFrameHeight);
        return 1;
    }

    // ---- 3. 场景 + 窗口 ----
    AtlasScene scene(MakeAtlasData(atlasPath, fps));

    InkingWindow& window = InkingWindow::Instance();
    window.SetBorderless(true);   // 必须在 Open() 之前：建窗时才带标志
    const bool opaque = SDL_getenv("INK_ATLAS_OPAQUE") != nullptr;
    if (!opaque) {
        window.SetTransparent(true);
    }
    window.SetAlwaysOnTop(true);
    window.SetBackgroundColor(0x00000000u);  // 清屏全透明

    // 透明窗口**只在 Windows 的 D3D11 后端成立**：SDL 只给那条路补了透明
    // swapchain（direct3d12 / gpu 会直接拒绝透明窗口，opengl 不保证）。
    //
    // **不写死驱动名**，交给 SDL 自己挑——它会为透明窗口跳过不支持透明的后端。
    // 教训（2026-11-01 实际踩到）：手写 `SDL_SetHint(..., "d3d11")` 用的是 SDL2
    // 时代的名字，SDL3 的驱动名是 **"direct3d11"**；名字对不上时
    // `SDL_CreateRenderer` 返回空、错误串是 "d3d11 not available"，而**窗口照样
    // 建出来、透明标志照样落地**，只是什么都画不上——表现为"窗口在、里面全透明"。
    // 要指定后端就用 `INK_ATLAS_BACKEND=direct3d11`（名字错了会退回自选）。
    if (const char* backend = SDL_getenv("INK_ATLAS_BACKEND")) {
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, backend);
    }
    // `Open()` 的参数是**场景名**（不是窗口标题）：`stepFrame` 靠
    // `InkingSceneLibrary::GetActiveScene()` 找场景喂帧，名字对不上就是"窗口开起来了、
    // 场景一帧都没收到"——画面停在起始帧，且不报任何错。场景构造时登记进
    // `InkingSceneLibrary`，`Open("atlas_frames")` 把它激活。
    if (!window.Open("atlas_frames")) {
        std::printf("建窗失败（见日志）\n");
        return 1;
    }
    // 渲染器没建起来时 `Open()` **仍返回 true**（窗口是真的开了），只是什么都不画。
    // 所以必须另外探一次：`RequestCapture` 在没有渲染器时明确返回 false。
    if (!window.RequestCapture((std::filesystem::temp_directory_path() / "inking_atlas_probe.png").string())) {
        std::printf("没有渲染器：窗口开着但什么都不会画。见 LOG/ 下的日志，"
                    "试 INK_ATLAS_BACKEND=direct3d11\n");
        return 1;
    }
    if (!opaque) {
        std::printf(window.IsTransparentApplied()
                        ? "透明窗口：SDL_WINDOW_TRANSPARENT 已进窗口标志\n"
                        : "透明窗口：标志**没**落地，背景会是一整块黑\n");
    }

    InkingSprite* sprite = scene.GetSprite();
    if (sprite != nullptr && !sprite->IsValid()) {
        std::printf("帧动画配置有问题：%s\n", sprite->GetConfigError().c_str());
    } else if (sprite != nullptr) {
        std::printf("帧数：%zu（可播），字节备齐 %zu 帧\n",
                    sprite->GetFrameCount(), sprite->GetLoadedFrameCount());
    }

    // ---- 4. 播控：住在窗口层的全局快捷键（焦点在谁身上都响应）----
    InkingShortcutRegistry& shortcuts = window.GetShortcuts();
    shortcuts.Bind(InkingShortcut::Parse("Space"), [sprite]() {
        if (sprite == nullptr) {
            return;
        }
        if (sprite->IsPlaying()) {
            sprite->Pause();
            std::printf("暂停 @ 第 %zu 帧\n", sprite->GetFrameIndex());
        } else {
            sprite->Play();
            std::printf("继续播（从第 %zu 帧）\n", sprite->GetFrameIndex());
        }
    }, "暂停/继续");

    // 逐帧步进：先暂停再定位。播放中定位会被下一帧立刻推走，看不到静帧。
    shortcuts.Bind(InkingShortcut::Parse("Left"), [sprite]() {
        if (sprite == nullptr) {
            return;
        }
        sprite->Pause();
        const std::size_t count = sprite->GetFrameCount();
        const std::size_t current = sprite->GetFrameIndex();
        const std::size_t next = current == 0 ? count - 1 : current - 1;
        sprite->SeekToFrame(next);
        std::printf("第 %zu 帧\n", sprite->GetFrameIndex());
    }, "上一帧");

    shortcuts.Bind(InkingShortcut::Parse("Right"), [sprite]() {
        if (sprite == nullptr) {
            return;
        }
        sprite->Pause();
        const std::size_t count = sprite->GetFrameCount();
        const std::size_t next = (sprite->GetFrameIndex() + 1) % count;
        sprite->SeekToFrame(next);
        std::printf("第 %zu 帧\n", sprite->GetFrameIndex());
    }, "下一帧");

    shortcuts.Bind(InkingShortcut::Parse("R"), [sprite]() {
        if (sprite == nullptr) {
            return;
        }
        sprite->SeekToFrame(0);
        sprite->Play();
        std::printf("从头播\n");
    }, "重播");

    printf("操作：Space 暂停/继续 · ←/→ 逐帧 · R 重播 · 拖动移动窗口 · ESC 退出\n");

    // ---- 5. 主循环：跑帧 + 拖动窗口 ----
    //
    // 拖动用**全局鼠标坐标**：窗口跟着指针动时，窗口内的设计坐标几乎不变，
    // 拿它算增量会得到"走一步退一步"的抖动。
    float lastGlobalX = 0.0f;
    float lastGlobalY = 0.0f;
    bool dragging = false;
    long long frameIndex = 0;

    const bool debugTrace = SDL_getenv("INK_ATLAS_DEBUG") != nullptr;
    const char* shotPath = SDL_getenv("INK_ATLAS_SHOT");
    // 窗口后缓冲（`Present` 之前）—— 与 `INK_ATLAS_SHOT` 的离屏软件渲染不同，
    // 这条是**真 renderer 画进真后缓冲**的东西。区分"没画"和"画了但没合成"
    // 只能靠它：前者这条也会是空的。
    const char* capturePath = SDL_getenv("INK_ATLAS_CAPTURE");

    while (window.PumpOnce()) {
        ++frameIndex;
        if (capturePath != nullptr && frameIndex == 3) {
            const bool queued = window.RequestCapture(capturePath);
            std::printf("[debug] RequestCapture=%d → %s\n", queued ? 1 : 0,
                        capturePath);
        }
        if (debugTrace && (frameIndex % 15 == 1)) {
            const auto stats = inking_detail::TextCacheStatsSnapshot();
            std::printf("[debug] #%lld 帧号=%zu 推进=%llu 跳过=%llu 纹理=%llu "
                        "playing=%d 窗口=%dx%d\n",
                        frameIndex,
                        sprite != nullptr ? sprite->GetFrameIndex() : 0,
                        sprite != nullptr
                            ? static_cast<unsigned long long>(
                                  sprite->GetFrameAdvanceCount())
                            : 0ull,
                        sprite != nullptr
                            ? static_cast<unsigned long long>(
                                  sprite->GetSkippedFrameCount())
                            : 0ull,
                        static_cast<unsigned long long>(stats.texturesCreated),
                        sprite != nullptr && sprite->IsPlaying() ? 1 : 0,
                        window.GetWindowWidth(), window.GetWindowHeight());
        }
        if (shotPath != nullptr && frameIndex == 2) {
            const bool ok = InkingWindow::RenderToTexture(
                shotPath, window.GetWindowWidth(), window.GetWindowHeight(),
                "atlas_frames");
            std::printf("[debug] offscreen=%d → %s\n", ok ? 1 : 0, shotPath);
        }

        float globalX = 0.0f;
        float globalY = 0.0f;
        const SDL_MouseButtonFlags buttons =
            SDL_GetGlobalMouseState(&globalX, &globalY);
        const bool leftDown = (buttons & SDL_BUTTON_LMASK) != 0;

        if (leftDown && !dragging) {
            dragging = true;
            lastGlobalX = globalX;
            lastGlobalY = globalY;
        } else if (!leftDown) {
            dragging = false;
        } else {
            const int deltaX = static_cast<int>(std::lround(globalX - lastGlobalX));
            const int deltaY = static_cast<int>(std::lround(globalY - lastGlobalY));
            if (deltaX != 0 || deltaY != 0) {
                window.MoveWindowBy(deltaX, deltaY);
                lastGlobalX = globalX;
                lastGlobalY = globalY;
            }
        }
    }

    return 0;
}
