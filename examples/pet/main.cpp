// ---------------------------------------------------------------------------
// 桌宠 demo（0.6.0 T-03 之后的手工验证示例）
//
// 三件事同时验证，所以它不是"演示动画"，而是**把 0.6.0 的能力拧在一起**：
//   1. 序列帧动画（`InkingSprite`，T-03）：一套散图 PNG 按 fps 轮播；
//   2. 无边框 + 透明 + 置顶窗口（`SetBorderless` / `SetTransparent` /
//      `SetAlwaysOnTop`）：桌面上只有角色本身，没有标题栏、没有底板；
//   3. 拖动窗口（`MoveWindowBy` + `SDL_GetGlobalMouseState`）。
//
// 用法：
//   inking_pet.exe [帧目录] [fps]
//   inking_pet.exe "D:/AllMyDream/2026/DragonBonesAnimation/Pusshine/Pusshine_StayAlive" 24
//
// 不给参数就用下面的 kDefaultFrameDir；帧目录里所有 `*.png` 按文件名排序 = 播放顺序
// （DragonBones 导出的 `<Armature>_00.png` 这种零填充命名，字典序就是帧序）。
//
// 退出：ESC（窗口层内置"先退全屏、再退程序"）。
//
// ---------------------------------------------------------------------------
// 三条已知边界（不是 bug，是这一版的能力边界）
//
//   1. **透明区仍然吃点击**：窗口是矩形的，角色的"空白处"照样命中窗口。
//      逐像素穿透窗口（`WM_NCHITTEST` 返回 `HTTRANSPARENT`）没做，
//      所以点击角色旁边会落到这个窗口上，而不是桌面。
//   2. **半透明边缘会略亮**：DWM 按 premultiplied 解释窗口 alpha，而组件画的是
//      straight alpha（`InkingWindow.h` 的透明段有实测记录）。全不透明的地方不受影响。
//   3. **帧纹理首轮全建一次**：构造期只预热**字节**（18MB 这档要读一次盘），
//      纹理在第一次画到那一帧时解码。播完第一圈之后换帧零解码。
//      73 帧 321×900 的纹理 ≈ 84MB 显存——帧数再大就该上环形窗口了（T-03 的风险段）。
// ---------------------------------------------------------------------------

#include <config/window_config.h>
#include <image/InkingSprite.h>
#include <render/InkingText.h>  // 诊断用：纹理计数
#include <scene/InkingScene.h>
#include <window/InkingWindow.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

/// 没给参数时用的帧目录（本机素材；换机器请把路径作为第一个参数传进来）。
const char* kDefaultFrameDir =
    "D:/AllMyDream/2026/DragonBonesAnimation/Pusshine/Pusshine_StayAlive";

/// 默认帧率。DragonBones 导出的角色待机动画通常是 24fps。
constexpr double kDefaultFps = 24.0;

/// `std::filesystem::path` → UTF-8 窄串。
///
/// 不能直接用 `path.string()`：MinGW 下它按 "C" locale 编码，中文路径会抛
/// `Illegal byte sequence`（`AGENTS.md` §6 那条）。`AssetLoad` 那侧收的正是
/// UTF-8（`PathUtil.h` 负责转宽字符）。
std::string Utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

/// 从 PNG 的 IHDR 里读宽高（前 33 字节足够）。
///
/// 为什么不用 SDL：这里在主循环之前、渲染器还没建，`SDL_LoadPNG` 那条路要
/// 初始化图片子系统；而只是要两个整数，读 8 个字节比拉起一个子系统便宜。
bool ReadPngSize(const std::filesystem::path& path, int& width, int& height) {
    std::FILE* file = std::fopen(Utf8(path).c_str(), "rb");
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

/// 帧目录里所有 `*.png`，按文件名排序（= 帧序）。
std::vector<std::filesystem::path> CollectFrames(const std::string& dir) {
    std::vector<std::filesystem::path> frames;
    std::error_code code;
    for (const auto& entry : std::filesystem::directory_iterator(dir, code)) {
        if (code || !entry.is_regular_file(code)) {
            continue;
        }
        std::string extension = Utf8(entry.path().extension());
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension == ".png") {
            frames.push_back(entry.path());
        }
    }
    std::sort(frames.begin(), frames.end(),
              [](const std::filesystem::path& a, const std::filesystem::path& b) {
                  return a.filename().u8string() < b.filename().u8string();
              });
    return frames;
}

/// 把一个帧目录变成 `SpriteData`（`runtimeAsset = true`：字节由组件自己读进内存）。
inking::SpriteData MakePetData(const std::vector<std::filesystem::path>& frames,
                               int width, int height, double fps) {
    inking::SpriteData data;
    data.name = "pet";
    data.width = width;
    data.height = height;
    data.selfAnchor = inking::InkingChangeAnchor::LeftTop;
    data.traceAnchor = inking::InkingChangeAnchor::LeftTop;
    data.offsetX = 0.0f;
    data.offsetY = 0.0f;
    data.fps = fps;
    data.loop = true;
    data.autoplay = true;
    data.runtimeAsset = true;  // 字节从文件读（不进 exe、也不进二进制资源）

    for (const std::filesystem::path& path : frames) {
        inking::ButtonAppearance frame(inking::AppearanceType::Image, 0xFF000000u);
        // `source` 有两重身份：AssetLoad 的路径 + 纹理缓存键。每帧不同是关键
        // ——同一个键会让所有帧命中同一张纹理（0.6.0 T-03 修过那个 bug）。
        frame.source = Utf8(path);
        data.frames.push_back(frame);
    }
    return data;
}

/// 一个只有宠物的场景。场景尺寸 = 设计尺寸 = 帧尺寸，所以贴图 1:1、不缩放。
class PetScene : public inking::InkingScene {
public:
    explicit PetScene(const inking::SpriteData& data)
        : inking::InkingScene("pet") {
        _sprite = static_cast<inking::InkingSprite*>(AddChild(
            std::make_unique<inking::InkingSprite>(this, data)));
    }

    inking::InkingSprite* GetSprite() const noexcept { return _sprite; }

private:
    inking::InkingSprite* _sprite = nullptr;
};

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    // 排错时把 SDL 自己的日志也放出来：渲染后端挑了哪个、透明标志有没有
    // 落地，都在里面（`SDL_LOG_PRIORITY_INFO` 就够）。
    if (SDL_getenv("INK_PET_DEBUG") != nullptr) {
        SDL_SetLogPriorities(SDL_LOG_PRIORITY_INFO);
    }
    const std::string frameDir = argc > 1 ? argv[1] : kDefaultFrameDir;
    const double fps = argc > 2 ? std::atof(argv[2]) : kDefaultFps;

    // ---- 1. 素材：目录 → 帧列表 → 帧尺寸 ----
    const std::vector<std::filesystem::path> frames = CollectFrames(frameDir);
    if (frames.empty()) {
        std::printf("找不到帧：%s\n", frameDir.c_str());
        std::printf("用法：inking_pet.exe <帧目录> [fps]\n");
        return 1;
    }

    int width = 0;
    int height = 0;
    if (!ReadPngSize(frames.front(), width, height)) {
        std::printf("读不出 PNG 尺寸：%s\n", Utf8(frames.front()).c_str());
        return 1;
    }
    std::printf("帧：%zu 张 %d×%d，%.1ffps（一圈 %.2fs）\n", frames.size(),
                width, height, fps, static_cast<double>(frames.size()) / fps);

    // ---- 2. 设计尺寸 = 帧尺寸 ----
    //
    // **必须是第一件事**：设计尺寸在窗口 / 场景构造那一刻就烘进根锚点与
    // letterbox 逻辑尺寸里，晚了 `SetDesignSize` 会拒绝（`window_config.h`）。
    if (!inking::SetDesignSize(width, height)) {
        std::printf("设计尺寸被拒（%d×%d）：窗口或场景已经定型？\n", width, height);
        return 1;
    }

    // ---- 3. 场景 + 窗口 ----
    PetScene scene(MakePetData(frames, width, height, fps));

    inking::InkingWindow& window = inking::InkingWindow::Instance();
    window.SetBorderless(true);              // 没有标题栏
    // `INK_PET_OPAQUE=1` 只用于排错：把透明关掉，一眼就能分出"没画"还是"没合成"。
    if (SDL_getenv("INK_PET_OPAQUE") == nullptr) {
        window.SetTransparent(true);         // 只有角色，没有底板（建窗前必须设）
    }
    window.SetAlwaysOnTop(true);             // 压在其他窗口之上
    window.SetBackgroundColor(0x00000000u);  // 清屏全透明

    // 透明窗口**只在 Windows 的 D3D11 后端成立**：SDL 只给那条路补了透明
    // swapchain，D3D12 明确不支持、Vulkan 多数环境不行。默认驱动由 SDL 自己挑，
    // 挑到 D3D12 就会得到"置顶、无边框、但背景是一整块黑"——正是这一版的症状。
    // 所以这里**强制** D3D11；建不出来再退回默认驱动（那时至少画面能出来）。
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "d3d11");
    if (!window.Open("pet")) {
        SDL_ResetHint(SDL_HINT_RENDER_DRIVER);
        std::printf("D3D11 建窗失败，退回默认驱动（背景可能不透）\n");
        if (!window.Open("pet")) {
            std::printf("建窗失败（见日志）\n");
            return 1;
        }
    }

    // 透明到底成没成：`IsTransparentApplied()` 读的是窗口上的**真实标志**，
    // 不是"我们请求过"。没成就说清楚——这一版的症状就是"角色旁边一整块黑"。
    if (SDL_getenv("INK_PET_OPAQUE") == nullptr) {
        std::printf(window.IsTransparentApplied()
                        ? "透明窗口：SDL_WINDOW_TRANSPARENT 已进窗口标志\n"
                        : "透明窗口：标志**没**落地，背景会是一整块黑\n");
    }

    if (scene.GetSprite() != nullptr && !scene.GetSprite()->IsValid()) {
        std::printf("帧动画配置有问题：%s\n",
                    scene.GetSprite()->GetConfigError().c_str());
    }

    // ---- 4. 主循环：跑帧 + 拖动窗口 ----
    //
    // 用 `PumpOnce()` 而不是 `Show()`：`Show()` 是"跑到关窗"的封装，中间没有
    // 插手的地方，而"拖着窗口走"必须每帧做一次。
    //
    // 拖动用**全局鼠标坐标**，不是场景里的设计坐标：窗口跟着指针动的时候，
    // 窗口内的设计坐标几乎不变（指针相对窗口没动），拿它算增量会得到
    // "走一步退一步"的抖动。全局坐标是稳定的。
    float lastGlobalX = 0.0f;
    float lastGlobalY = 0.0f;
    bool dragging = false;
    int frameIndex = 0;
    // 诊断开关（排错用，正常跑不看）：
    //   `INK_PET_DEBUG=1`   —— 第 2 帧 / 第 60 帧各打一行"组件到底认了什么"
    //   `INK_PET_SHOT=<路径>` —— 第 2 帧离屏渲一份 PNG（判断"没画出来"还是
    //                            "画了但窗口没合成上去"——屏幕上抓图在部分
    //                            环境里对 GPU 合成窗口不可靠，只有这个可信）
    const bool debugTrace = SDL_getenv("INK_PET_DEBUG") != nullptr;
    const char* shotPath = SDL_getenv("INK_PET_SHOT");

    while (window.PumpOnce()) {
        ++frameIndex;
        if (debugTrace && (frameIndex == 2 || frameIndex == 60)) {
            const auto stats = inking::detail::TextCacheStatsSnapshot();
            const inking::InkingSprite* sprite = scene.GetSprite();
            std::printf(
                "[debug] #%d index=%zu frames=%zu loaded=%zu playing=%d "
                "time=%.4f textures=%llu currentScene=%p window=%dx%d\n",
                frameIndex, sprite != nullptr ? sprite->GetFrameIndex() : 0,
                sprite != nullptr ? sprite->GetFrameCount() : 0,
                sprite != nullptr ? sprite->GetLoadedFrameCount() : 0,
                sprite != nullptr && sprite->IsPlaying() ? 1 : 0,
                sprite != nullptr ? sprite->GetAnimationTime() : 0.0,
                static_cast<unsigned long long>(stats.texturesCreated),
                static_cast<const void*>(window.GetCurrentScene()),
                window.GetWindowWidth(), window.GetWindowHeight());
        }
        if (shotPath != nullptr && frameIndex == 2) {
            const bool ok = inking::InkingWindow::RenderToTexture(
                shotPath, window.GetWindowWidth(), window.GetWindowHeight(),
                "pet");
            std::printf("[debug] offscreen=%d → %s\n", ok ? 1 : 0, shotPath);
        }

        float globalX = 0.0f;
        float globalY = 0.0f;
        const SDL_MouseButtonFlags buttons =
            SDL_GetGlobalMouseState(&globalX, &globalY);
        const bool leftDown = (buttons & SDL_BUTTON_LMASK) != 0;

        if (leftDown && !dragging) {
            dragging = true;  // 按下那一刻只记基准，不跳
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
