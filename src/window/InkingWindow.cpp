#include <window/InkingWindow.h>

#include <audio/InkingAudio.h>
#include <detail/InkLog.h>
#include <render/InkingText.h>
#include <timeline/Timeline.h>
#include <scene/InkingScene.h>
#include <scene/SceneLibrary.h>

#include <SDL3/SDL.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
// ------------------------------------------------------------ 窗口圆角（DWM）
//
// Windows 11 的 DWM 能替窗口画四角圆角（属性 `DWMWA_WINDOW_CORNER_PREFERENCE`
// = 33，`DWMWCP_ROUND` = 2）。走系统这条路而不是自己画，有两个理由：
//   ① 系统画的是**真圆角**（阴影、命中区域一起处理）；自己画要开透明窗口 +
//      逐像素 alpha，代价大得多、还和 SDL 的后端耦合；
//   ② 旧系统（Win10）不认这个属性——只要**动态加载** `dwmapi.dll` 并把失败
//      静默掉，就自动退回方角，不需要任何版本分支。
//
// ⚠️ **无边框窗口（C2）下 DWM 到底画不画圆角，取决于系统实现**：这里只保证
// "请求发出去了 + 把属性读回来的答案是圆角"，真正圆不圆是**观感**（`r5`：交给
// 用户看）。所以入口和观测口是分开的两个函数（请求 / 读回）。
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace {

/// `DWMWA_WINDOW_CORNER_PREFERENCE`（Win11 SDK 里的值）。写常量而不是引头文件：
/// 不依赖具体版本的 SDK，也不会在旧 SDK 上编不过。
constexpr DWORD kDwmwaWindowCornerPreference = 33;
constexpr int kDwmcpRound = 2;      ///< `DWMWCP_ROUND`
constexpr int kDwmcpDoNotRound = 1; ///< `DWMWCP_DONOTROUND`

using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
using DwmGetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, PVOID, DWORD);

/// 动态加载的 dwmapi 入口（**函数内静态量**，`AGENTS.md` §6 第 12 条）。
struct DwmApi {
    DwmSetWindowAttributeFn setAttribute = nullptr;
    DwmGetWindowAttributeFn getAttribute = nullptr;

    DwmApi() {
        HMODULE module = ::LoadLibraryW(L"dwmapi.dll");
        if (module == nullptr) {
            return;  // 很老的系统：连 dwmapi 都没有，圆角这件事直接作废
        }
        setAttribute = reinterpret_cast<DwmSetWindowAttributeFn>(
            reinterpret_cast<void*>(
                ::GetProcAddress(module, "DwmSetWindowAttribute")));
        getAttribute = reinterpret_cast<DwmGetWindowAttributeFn>(
            reinterpret_cast<void*>(
                ::GetProcAddress(module, "DwmGetWindowAttribute")));
    }
};

DwmApi& dwmApi() {
    static DwmApi api;
    return api;
}

/// SDL 窗口背后的 HWND（窗口还没建起来时给 nullptr）。
HWND NativeWindowHandle(SDL_Window* window) {
    if (window == nullptr) {
        return nullptr;
    }
    return static_cast<HWND>(SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER,
        nullptr));
}

/// 见 `InkingWindow::SetRoundedCorners` 的注释：返回的是"**属性读回来**是不是
/// 我们要的那一个"。
bool RequestRoundedCorners(SDL_Window* window, bool rounded) {
    HWND hwnd = NativeWindowHandle(window);
    if (hwnd == nullptr || dwmApi().setAttribute == nullptr) {
        return false;
    }

    const int preference = rounded ? kDwmcpRound : kDwmcpDoNotRound;
    if (FAILED(dwmApi().setAttribute(hwnd, kDwmwaWindowCornerPreference,
                                     &preference, sizeof(preference)))) {
        return false;  // Win10：不认这个属性，静默退回方角
    }

    // **读回才算数**（`AGENTS.md` §6 第 40 条那一族："设置成功"与"真的生效"是
    // 两件事）。DWM 在某些窗口样式下会忽略这个偏好，那时读回来是默认值 0。
    if (dwmApi().getAttribute == nullptr) {
        return true;  // 读不回来就只能相信 set 的返回值
    }
    int actual = 0;
    if (FAILED(dwmApi().getAttribute(hwnd, kDwmwaWindowCornerPreference, &actual,
                                     sizeof(actual)))) {
        return true;
    }
    return actual == preference;
}

}  // namespace

#else

namespace {

/// 非 Windows：没有这件事，一律"没生效"（调用方不需要分支）。
bool RequestRoundedCorners(SDL_Window*, bool) { return false; }

}  // namespace

#endif  // _WIN32

// ---------------------------------------------------------------------------
// 音频（B10）与文本（SDL3_ttf / SDL3_image）：**直接接真实实现**
//
// 这里曾经是一段"未接入的可选子系统"占位——三个放在匿名 namespace 里的空桩
// （`AudioShutdown` / `AudioTick` / `detail::ReleaseTextCache`），让窗口层在
// 两个子系统还没搬进来时先编过。
//
// 两层搬完之后它们就不再是占位，而是**遮蔽**：匿名 namespace 里的同名函数在
// 名字查找里更近，于是 `InkingWindow` 调的一直是空函数，而真实实现
// （`inking::AudioTick` 的淡入淡出推进、`inking::AudioShutdown` 的关设备、
// `inking::detail::ReleaseTextCache` 的 TTF_Quit）**一个调用者都没有**，
// 并且不报错、不警告。最刺眼的表现：即使音频宏打开了，
// `AudioPlayBgm(sound, 0.5f)` 也会永远停在增益 0——淡入曲线只在那段没人调的
// `AudioTick` 里推进。
//
// 删掉空桩 + include 真头（见文件顶部的两个 include）之后，三个调用点
// ——关窗、每帧、销毁渲染器前——一个字都没改。
// ---------------------------------------------------------------------------

namespace inking {

namespace {

constexpr const char* kModuleName = "InkingWindow";

/**
 * 帧节流的兜底：vsync 接不上时用的固定节拍（16ms ≈ 60Hz）。
 *
 * 注意它**不是**精确的 60fps：`sleep_for` 不扣本帧耗时，实际一帧是
 * 16ms + 工作时间。正常情况下走 vsync（`SDL_SetRenderVSync`），
 * 这条只在无头 / 无显示环境接不上 vsync 时兜底。
 */
constexpr std::chrono::microseconds kFrameInterval{16000};

/// 无头冒烟：设了 `INK_AUTOQUIT` 就跑这么久然后自己退出。
///
/// 计时用 steady_clock：主循环的节流和冒烟计时走同一套单调时钟，
/// 不掺 SDL 的计时器。
///
/// 时长可以用 `INK_AUTOQUIT_MS` 覆盖（毫秒）：2 秒对"开关一次抽屉"够用，
/// 但对**音频**不够——起播、淡入、放 SE、淡出这一串要好几秒
/// （源项目的 `examples/audio_demo` 就是为此把超时调到几秒；该示例未搬入）。
constexpr std::chrono::milliseconds kAutoQuitAfter{2000};

std::chrono::milliseconds AutoQuitDuration() {
    const char* raw = SDL_getenv("INK_AUTOQUIT_MS");
    if (raw == nullptr || *raw == '\0') {
        return kAutoQuitAfter;
    }
    char* end = nullptr;
    const long value = std::strtol(raw, &end, 10);
    if (end == raw || value <= 0) {
        return kAutoQuitAfter;
    }
    return std::chrono::milliseconds{value};
}

/**
 * 根锚点的尺寸 = 设计画布，不是窗口像素：子组件的锚点是相对设计空间算的。
 *
 * 这里读的是**运行期实际值**（`DesignWidth()`，由 `CXXCSS/config.json` 决定），
 * 并且顺手把"设计尺寸已被定型"记一笔——窗口是第一批按它对几何的地方之一，
 * 从这一刻起 `SetDesignSize` 就该拒绝改动了（`config/window_config.h`）。
 */
AnchorData rootAnchorData() {
    AnchorData data;
    data.width  = inking::DesignWidth();
    data.height = inking::DesignHeight();
    inking::detail::NoteDesignSizeTaken();
    return data;
}

/**
 * 设计单位 → 设备像素的倍率。
 *
 * letterbox 是整幅等比缩放，所以取两轴里小的那个，多出来的空间就是黑边。
 * 它只影响绘制换算，不标脏（docs/API.md「标脏」）；窗口层是唯一来源，
 * 组件不自己倒推。
 */
float letterboxMagnification(SDL_Window* window) {
    int pixelWidth = 0;
    int pixelHeight = 0;
    if (window == nullptr
        || !SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight)
        || pixelWidth <= 0 || pixelHeight <= 0) {
        return 1.0f;
    }

    const float scaleX = static_cast<float>(pixelWidth)
                       / static_cast<float>(inking::DesignWidth());
    const float scaleY = static_cast<float>(pixelHeight)
                       / static_cast<float>(inking::DesignHeight());
    return scaleX < scaleY ? scaleX : scaleY;
}

/**
 * 窗口的运行期状态。
 *
 * 放在实现文件的匿名命名空间里，是为了让公开头文件保持"零 SDL 依赖"：
 * 谁包含 <window/InkingWindow.h> 都不会被迫跟着包含 SDL 的头文件。
 * InkingWindow 本身是单例，这份状态也天然只有一份。
 */
struct WindowState {
    SDL_Window*   window = nullptr;     /** SDL 窗口句柄 */
    SDL_Renderer* renderer = nullptr;   /** SDL 渲染器句柄 */
    bool          rendererReady = false;/** 渲染器是否真的建起来了 */
    bool          vsyncActive = false;  /** vsync 是否接上了（决定还要不要自己 sleep） */
    bool          open = false;         /** 是否已经成功打开 */
    std::string   sceneName;            /** 当前要展示的场景名 */

    /**
     * @brief 帧时钟 + 时间线（D1 的落点）。
     *
     * 以前两者都是 `runLoop` 里的**局部变量**，于是"跑一帧就返回"根本做不到：
     * timeline 的累加器、上一帧时刻都是跨帧状态，重开一次就清零（逻辑步会
     * 重新计时、动画会跳一下）。搬到状态里之后，`Show()` 与 `PumpOnce()`
     * 共用**同一个**节拍器。
     */
    Timeline timeline;
    std::chrono::steady_clock::time_point previousFrame{};
    std::chrono::steady_clock::time_point start{};
    bool pumpReady = false;  ///< 上面三个量初始化过了没有

    /**
     * 帧末任务（D3）：`RunAfterFrames(n, fn)` 排进来的那些。
     *
     * `framesLeft` 每帧末减一，到 0 的那一帧执行。用 vector 而不是
     * priority_queue：任务数是个位数，顺序执行比"按时间排序"更好推理。
     */
    struct FrameTask {
        int framesLeft = 0;
        std::function<void()> action;
    };
    std::vector<FrameTask> frameTasks;
};

/** 返回唯一的窗口状态。 */
WindowState& state() {
    static WindowState instance;
    return instance;
}

/** 释放 SDL 资源并复位状态。 */
void releaseWindow(WindowState& s) {
    // 音频：设备与已加载的声音都属于"这一层自己"，窗口关掉就一起收掉
    // （B10）。放在 SDL_Quit 之前——SDL 的音频子系统得活着才能销毁流。
    AudioShutdown();

    if (s.renderer) {
        // 纹理属于渲染器：**先**把文字纹理层持有的纹理放掉，再销毁渲染器。
        // 反过来的话，那一层会拿着已经随渲染器死掉的句柄，下次再用就是野指针。
        // 下面这个是本文件里的**空实现占位**（文本层未接入，见文件顶上那段）。
        detail::ReleaseTextCache();
        SDL_DestroyRenderer(s.renderer);
        s.renderer = nullptr;
    }
    s.rendererReady = false;

    if (s.window) {
        SDL_DestroyWindow(s.window);
        s.window = nullptr;
    }

    if (s.open) {
        SDL_Quit();
        s.open = false;
    }

    s.sceneName.clear();
}

/**
 * 窗口坐标 → 设计坐标。
 *
 * letterbox 之下这是两套坐标：SDL 给的是窗口像素，场景认的是设计空间
 * （DesignWidth() × DesignHeight()）。换算是渲染器的职责，所以只在这里做一次，
 * 交给场景层的永远是设计坐标。
 *
 * 必须走渲染器换算，不能拿展示倍率乘除：letterbox 下设计画面只占窗口中间一块，
 * 上下（或左右）还有黑边，换算里有偏移。实测 1000×1200 的窗口配 1920×1080 的
 * 设计——等比倍率 0.5208、上下黑边各 318.8：
 *   正中 (500,600)：SDL 给 (960,540)，除以倍率会算成 (960,1152)；
 *   (500,0) 在黑边里：SDL 给负的 y（正确地判为空白），除以倍率会算成 y=0，在顶边误命中。
 *
 * 这条同时说明：**整体缩放不需要重烘命中表**——表建在设计坐标里，缩放只写
 * Magnification（不标脏），鼠标坐标每帧换算回设计空间再查表。
 */
void toDesign(SDL_Renderer* renderer, float windowX, float windowY,
              float& designX, float& designY) {
    designX = windowX;
    designY = windowY;
    if (renderer != nullptr) {
        SDL_RenderCoordinatesFromWindow(renderer, windowX, windowY,
                                        &designX, &designY);
    }
}

/**
 * 事件泵：把这一帧攒下的 SDL 事件吃干净，变成 MouseInput 里的状态。
 * 返回 false 表示该退出主循环了。
 *
 * 这里只“更新状态”，不派发事件：命中与投递留给下一帧的 Scene 层，
 * 这样才和设计稿里的 isDirty + query 流程对得上。
 */
bool pumpEvents(WindowState& s, MouseInput& mouse, KeyboardInput& keyboard,
                InkingWindow& window) {
    bool running = true;
    SDL_Event event{};

    // 触摸（B6）：SDL 的 finger 事件给的是**归一化**坐标（0~1），乘窗口像素尺寸
    // 才是窗口坐标；之后的"窗口坐标 → 设计坐标"和鼠标共用下面那条 `toDesign`。
    // 每帧读一次很便宜，而且免了"缩窗口之后什么时候该更新"这笔账。
    {
        int windowWidth = 0;
        int windowHeight = 0;
        SDL_GetWindowSize(s.window, &windowWidth, &windowHeight);
        mouse.SetWindowSize(static_cast<float>(windowWidth),
                            static_cast<float>(windowHeight));
    }

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                INK_LOG_INFO(kModuleName, "收到退出事件");
                running = false;
                break;

            case SDL_EVENT_KEY_DOWN:
                // 先喂给输入层，再判 ESC——这样"按下 ESC 的那一帧"，拿着焦点的
                // 组件也拿得到这次按键，不会被窗口层私吞。
                keyboard.UpdateFromSDL(event);
                if (event.key.key == SDLK_ESCAPE) {
                    // ESC 的语义是"退一层"，不是"直接退出程序"：
                    //   全屏 → 先退全屏（不然全屏里按一下就把程序关了）；
                    //   非全屏 → 走**退出闸门**（`SetExitConfirmHandler`，
                    //            业务可以在这里问一句"确定要退出吗"）。
                    // 仍保留"ESC 能关窗口"这条调试便利——没有它示例没法关窗。
                    if (window.IsFullscreen()) {
                        INK_LOG_INFO(kModuleName, "按下 ESC，退出全屏");
                        window.SetFullscreen(false);
                    } else {
                        INK_LOG_INFO(kModuleName, "按下 ESC，请求退出");
                        window.RequestExit();
                    }
                } else if (event.key.key == SDLK_F11) {
                    // 全屏是**窗口级**能力，所以在窗口层给一个内置键位；
                    // 业务可以另外绑快捷键（`GetShortcuts()`），两者互不冲突。
                    window.ToggleFullscreen();
                }
                break;

            case SDL_EVENT_KEY_UP:
                keyboard.UpdateFromSDL(event);
                break;

            case SDL_EVENT_TEXT_INPUT:
                // 输入法组合好的结果。SDL 要先 `SDL_StartTextInput(window)`
                // 才会发这个事件（`Show()` 里开了）。
                keyboard.UpdateFromSDL(event);
                break;

            case SDL_EVENT_WINDOW_FOCUS_LOST:
                // 按着键切走以后，抬起事件永远不会来：不清掉的话 `IsTyping()`
                // 会一直卡在 true、`IsKeyDown(方向键)` 会永远成立。
                keyboard.ClearHeldKeys();
                break;

            // 全屏不一定是我们自己切的（系统手势、别的程序、将来某个组件）。
            // 只信自己那个开关，就会出现"状态位说不是全屏、画面是全屏"的静默
            // 不一致；所以 SDL 的通知也收。`SetFullscreen` 是幂等的——值没变
            // 就早退，不会反过来再调一次 SDL。
            case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
                window.SetFullscreen(true);
                break;

            case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
                window.SetFullscreen(false);
                break;

            case SDL_EVENT_MOUSE_MOTION:
            case SDL_EVENT_MOUSE_WHEEL:
                mouse.UpdateFromSDL(event);
                break;

            // 触摸（B6）：最简映射——第一根手指当成鼠标（只认第一根，见
            // `MouseInput::UpdateFromSDL` 里那段）。归到和鼠标同一格里，
            // 是因为它产出的就是同一个指针状态。
            case SDL_EVENT_FINGER_DOWN:
            case SDL_EVENT_FINGER_MOTION:
            case SDL_EVENT_FINGER_UP:
            case SDL_EVENT_FINGER_CANCELED:
                mouse.UpdateFromSDL(event);
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                mouse.UpdateFromSDL(event);
                // 只记按下与抬起，移动不记——否则 Log.html 会被鼠标刷屏。
                INK_LOG_DEBUG(kModuleName,
                              std::string("鼠标") + (event.button.down ? "按下 " : "抬起 ")
                                  + std::to_string(static_cast<int>(event.button.button))
                                  + " @窗口 ("
                                  + std::to_string(static_cast<int>(event.button.x)) + ", "
                                  + std::to_string(static_cast<int>(event.button.y)) + ")");
                break;

            default:
                break;
        }
    }

    // 设计坐标一帧只换算一次：一帧里收到多少个移动事件都只算一次，
    // 和“每帧只判断一次”同拍。
    const MousePosition& windowPoint = mouse.GetState().position;
    float designX = 0.0f;
    float designY = 0.0f;
    toDesign(s.renderer, static_cast<float>(windowPoint.x),
             static_cast<float>(windowPoint.y), designX, designY);
    mouse.SetDesignPosition(designX, designY);

    return running;
}

/**
 * 每帧把指针状态喂给活跃场景，并派发一次。
 *
 * 位置：事件泵之后（设计坐标刚算完）、时间线推进之前——和
 * `SceneLibrary::RenderScene` 同一个落点（"只有活跃场景"这条规矩的唯一出口）。
 *
 * 为什么由窗口层喂：设计坐标的换算要走渲染器（letterbox 有黑边偏移），
 * 那是渲染器的职责，场景层不该认识渲染器（docs/AGENTS.md §6 第 8 条）。
 * 场景只接受"设计坐标 + 按没按下"这四个值。
 *
 * 为什么不在事件循环里直接派发：一帧里可能有多个移动事件，逐个派发等于
 * 同一帧判断多次；docs/InputDesign.md §1 定的是"每帧只判断一次"。
 */
void dispatchPointer(WindowState& s, const MouseInput& mouse) {
    InkingScene* scene = SceneLibrary::GetActiveScene();
    if (scene == nullptr) {
        return;
    }

    const MouseState& state = mouse.GetState();
    // 指针在不在窗口里：移出去时不该继续悬停，也不该在窗口外"按下"。
    const bool inside =
        s.window != nullptr
        && (SDL_GetWindowFlags(s.window) & SDL_WINDOW_MOUSE_FOCUS) != 0;

    // 滚轮（A4）：本帧累积的滚动量。和指针位置一样"只喂不判"——派发在
    // Scene::DispatchPointer 里统一做（目标：指针下最上面那个 / 拖动捕获者）。
    scene->SetPointerWheel(state.wheelX, state.wheelY);

    scene->SetPointerState(state.design.x, state.design.y, state.leftButtonDown,
                           inside);
    scene->DispatchPointer();
}

/**
 * 每帧把键盘的状态与事件喂给活跃场景，并派发一次。
 *
 * 位置：**指针派发之后**（见 runLoop）。顺序是有意的——先按位置把焦点算好
 * （可能刚因为这次点击换了人），再把这一帧的按键推给**新的**焦点节点；
 * 反过来的话，"点进输入框并立刻打字"的第一下会丢。
 *
 * 与指针那边不同，这里不需要问"窗口在不在焦点"：窗口失焦时 SDL 根本不会把
 * 按键事件送过来，而"按着键切走"那种半途状态由 `ClearHeldKeys` 收掉。
 */
void dispatchKeyboard(const KeyboardInput& keyboard) {
    InkingScene* scene = SceneLibrary::GetActiveScene();
    if (scene == nullptr) {
        return;
    }

    scene->SetKeyboardInput(keyboard);
    scene->DispatchKeyboard();
}

/**
 * 每帧把展示倍率刷成当前值。
 *
 * 不靠事件驱动：全屏切换、移动到不同 DPI 的显示器、系统缩放变化……SDL 发的事件
 * 并不统一（实测进全屏会发 PIXEL_SIZE_CHANGED **和** DISPLAY_SCALE_CHANGED，
 * 后者我们没接）。反正只是两次除法，每帧从当前像素尺寸重算最省心，也永远不会
 * 落后一帧。值没变就什么都不做。
 *
 * 设计坐标里的几何一点没动——这正是"场景不做布局变化"和"整体缩放不用重烘表"
 * 能成立的原因。
 */
void syncMagnification(WindowState& s, InkingWindow& window) {
    const float magnification = letterboxMagnification(s.window);
    const float previous = window.GetMagnification();
    if (magnification > previous - 1e-6f && magnification < previous + 1e-6f) {
        return;
    }
    window.SetMagnification(magnification);
    INK_LOG_DEBUG(kModuleName, "展示倍率=" + std::to_string(magnification));
}

/**
 * 上一帧真的渲染了哪个场景（没有场景时为 nullptr）。
 *
 * 纯自检用：主循环跑完、窗口释放时会清掉场景名，所以"窗口确实按名字解析到
 * 场景并把它渲染出来了"这件事，只能靠这里的记录来验。
 * 函数内静态量，避开静态初始化顺序问题（docs/AGENTS.md §6 第 12 条）。
 */
InkingScene*& lastRenderedScene() {
    static InkingScene* scene = nullptr;
    return scene;
}

/**
 * 排队中的截图请求（空 = 没有）。
 *
 * 住函数内静态量而不是窗口成员：`drawFrame` 是自由函数、只拿得到
 * `WindowState`，而"这一帧要不要截图"属于进程唯一的窗口状态，和
 * `lastRenderedScene()` 同族（`AGENTS.md` §2 那条"进程唯一槽位必须落 .cpp"）。
 */
std::string& pendingCapturePath() {
    static std::string path;
    return path;
}

/** 扩展名是不是 `.png`（大小写不敏感）。 */
bool hasPngExtension(const std::string& path) {
    if (path.size() < 4) {
        return false;
    }
    std::string ext = path.substr(path.size() - 4);
    for (char& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return ext == ".png";
}

/**
 * 把**已经编码好**的字节写进文件。
 *
 * 顺序有讲究：**先用 `std::fopen`**（走 CRT，不吃 SDL 的文件抽象层），失败才
 * 退回 SDL 的路径 API。两个理由：
 *   1. 某些受限环境（沙箱 / 只给 stdio 的容器）里 SDL 的文件层会被拦或截断，
 *      而 `fopen` 正常——实测过"文件名被截成前 7 个字符"这种荒唐现象；
 *   2. Windows 下窄串是 ANSI，`fopen` 对非 ASCII 路径会失败，而 SDL 的
 *      `SDL_IOFromFile` 收 UTF-8——两条路互补，覆盖面比任一条都大。
 */
bool writeBytesToFile(const std::vector<unsigned char>& bytes,
                      const std::string& path) {
    if (bytes.empty()) {
        return false;
    }
    if (std::FILE* file = std::fopen(path.c_str(), "wb")) {
        const std::size_t written =
            std::fwrite(bytes.data(), 1, bytes.size(), file);
        const bool closed = (std::fclose(file) == 0);
        if (written == bytes.size() && closed) {
            return true;
        }
        INK_LOG_WARN(kModuleName,
                     "写文件不完整（fwrite 短写），改用 SDL 文件层再试一次："
                         + path);
    }

    // 退回 SDL：它在 Windows 上认 UTF-8 路径。
    SDL_IOStream* stream = SDL_IOFromFile(path.c_str(), "wb");
    if (stream == nullptr) {
        INK_LOG_ERROR(kModuleName,
                      "写文件失败：" + path + "：" + std::string(SDL_GetError()));
        return false;
    }
    const std::size_t written = SDL_WriteIO(stream, bytes.data(), bytes.size());
    const bool flushed = SDL_FlushIO(stream);
    SDL_CloseIO(stream);
    if (written != bytes.size() || !flushed) {
        INK_LOG_ERROR(kModuleName, "写文件不完整（SDL 文件层）：" + path);
        return false;
    }
    return true;
}

/**
 * 一块**可写内存流**（`SDL_IOStreamInterface`）：编码先落到内存，再一次性写盘。
 *
 * 为什么绕这一道：`SDL_SavePNG(surface, path)` 直接把路径交给 SDL 的文件层，
 * 于是"能不能存图"取决于那层在这个环境里好不好使——在沙箱里实测会把文件名
 * 截断（写出一个叫 `probe_o` 的文件、返回 true）。编码与落盘分开之后，
 * 编码用 SDL（它只会写内存），落盘用自己的两条路（见 `writeBytesToFile`）。
 */
struct MemorySink {
    std::vector<unsigned char> bytes;
    std::size_t position = 0;
};

Sint64 SDLCALL sinkSize(void* userdata) {
    return static_cast<Sint64>(static_cast<MemorySink*>(userdata)->bytes.size());
}

Sint64 SDLCALL sinkSeek(void* userdata, Sint64 offset, SDL_IOWhence whence) {
    MemorySink& sink = *static_cast<MemorySink*>(userdata);
    const Sint64 size = static_cast<Sint64>(sink.bytes.size());
    Sint64 base = 0;
    switch (whence) {
        case SDL_IO_SEEK_SET:
            base = 0;
            break;
        case SDL_IO_SEEK_CUR:
            base = static_cast<Sint64>(sink.position);
            break;
        case SDL_IO_SEEK_END:
            base = size;
            break;
        default:
            return -1;
    }
    const Sint64 target = base + offset;
    if (target < 0) {
        return -1;
    }
    sink.position = static_cast<std::size_t>(target);
    return target;
}

size_t SDLCALL sinkRead(void* /*userdata*/, void* /*ptr*/, size_t /*size*/,
                        SDL_IOStatus* status) {
    *status = SDL_IO_STATUS_READONLY;
    return 0;
}

size_t SDLCALL sinkWrite(void* userdata, const void* ptr, size_t size,
                         SDL_IOStatus* status) {
    MemorySink& sink = *static_cast<MemorySink*>(userdata);
    if (size == 0) {
        return 0;
    }
    if (sink.position + size > sink.bytes.size()) {
        sink.bytes.resize(sink.position + size);
    }
    std::memcpy(sink.bytes.data() + sink.position, ptr, size);
    sink.position += size;
    *status = SDL_IO_STATUS_READY;
    return size;
}

bool SDLCALL sinkFlush(void* /*userdata*/, SDL_IOStatus* status) {
    *status = SDL_IO_STATUS_READY;
    return true;  // 内存流没有缓冲可刷
}

bool SDLCALL sinkClose(void* /*userdata*/) { return true; }

/**
 * 把渲染器当前的输出存成图片。
 *
 * PNG 只有 SDL ≥ 3.4 的核心库才有（`SDL_SavePNG_IO`）。这里**故意不引
 * SDL3_image 的 `IMG_SavePNG`**：截图是排错手段，不该因为一个可选依赖没开
 * 就"能用 / 不能用"取决于构建配置。老 SDL 上请用 `.bmp`。
 *
 * 读到的是**整个窗口帧缓冲**（`rect = nullptr`），含 letterbox 黑边——那正是
 * "窗口长什么样"的答案。
 */
bool writeRendererImage(SDL_Renderer* renderer, const std::string& path) {
    if (renderer == nullptr || path.empty()) {
        return false;
    }

    SDL_Surface* shot = SDL_RenderReadPixels(renderer, nullptr);
    if (shot == nullptr) {
        INK_LOG_ERROR(kModuleName,
                      "截图失败（读像素）：" + std::string(SDL_GetError()));
        return false;
    }

    // ① 编码到内存：这一段只用 SDL 的**内存**能力，不碰文件系统。
    MemorySink sink;
    SDL_IOStreamInterface iface{};
    iface.version = sizeof(iface);
    iface.size = sinkSize;
    iface.seek = sinkSeek;
    iface.read = sinkRead;
    iface.write = sinkWrite;
    iface.flush = sinkFlush;
    iface.close = sinkClose;

    SDL_IOStream* stream = SDL_OpenIO(&iface, &sink);
    if (stream == nullptr) {
        INK_LOG_ERROR(kModuleName,
                      "截图失败（建内存流）：" + std::string(SDL_GetError()));
        SDL_DestroySurface(shot);
        return false;
    }

    bool encoded = false;
    if (hasPngExtension(path)) {
#if SDL_VERSION_ATLEAST(3, 4, 0)
        encoded = SDL_SavePNG_IO(shot, stream, /*closeio=*/false);
#else
        INK_LOG_ERROR(kModuleName,
                      "这个 SDL 版本没有 PNG 保存能力（需要 3.4+），请改用 .bmp："
                          + path);
        SDL_CloseIO(stream);
        SDL_DestroySurface(shot);
        return false;
#endif
    } else {
        encoded = SDL_SaveBMP_IO(shot, stream, /*closeio=*/false);
    }
    SDL_CloseIO(stream);
    SDL_DestroySurface(shot);

    if (!encoded) {
        INK_LOG_ERROR(kModuleName,
                      "截图编码失败：" + path + "：" + std::string(SDL_GetError()));
        return false;
    }

    // ② 落盘：内存 → 文件（两条路，见 `writeBytesToFile`）。
    return writeBytesToFile(sink.bytes, path);
}

/**
 * 绘制这一帧：底色 → 当前场景的绘制列表 → 指针标记。
 *
 * 场景那一笔是真正的渲染路径：`InkingScene::Render` 在内部把顺序重建到最新
 * （惰性、每帧最多一次），再按 z 序把每个节点提交给渲染器。
 *
 * 指针小方块是临时脚手架，作用只有一个——肉眼确认「窗口坐标 → 设计坐标」
 * 换算对不对：拉成别的窗口比例，方块也应该准确贴在指针上。
 * SDF 形状层 / 样式层接手后，这一段连同 SDL_RenderFillRect 一起删掉。
 */
void drawFrame(WindowState& s, const MouseInput& mouse,
               std::uint32_t background) {
    SDL_Renderer* renderer = s.renderer;

    // 自检钩子：记录这一帧真的渲染了哪个场景。
    // 窗口释放时会清掉场景名，所以"窗口确实解析到场景并渲染了"这件事
    // 只能靠这里的记录来验。
    lastRenderedScene() = SceneLibrary::GetActiveScene();

    if (renderer == nullptr) {
        return;
    }

    // 清屏色由窗口对象给（`SetBackgroundColor`）：亮色场景要浅底，暗色主题要深底
    // ——组件一个字都不知道这件事。
    SDL_SetRenderDrawColor(renderer,
                           static_cast<Uint8>((background >> 16) & 0xFFu),
                           static_cast<Uint8>((background >> 8) & 0xFFu),
                           static_cast<Uint8>(background & 0xFFu),
                           static_cast<Uint8>((background >> 24) & 0xFFu));
    SDL_RenderClear(renderer);

    // 当前场景的整棵渲染树。经 SceneLibrary 走一道，而不是直接调场景的
    // Render（那是 protected）：这样"只有活跃场景会被渲染"这条规矩只有
    // 一个落点，将来上合批时提交层也落在这里。没有活跃场景时返回 false。
    SceneLibrary::RenderScene(renderer);

    // 截图（P2-6）：就落在这里——场景画完、`Present` 还没发生，后缓冲里
    // 正好是这一帧。**必须在指针小方块之前**，否则截图会带上那个调试方块。
    if (!pendingCapturePath().empty()) {
        const std::string path = pendingCapturePath();
        pendingCapturePath().clear();
        if (writeRendererImage(renderer, path)) {
            INK_LOG_INFO(kModuleName, "已截图：" + path);
        }
    }

    if (s.rendererReady) {
        const MousePoint& point = mouse.GetState().design;
        constexpr float kCursorSize = 16.0f;
        const SDL_FRect cursor{point.x - kCursorSize * 0.5f,
                               point.y - kCursorSize * 0.5f,
                               kCursorSize, kCursorSize};
        SDL_SetRenderDrawColor(renderer, 240, 200, 80, 255);
        SDL_RenderFillRect(renderer, &cursor);
    }

    SDL_RenderPresent(renderer);
}

/**
 * 帧末任务（D3）：把 `framesLeft` 减一，到 0 的那些**先取出来再执行**。
 *
 * 先取出来再执行是必须的：任务里常常又要排一个（"再等 3 帧做下一步"），
 * 边遍历边改 vector 会失效。
 */
void runFrameTasks(WindowState& s) {
    if (s.frameTasks.empty()) {
        return;  // 稳态：一次判空
    }
    std::vector<WindowState::FrameTask> due;
    for (std::size_t i = 0; i < s.frameTasks.size();) {
        WindowState::FrameTask& task = s.frameTasks[i];
        --task.framesLeft;
        if (task.framesLeft <= 0) {
            due.push_back(std::move(task));
            s.frameTasks.erase(s.frameTasks.begin()
                               + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        ++i;
    }
    for (WindowState::FrameTask& task : due) {
        if (task.action) {
            task.action();
        }
    }
}

/**
 * 跑**一帧**：事件泵 → 展示倍率 → 时间线 → 绘制 → 帧末收尾 → 帧末任务 → 节流。
 *
 * 从 `runLoop` 里拆出来（D1）是这一版唯一的结构性改动：拆之前"跑一帧就返回"
 * 做不到，自检只能塞在 `onRenderBoundTick` 里数帧。拆之后 `Show()` 是
 * `while (stepFrame(...))`，`PumpOnce()` 是"调一次"，两条路**共用同一份顺序**
 * ——不存在"阻塞跑和手动跑行为不一样"这种最难查的分叉。
 *
 * 时间线是这里的节拍器：逻辑步固定 50Hz，每帧通道和渲染同拍。
 * 接上 vsync 之后节流交给 SDL，不再自己 sleep。
 *
 * @return 还要不要继续跑（false = 收到关窗请求 / 冒烟时间到）。
 */
bool stepFrame(WindowState& s, MouseInput& mouse, KeyboardInput& keyboard,
               InkingWindow& window) {
    // 0. 时钟初始化：第一次进来（或重新 Open 之后）才做。
    if (!s.pumpReady) {
        s.previousFrame = std::chrono::steady_clock::now();
        s.start = s.previousFrame;
        s.pumpReady = true;

        // 时间线的两条通道**必须在这里挂**（不是只在 runLoop 里挂）：
        // `PumpOnce()` 不走 runLoop，漏了这一步就是"帧跑了、场景一帧都没收到"
        // ——自检里表现为 `onRenderBoundTick` 计数为 0，非常像"能力没实现"。
        s.timeline.SetLogicCallback([](double fixedStep) {
            // 固定逻辑步：每次派发都喂同一个步长，状态机要的就是确定的步。
            if (InkingScene* scene = SceneLibrary::GetActiveScene()) {
                scene->TickLogic(fixedStep);
            }
        });
        // 每帧通道跟着渲染走：真实 delta，和这一帧画出来的东西对齐。
        s.timeline.SetFrameCallback([](double delta) {
            if (InkingScene* scene = SceneLibrary::GetActiveScene()) {
                scene->TickFrame(delta);
            }
        });
    }

    // 1. 事件泵：退出、键盘、鼠标都在这里变成状态。
    bool running = pumpEvents(s, mouse, keyboard, window);

    // 1a. 全局快捷键（B）：**排在指针 / 键盘派发之前**。
    //     它是"应用级命令"，语义上先于任何组件的局部反应；命中后会把那条
    //     Down 事件从本帧事件里消费掉，于是焦点输入框收不到 `Ctrl+S`，
    //     但"按着 Ctrl"这个跨帧状态不受影响。
    window.GetShortcuts().Dispatch(keyboard);

    // 1b. 关窗请求（C2）：无边框下的自绘关闭按钮、或业务代码调 `RequestClose()`
    //     都走这条路。它和 ESC 是同一个落点（都在"这一帧还要不要继续"上），
    //     区别只是来源是**程序自己**而不是 SDL 事件。
    if (window.IsCloseRequested()) {
        INK_LOG_INFO(kModuleName, "收到关窗请求（自绘标题栏 / 业务代码）");
        running = false;
    }

    // 1c. 预加载收尾（帧首）：管家线程跑完 `onPreload` 的场景，在这一步
    //     回到 UI 线程做它的 `onPreloadFinished`——建组件 / 建纹理都只在
    //     UI 线程做得了。放在帧首而不是帧末：本帧的绘制就能用上刚装好的东西。
    SceneLibrary::DrainPreloadCompletions();

    // 2. 展示倍率跟着窗口 / DPI 走。
    syncMagnification(s, window);

    // 2b. 指针：把设计坐标与按键状态喂给活跃场景，并派发一次
    //     （悬停进出 / 按下 / 抬起触发点击）。放在推进时间线之前，
    //     这样本帧画出来的就是本帧的交互结果，不会差一帧。
    dispatchPointer(s, mouse);

    // 2c. 键盘：**排在指针之后**——点击可能刚把焦点换给别人，本帧的按键
    //     要推给新的焦点节点（"点进输入框并立刻打字"的第一下不能丢）。
    //     它同样要放在推进时间线之前，理由和指针那条一样。
    dispatchKeyboard(keyboard);

    // 3. 推进时间线。delta 用**真实流逝时间**，所以某帧掉到 30ms 也不会
    //    让逻辑时间相对墙上时钟漂移——累加器会把欠的步补上。
    //
    //    D4：装了固定 delta（`SetFixedDelta`）就用它，不看墙上时钟——
    //    动画 / 主题过渡的截图要在 CI 里逐像素稳定，就必须去掉"这一帧花了多久"
    //    这个变量。固定 delta 下也**不节流**（否则跑 100 帧要 1.6 秒）。
    const auto now = std::chrono::steady_clock::now();
    const double measuredDelta =
        std::chrono::duration<double>(now - s.previousFrame).count();
    s.previousFrame = now;

    const float fixedDelta = window.GetFixedDelta();
    const double deltaSeconds =
        fixedDelta > 0.0f ? static_cast<double>(fixedDelta) : measuredDelta;

    s.timeline.Advance(deltaSeconds);

    // 3b. 音频（B10）：**每帧都要推进**——它和场景无关（标题画面没场景也要
    //     有 BGM），所以不能挂在 timeline 那条"给活跃场景"的通道上。
    //     用同一个真实 delta：声音不该跟着帧率走。
    AudioTick(static_cast<float>(deltaSeconds));

    // 4. 绘制：底色 + 当前场景的整棵渲染树 + 指针标记。
    drawFrame(s, mouse, window.GetBackgroundColor());

    // 5. 帧末收尾：瞬时状态一帧只清一次。放在这里而不是事件循环里，
    //    否则一帧里的多个移动事件会被后一个清掉。
    //
    //    场景的脏标记也在这里**统一消费**（T-3）：一帧里可能连着标了很多次，
    //    攒到最后只处理这一回。只消费**活跃**场景——静默 / 停放 / 已关闭的
    //    场景脏要留着，切回来时才有意义。
    if (InkingScene* scene = SceneLibrary::GetActiveScene()) {
        scene->ConsumeFrameDirty();
    }
    mouse.ResetFrameFlags();
    // 键盘的瞬时部分同理：本帧事件清掉，**按着的键留着**（跨帧状态）。
    keyboard.ResetFrameFlags();

    // 5a. 帧末任务（D3）：`RunAfterFrames(n, fn)` 攒下的那些。
    //
    //     为什么在"脏消费之后"：任务里常常要读"这一帧的结果"（命中、尺寸、
    //     动画进度），而消费之后那些值才是定稿的。为什么在"待销毁场景提交
    //     之前"：任务里可能刚把所有引用交出去，场景下一帧再析构更安全。
    runFrameTasks(s);

    // 5b. 待销毁的场景在这里真正析构（`SceneLibrary::DestroyScene` 只入队）。
    //
    //     为什么必须等到帧末：销毁场景会连带拆掉它整棵子树，而上面这一帧的
    //     Render / Tick / 指针派发都在遍历那份绘制列表——遍历中途拆容器就是
    //     悬垂。攒到这一步再删，"这一刻没有人在看它"。
    SceneLibrary::CommitPendingDestructions();

    // 6. 节流。接了 vsync 就交给 SDL（它会阻塞到下一次垂直回扫），
    //    没接上时才退回固定 sleep——sleep 不扣本帧耗时，
    //    实际是 16ms + 工作时间，这是没 vsync 时的将就之法。
    //    固定 delta（D4）下不节流：手动跑帧要的是"快"，不是"像真的"。
    if (!s.vsyncActive && fixedDelta <= 0.0f) {
        std::this_thread::sleep_for(kFrameInterval);
    }

    const bool autoQuit = SDL_getenv("INK_AUTOQUIT") != nullptr;
    if (autoQuit
        && std::chrono::steady_clock::now() - s.start > AutoQuitDuration()) {
        running = false;
    }

    return running;
}

/**
 * 主循环：一直跑 `stepFrame` 到它说不跑为止。
 *
 * 位置：**指针派发之后**（见 stepFrame 里的顺序）。这个函数只负责循环，
 * 一帧做什么全在 `stepFrame` 里——那样 `PumpOnce()` 才可能是"同一份顺序"。
 */
void runLoop(WindowState& s, MouseInput& mouse, KeyboardInput& keyboard,
             InkingWindow& window) {
    // 零代码截图（P2-6）：`INK_CAPTURE=<路径>` 让**第一帧**落一张图。
    // 和 `INK_AUTOQUIT` 同一族——排错时"先看一眼画面"比加日志快得多，
    // 而此刻改业务代码往往是最不方便的事。只截一次，之后照常跑。
    if (const char* capturePath = SDL_getenv("INK_CAPTURE")) {
        if (capturePath[0] != '\0' && s.renderer != nullptr) {
            pendingCapturePath() = capturePath;
        }
    }

    // 整个 UI 的唯一时间源。逻辑步长取默认的 1/50s。
    // **两条通道在 `stepFrame` 的初始化里挂**（`PumpOnce()` 也要用同一份），
    // 这里不再重复——两处都挂会出现"谁最后写谁生效"，而它们是同一份逻辑，
    // 重复只是掩盖"漏挂"这个真问题。

    bool running = true;
    while (running) {
        running = stepFrame(s, mouse, keyboard, window);
    }
}

/**
 * 缩放候选档位（**候选**，不是最终表）。
 *
 * 最终能用哪几档由 `GetZoomLadder()` 在运行期筛：设计尺寸来自
 * `window_config.h`（将来由 inkgen 生成），能不能到 2 倍取决于**这台机器的
 * 显示器**装不装得下 1280×2 × 720×2。写死一张"最终表"在换设计尺寸、
 * 换小屏、或跑在多显示器上的那天就会错。
 *
 * 为什么是这几档：0.25 是"再小就看不清"的下限；0.67 / 0.75 对应"笔记本屏上
 * 一屏放下整个设计画面"；1.25 ~ 2.0 是常见的放大区间；3.0 / 4.0 只在高分屏上
 * 才可达（`GetZoomLadder` 会自动把它们筛掉）。
 */
constexpr float kZoomCandidates[] = {0.25f, 0.4f, 0.5f, 0.67f, 0.75f, 1.0f,
                                     1.25f, 1.5f, 1.75f, 2.0f, 2.5f, 3.0f,
                                     4.0f};

}  // namespace

InkingWindow::InkingWindow() : InkingStaticWidget(nullptr, rootAnchorData()) {}

InkingScene* GetLastRenderedScene() noexcept {
    return lastRenderedScene();
}

InkingWindow::~InkingWindow() {
    WindowState& s = state();
    if (s.open) {
        INK_LOG_INFO(kModuleName, "窗口销毁");
    }
    releaseWindow(s);
}

InkingWindow& InkingWindow::Instance() {
    static InkingWindow instance;
    return instance;
}

InkingScene* InkingWindow::GetCurrentScene() const {
    // 当前活跃（正在渲染）的场景。活跃态由 SceneLibrary 维护：
    // 切场景时旧的自动进入静默，所以这里拿到的永远是"该渲染的那一个"。
    return SceneLibrary::GetActiveScene();
}

int InkingWindow::GetWindowWidth() const noexcept {
    return _windowWidth;
}

int InkingWindow::GetWindowHeight() const noexcept {
    return _windowHeight;
}

bool InkingWindow::setWindowWidth(int width) {
    if (width <= 0) {
        INK_LOG_ERROR(kModuleName, "宽度必须为正数：" + std::to_string(width));
        return false;
    }

    _windowWidth = width;

    // 窗口已经开出来时立刻跟随；还没开就只是记下来，等 Show() 时用
    WindowState& s = state();
    if (s.window) {
        SDL_SetWindowSize(s.window, _windowWidth, _windowHeight);
        // 展示倍率不用在这里算：主循环每帧都会刷一次（见 syncMagnification）。
    }

    INK_LOG_DEBUG(kModuleName, "窗口宽度=" + std::to_string(_windowWidth));
    return true;
}

bool InkingWindow::setWindowHeight(int height) {
    if (height <= 0) {
        INK_LOG_ERROR(kModuleName, "高度必须为正数：" + std::to_string(height));
        return false;
    }

    _windowHeight = height;

    WindowState& s = state();
    if (s.window) {
        SDL_SetWindowSize(s.window, _windowWidth, _windowHeight);
        // 同上：倍率由主循环每帧刷新。
    }

    INK_LOG_DEBUG(kModuleName, "窗口高度=" + std::to_string(_windowHeight));
    return true;
}

// ---------------------------------------------------------------------------
// 无边框窗口（C2）
// ---------------------------------------------------------------------------

bool InkingWindow::SetBorderless(bool borderless) {
    if (_borderless == borderless) {
        return false;  // 幂等
    }
    _borderless = borderless;

    WindowState& s = state();
    if (s.window != nullptr) {
        // 已经开出来了：改边框。SDL3 这边返回 bool（true = 成功）。
        if (!SDL_SetWindowBordered(s.window, !_borderless)) {
            INK_LOG_WARN(kModuleName,
                         std::string("SDL_SetWindowBordered 失败：")
                             + SDL_GetError());
        }
    }
    INK_LOG_INFO(kModuleName,
                 std::string("无边框窗口 = ") + (_borderless ? "开" : "关"));
    return true;
}

bool InkingWindow::IsBorderless() const noexcept { return _borderless; }

int InkingWindow::GetWindowX() const noexcept { return _windowX; }

int InkingWindow::GetWindowY() const noexcept { return _windowY; }

void InkingWindow::MoveWindowBy(int dx, int dy) {
    if (dx == 0 && dy == 0) {
        return;  // 空操作：不记一笔无意义的移动
    }
    // 先记账再同步。记账是**唯一真相**：没有真实窗口时（自检 / 还没 Show）
    // 位置变化量照样可断言；窗口在的话顺手推给 SDL。
    _windowX += dx;
    _windowY += dy;

    WindowState& s = state();
    if (s.window != nullptr) {
        SDL_SetWindowPosition(s.window, _windowX, _windowY);
    }
    INK_LOG_DEBUG(kModuleName, "窗口移到 (" + std::to_string(_windowX) + ", "
                                   + std::to_string(_windowY) + ")");
}

void InkingWindow::MinimizeWindow() {
    WindowState& s = state();
    if (s.window == nullptr) {
        return;
    }
    if (!SDL_MinimizeWindow(s.window)) {
        INK_LOG_WARN(kModuleName,
                     std::string("SDL_MinimizeWindow 失败：") + SDL_GetError());
    }
}

void InkingWindow::RequestClose() {
    // 关窗 = 退出主循环 = 退出程序：两条路必须是同一个闸门，否则"确认退出"
    // 会被自绘标题栏的 × 绕过去（那是最容易被漏掉的一条旁路）。
    RequestExit();
}

void InkingWindow::RequestClose(int exitCode) {
    // 先记账再走闸门：被否决的这次关窗请求**不改退出码**——"用户点了取消"
    // 不该让上一轮的失败码生效，也不该把已经记下的失败码抹成 0。
    if (_exitConfirm && !_exitConfirm()) {
        INK_LOG_INFO(kModuleName, "退出请求被确认回调否决，主循环继续");
        return;
    }
    _exitCode = exitCode;
    _closeRequested = true;
}

int InkingWindow::GetExitCode() const noexcept { return _exitCode; }

void InkingWindow::SetExitConfirmHandler(ExitConfirmHandler handler) {
    _exitConfirm = std::move(handler);
}

void InkingWindow::RequestExit() {
    if (_exitConfirm && !_exitConfirm()) {
        // 否决：什么旗都不置，主循环照常跑。业务那边要么弹了模态框
        //（自绘的，异步），要么干脆"这次不退了"。
        INK_LOG_INFO(kModuleName, "退出请求被确认回调否决，主循环继续");
        return;
    }
    _closeRequested = true;
}

bool InkingWindow::IsCloseRequested() const noexcept { return _closeRequested; }

// ---------------------------------------------------------------------------
// 全屏
// ---------------------------------------------------------------------------

bool InkingWindow::SetFullscreen(bool fullscreen) {
    if (_fullscreen == fullscreen) {
        return false;  // 幂等：SDL 的通知与自己的调用会撞在一起，靠这条挡住
    }
    _fullscreen = fullscreen;

    WindowState& s = state();
    if (s.window != nullptr) {
        if (!SDL_SetWindowFullscreen(s.window, fullscreen)) {
            INK_LOG_WARN(kModuleName,
                         std::string("SDL_SetWindowFullscreen 失败：")
                             + SDL_GetError());
        }
    }
    // 窗口还没建起来时只记账：`Show()` 建完窗口会补应用一次（和圆角同一条路）。

    INK_LOG_INFO(kModuleName,
                 std::string("全屏 = ") + (fullscreen ? "开" : "关"));
    fullscreenChanged.Emit(_fullscreen);
    return true;
}

bool InkingWindow::IsFullscreen() const noexcept { return _fullscreen; }

void InkingWindow::ToggleFullscreen() { SetFullscreen(!_fullscreen); }

// ---------------------------------------------------------------------------
// 缩放（无边框窗口的代偿：等比改窗口尺寸 ⇒ letterbox 没有黑边）
// ---------------------------------------------------------------------------

float InkingWindow::maxUsableZoom() const noexcept {
    // 没有视频子系统时问显示器会顺带初始化 SDL，那不是本函数该干的事。
    if (SDL_WasInit(SDL_INIT_VIDEO) == 0) {
        return kMaxZoom;
    }

    WindowState& s = state();
    SDL_DisplayID display = 0;
    if (s.window != nullptr) {
        display = SDL_GetDisplayForWindow(s.window);  // 多显示器：跟着窗口走
    }
    if (display == 0) {
        display = SDL_GetPrimaryDisplay();
    }

    SDL_Rect usable{};
    if (display == 0 || !SDL_GetDisplayUsableBounds(display, &usable)
        || usable.w <= 0 || usable.h <= 0) {
        return kMaxZoom;  // 无头 / 拿不到：不按屏幕夹
    }

    const float byWidth =
        static_cast<float>(usable.w) / static_cast<float>(DesignWidth());
    const float byHeight =
        static_cast<float>(usable.h) / static_cast<float>(DesignHeight());
    const float fit = byWidth < byHeight ? byWidth : byHeight;
    return fit < kMaxZoom ? fit : kMaxZoom;
}

std::vector<float> InkingWindow::GetZoomLadder() const {
    const float ceiling = maxUsableZoom();
    std::vector<float> ladder;
    for (const float candidate : kZoomCandidates) {
        if (candidate < kMinZoom - 1e-4f || candidate > ceiling + 1e-4f) {
            continue;
        }
        ladder.push_back(candidate);
    }
    if (ladder.empty()) {
        // 极端情况（屏幕比 0.25 倍还小）：至少留一档，别返回空表让调用方除零。
        ladder.push_back(ceiling < kMinZoom ? kMinZoom : ceiling);
    }
    return ladder;
}

float InkingWindow::effectiveZoom() const noexcept {
    WindowState& s = state();
    int width = 0;
    int height = 0;
    if (s.window != nullptr && SDL_GetWindowSize(s.window, &width, &height)
        && width > 0 && height > 0) {
        const float sx = static_cast<float>(width)
                       / static_cast<float>(DesignWidth());
        const float sy = static_cast<float>(height)
                       / static_cast<float>(DesignHeight());
        return sx < sy ? sx : sy;
    }
    return _zoom;
}

void InkingWindow::applyZoom(float zoom) {
    _zoom = zoom;

    const int newWidth =
        static_cast<int>(std::lround(static_cast<float>(DesignWidth()) * zoom));
    const int newHeight =
        static_cast<int>(std::lround(static_cast<float>(DesignHeight()) * zoom));
    const int safeWidth = newWidth > 0 ? newWidth : 1;
    const int safeHeight = newHeight > 0 ? newHeight : 1;

    WindowState& s = state();
    if (s.window != nullptr) {
        // **窗口中心不动**：缩放时如果只改尺寸，窗口会从左上角"往右下长"，
        // 用户会觉得整个界面在漂。中心保持是桌面软件缩放该有的手感。
        int oldX = 0;
        int oldY = 0;
        int oldWidth = 0;
        int oldHeight = 0;
        SDL_GetWindowPosition(s.window, &oldX, &oldY);
        SDL_GetWindowSize(s.window, &oldWidth, &oldHeight);
        const int centerX = oldX + oldWidth / 2;
        const int centerY = oldY + oldHeight / 2;

        SDL_SetWindowSize(s.window, safeWidth, safeHeight);

        int newX = centerX - safeWidth / 2;
        int newY = centerY - safeHeight / 2;

        // 夹进显示器可用区域：放大到接近屏幕时，中心保持会把窗口顶出屏幕，
        // 那时标题栏 / 菜单栏就摸不到了（无边框下更是死路）。
        SDL_Rect usable{};
        const SDL_DisplayID display = SDL_GetDisplayForWindow(s.window);
        if (display != 0 && SDL_GetDisplayUsableBounds(display, &usable)) {
            const int maxX = usable.x + usable.w - safeWidth;
            const int maxY = usable.y + usable.h - safeHeight;
            if (newX < usable.x) {
                newX = usable.x;
            }
            if (newY < usable.y) {
                newY = usable.y;
            }
            if (newX > maxX) {
                newX = maxX;
            }
            if (newY > maxY) {
                newY = maxY;
            }
        }
        SDL_SetWindowPosition(s.window, newX, newY);
        // 读回：窗口管理器会自己修正（贴边、跨屏），记账要跟着事实走。
        SDL_GetWindowPosition(s.window, &_windowX, &_windowY);
    }

    // 逻辑尺寸的账一直记着：没窗口时它是 `Show()` 建窗口用的尺寸。
    _windowWidth = safeWidth;
    _windowHeight = safeHeight;

    INK_LOG_DEBUG(kModuleName,
                  "缩放 " + std::to_string(_zoom) + " → 窗口 "
                      + std::to_string(safeWidth) + "x"
                      + std::to_string(safeHeight));
}

bool InkingWindow::SetZoom(float zoom) {
    if (!(zoom > 0.0f)) {  // 顺带挡住 NaN
        INK_LOG_ERROR(kModuleName,
                      "缩放必须是正数：" + std::to_string(zoom));
        return false;
    }
    if (IsFullscreen()) {
        // 全屏的尺寸由系统给，这时候改窗口尺寸没有意义。退出全屏后
        // `_zoom` 仍然是上一次设置的那档，窗口会回到它对应的尺寸。
        INK_LOG_DEBUG(kModuleName, "全屏下忽略缩放请求");
        return false;
    }

    const std::vector<float> ladder = GetZoomLadder();
    float target = ladder.front();
    for (const float candidate : ladder) {
        if (std::fabs(candidate - zoom) < std::fabs(target - zoom)) {
            target = candidate;
        }
    }

    WindowState& s = state();
    const int expectedWidth =
        static_cast<int>(std::lround(static_cast<float>(DesignWidth()) * target));
    const int expectedHeight = static_cast<int>(
        std::lround(static_cast<float>(DesignHeight()) * target));

    bool sameAsRecorded = std::fabs(target - _zoom) < 1e-4f;
    if (s.window != nullptr) {
        int width = 0;
        int height = 0;
        SDL_GetWindowSize(s.window, &width, &height);
        sameAsRecorded = sameAsRecorded && width == expectedWidth
                      && height == expectedHeight;
    }
    if (sameAsRecorded) {
        return false;  // 幂等
    }

    applyZoom(target);
    return true;
}

bool InkingWindow::ZoomBy(int steps) {
    if (steps == 0) {
        return false;
    }
    const std::vector<float> ladder = GetZoomLadder();
    if (ladder.empty()) {
        return false;
    }

    // 起点按**实际尺寸**反推，而不是用 `_zoom`：业务可能用 `setWindowWidth`
    // 把窗口调成了非档位尺寸，那时用历史值会让滚轮"跳回去一下"。
    const float current = effectiveZoom();
    std::size_t nearest = 0;
    float best = std::fabs(ladder.front() - current);
    for (std::size_t i = 1; i < ladder.size(); ++i) {
        const float distance = std::fabs(ladder[i] - current);
        if (distance < best) {
            best = distance;
            nearest = i;
        }
    }

    const long wanted = static_cast<long>(nearest) + static_cast<long>(steps);
    long clamped = wanted;
    if (clamped < 0) {
        clamped = 0;
    }
    if (clamped > static_cast<long>(ladder.size()) - 1) {
        clamped = static_cast<long>(ladder.size()) - 1;
    }

    if (clamped == static_cast<long>(nearest)
        && std::fabs(ladder[static_cast<std::size_t>(clamped)] - current)
               < 1e-3f) {
        return false;  // 到头了
    }
    return SetZoom(ladder[static_cast<std::size_t>(clamped)]);
}

float InkingWindow::GetZoom() const noexcept { return _zoom; }


void InkingWindow::SetBackgroundColor(std::uint32_t argb) noexcept {
    _backgroundColor = argb;
}

std::uint32_t InkingWindow::GetBackgroundColor() const noexcept {
    return _backgroundColor;
}

bool InkingWindow::SetRoundedCorners(bool rounded) {
    _roundedCorners = rounded;
    // 窗口已经起来就立刻应用一次；还没起来只记账，`Show()` 建完窗口会应用。
    _roundedCornersApplied = RequestRoundedCorners(state().window, _roundedCorners);
    return _roundedCornersApplied;
}

bool InkingWindow::IsRoundedCornersApplied() const noexcept {
    return _roundedCornersApplied;
}

bool InkingWindow::IsRoundedCornersRequested() const noexcept {
    return _roundedCorners;
}

bool InkingWindow::HasWindow() const noexcept { return state().window != nullptr; }

// ---------------------------------------------------------------------------
// 截图（P2-6）
// ---------------------------------------------------------------------------

bool InkingWindow::RequestCapture(const std::string& path) {
    if (path.empty()) {
        INK_LOG_ERROR(kModuleName, "截图路径为空");
        return false;
    }
    // 没有渲染器就没有"当前帧"，排队也永远不会被执行——**当场报错**，
    // 不要留下一个"排进去了但永远不落盘"的请求（那正是最难查的一类）。
    if (state().renderer == nullptr) {
        INK_LOG_ERROR(kModuleName,
                      "没有渲染器（窗口没打开或已关闭），无法截图：" + path);
        return false;
    }
    pendingCapturePath() = path;
    return true;
}

bool InkingWindow::Capture(const std::string& path) {
    if (path.empty()) {
        INK_LOG_ERROR(kModuleName, "截图路径为空");
        return false;
    }
    if (state().renderer == nullptr) {
        INK_LOG_ERROR(kModuleName,
                      "没有渲染器（窗口没打开或已关闭），无法截图：" + path);
        return false;
    }
    return writeRendererImage(state().renderer, path);
}

bool InkingWindow::HasPendingCapture() const noexcept {
    return !pendingCapturePath().empty();
}


/**
 * 按名字把某个场景设为活跃；找不到 / 名字为空时返回 false。
 *
 * 活跃态统一交给 SceneLibrary 管：切换时旧的自动进入静默（不渲染、
 * 跟着渲染的逻辑也停），新的开始活跃。窗口自己不保存场景指针，
 * 免得在场景关闭 / 析构之后留下野指针。
 */
/**
 * 按名字把场景设为活跃。
 *
 * `withPreload = true` 时**先同步跑一次预加载**（含它的 UI 侧收尾）。
 *
 * 为什么 `Show()` 走同步、而 `SceneLibrary::PreloadScene()` 走任务队列：
 * `Show()` 是启动点，它返回之后马上就要渲染这个场景——这时候"加载还没好"
 * 没有意义，画出来是半成品。所以就地跑完（UI 线程），代价是启动卡在加载上；
 * 不想卡启动就在 `Show()` 之前先 `PreloadScene()` 让它在后台备好，那样这里
 * 的同一次调用会被状态机挡掉、什么都不做（不会加载两遍）。
 */
bool showSceneByName(const std::string& sceneName, bool withPreload) {
    if (sceneName.empty()) {
        SceneLibrary::SetActiveScene(nullptr);
        return false;
    }

    InkingScene* scene = SceneLibrary::FindScene(sceneName);
    if (scene == nullptr) {
        INK_LOG_WARN(kModuleName, "找不到场景：" + sceneName);
        return false;
    }

    // 只跑一次：手动提前预加载过的话，`PreloadNow` 会返回 false 且什么都不做。
    if (withPreload) {
        scene->PreloadNow();
    }

    SceneLibrary::SetActiveScene(scene);
    return true;
}

int InkingWindow::Show(const std::string& sceneName) {
    WindowState& s = state();

    if (s.open) {
        // 主循环跑起来之后，只有场景回调里再调 Show() 会走到这条分支——
        // 那正是运行期换场景的用法，所以这里是"切场景"而不是"忽略重复调用"。
        // 旧场景会在这里自动进入静默。
        s.sceneName = sceneName;
        showSceneByName(sceneName, /*withPreload=*/true);
        INK_LOG_INFO(kModuleName, "窗口已经打开，切换场景：" + sceneName);
        return _exitCode;
    }

    if (!Open(sceneName)) {
        return _exitCode;
    }

    // Show() 是启动点：窗口开出来之后就在这里进主循环，一直阻塞到窗口关闭
    // （或按 ESC），返回前把窗口释放掉。想自己控制帧节奏（自检 / CI）就走
    // `Open()` + `RunFrames(n)`，那条路与这里共用 `stepFrame`。
    runLoop(s, _mouseInput, _keyboardInput, *this);

    INK_LOG_INFO(kModuleName, "主循环退出，释放窗口");
    releaseWindow(s);

    // D5：`RequestClose(code)` 记下的退出码原样传出去——CI 拿的就是它。
    return _exitCode;
}

bool InkingWindow::Open(const std::string& sceneName) {
    WindowState& s = state();

    if (s.open) {
        // 已经开着：与 `Show()` 的第二条路径同义（切场景）。
        s.sceneName = sceneName;
        showSceneByName(sceneName, /*withPreload=*/true);
        return true;
    }

    // 每次进循环都复位一次：上一次跑完留下的请求不该影响下一次。
    _closeRequested = false;
    _exitCode = 0;

    // SDL3 的 SDL_Init 返回 bool：true 才是成功（SDL2 是"0 表示成功"）
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        INK_LOG_ERROR(kModuleName, std::string("SDL_Init 失败：") + SDL_GetError());
        return false;
    }

    // C2：无边框窗口。标志必须在**建窗口那一刻**给（之后改要走
    // `SDL_SetWindowBordered`，那会有一次真实的窗口重建）。默认关——
    // "某块东西莫名没有标题栏"和"某块东西莫名被挪走"是同一种难查。
    const SDL_WindowFlags windowFlags =
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
        | (_borderless ? SDL_WINDOW_BORDERLESS : 0);

    s.window = SDL_CreateWindow("InkingWindow", _windowWidth, _windowHeight,
                                windowFlags);
    if (!s.window) {
        INK_LOG_ERROR(kModuleName,
                      std::string("SDL_CreateWindow 失败：") + SDL_GetError());
        SDL_Quit();
        return false;
    }

    // 窗口位置取回来：自绘标题栏的拖动是"在当前坐标上累加"，起点必须是真的
    // 窗口位置（不然第一下拖动会把窗口拽回 (0,0)）。窗口还没建起来时这里是
    // 空操作，`_windowX/_windowY` 保持 0。
    SDL_GetWindowPosition(s.window, &_windowX, &_windowY);

    // 全屏：`SetFullscreen` 在窗口没建起来时只记账，这里补上那一次真实调用。
    // **不能**复用 `SetFullscreen`——它按"值没变就早退"挡重复，而这里正是
    // "值已经是 true、但还没落到窗口上"的那个窗口期。
    if (_fullscreen) {
        if (!SDL_SetWindowFullscreen(s.window, true)) {
            INK_LOG_WARN(kModuleName,
                         std::string("建窗后进入全屏失败：") + SDL_GetError());
        }
    }

    // 窗口圆角（Win11 的 DWM；旧系统 / 非 Windows 静默无效）——必须在窗口
    // **建起来之后**才能应用。请求与读回是两件事：`_roundedCornersApplied`
    // 存的是读回来的答案，日志照着它说，别把"发过请求"说成"已生效"。
    //
    // 级别（0.1.1）：**"请求了圆角但没落到窗口上"升 WARN**。它不是调试信息
    // ——用户配了圆角却看不到效果，属于"配置未生效"，而这条以前是 DEBUG，
    // 发布包里根本看不见（消费方正好也关着调试流）。另外两种情形仍是 DEBUG：
    // 已生效、以及本来就没请求。
    _roundedCornersApplied = RequestRoundedCorners(s.window, _roundedCorners);
    if (_roundedCorners && !_roundedCornersApplied) {
        INK_LOG_WARN(kModuleName,
                     "窗口圆角：系统不支持或忽略了它（退回方角）");
    } else {
        INK_LOG_DEBUG(kModuleName,
                      std::string("窗口圆角：")
                          + (_roundedCornersApplied
                                 ? "已生效（DWM 属性读回为圆角）"
                                 : "已关闭（DwmcpDoNotRound）"));
    }

    // 打开文本输入（A3）：**不调这一句就收不到 `SDL_EVENT_TEXT_INPUT`**，
    // 于是 `onTextInput` 永远不触发——而按键事件照样来，症状是"英文能打、
    // 中文输入法没反应"，很容易往输入法上怀疑。
    // 放在这里而不是"等哪个组件要输入时再开"：开着没有代价（不发事件而已），
    // 而"忘了开"是静默失败。
    if (!SDL_StartTextInput(s.window)) {
        INK_LOG_WARN(kModuleName,
                     std::string("SDL_StartTextInput 失败，文本输入（含输入法）"
                                 "不会工作：") + SDL_GetError());
    }

    // 渲染器建不起来（无 GPU / 无显示 / 驱动缺失）时只降级、不退出：
    // 窗口和主循环照旧跑，只是这一帧画不出东西——渲染树本身仍然每帧更新，
    // 自检和坐标换算都还能验。以前这里是"渲染器失败就整个放弃"，
    // 在无头环境（CI、冒烟测试）下直接把窗口也毙了。
    s.renderer = SDL_CreateRenderer(s.window, nullptr);
    s.rendererReady = (s.renderer != nullptr);
    if (!s.rendererReady) {
        INK_LOG_WARN(kModuleName,
                     std::string("SDL_CreateRenderer 失败，降级为无渲染：")
                         + SDL_GetError());
    } else {
        // 设计尺寸只在这里用一次：它定义的是逻辑坐标系，
        // letterbox 保证任何窗口比例下 UI 都不变形，多余空间留黑边。
        SDL_SetRenderLogicalPresentation(
            s.renderer,
            inking::DesignWidth(),
            inking::DesignHeight(),
            SDL_LOGICAL_PRESENTATION_LETTERBOX);

        // -------------------------------------------------------------------
        // 打开绘制的 alpha 混合。
        //
        // SDL 的**绘制**混合模式默认是 `SDL_BLENDMODE_NONE`（和纹理的
        // `SDL_SetTextureBlendMode` 是两套东西）：那时 `SDL_SetRenderDrawColor`
        // 的 alpha 会被**原样写进目标**，不参与合成。后果是"半透明"根本不存在——
        // 按钮 hover 把 0.75 改成 0.85，画出来只是把底色整块换成另一个不透明的
        // 黑，眼睛看不出任何变化（实测：BLENDMODE_NONE 下按钮里读出 0xBF000000、
        // 打开之后读出 0xFF040406，后者才是"0.75 的黑叠在底色上"）。
        //
        // 为什么在这里设一次而不是每个组件各设一次：混合是**渲染器级的开关**，
        // 每个组件都调一遍纯属重复；而且漏一个就会出现"界面上有的半透明生效、
        // 有的不生效"——那种不一致比整块不透明难查得多。
        //
        // 副作用（预期内）：所有画出来的颜色从此都按 alpha 合成，
        // `AnchorData::color` 里的 alpha 开始真的有意义。
        // -------------------------------------------------------------------
        SDL_SetRenderDrawBlendMode(s.renderer, SDL_BLENDMODE_BLEND);

        // vsync：接上之后 SDL_RenderPresent 会阻塞到下一次垂直回扫，
        // 主循环就不用自己 sleep 了——固定 sleep 不扣本帧耗时，
        // 实际跑出来是 16ms + 工作时间，帧率会低于 60 且抖动。
        // 无头 / 无显示环境下可能接不上，那时退回 sleep（见 runLoop）。
        s.vsyncActive = SDL_SetRenderVSync(s.renderer, 1);
        if (s.vsyncActive) {
            INK_LOG_DEBUG(kModuleName, "vsync 已接上，节流交给 SDL");
        } else {
            INK_LOG_WARN(kModuleName,
                         std::string("vsync 没接上，退回固定节流：")
                             + SDL_GetError());
        }
    }

    // 展示倍率：设计单位 → 设备像素。窗口层写一次，之后每帧跟着窗口尺寸刷。
    SetMagnification(letterboxMagnification(s.window));

    s.open = true;
    s.sceneName = sceneName;

    // 场景的活跃态在这里落地：把指定场景设为"正在渲染"的那一个，**并先同步
    // 跑完它的预加载**——`Show()` 返回之后马上就要画它，"加载还没好"没意义。
    // 名字找不到（还没构造 / 已被关闭）时只记名字，画面上就没有场景。
    showSceneByName(sceneName, /*withPreload=*/true);

    INK_LOG_PASS(kModuleName,
                 "窗口已打开 " + std::to_string(_windowWidth) + "x"
                     + std::to_string(_windowHeight)
                     + "，设计尺寸 " + std::to_string(inking::DesignWidth()) + "x"
                     + std::to_string(inking::DesignHeight())
                     + "，展示倍率 " + std::to_string(GetMagnification())
                     + "，场景：" + sceneName);

    // **不进主循环**：这是 `Open()` 与 `Show()` 的唯一区别。帧时钟在这里复位，
    // 于是"Open → RunFrames(2)"与"Show 跑两帧"的 delta 起点一致。
    s.pumpReady = false;
    s.frameTasks.clear();
    return true;
}

bool InkingWindow::PumpOnce() {
    WindowState& s = state();

    if (!s.open) {
        // 还没开：先开一个（场景为空 = 沿用当前活跃场景）。
        // 自检因此不用先写一遍窗口代码，`PumpOnce()` 自己就够了。
        if (!Open("")) {
            return false;
        }
    }

    const bool running = stepFrame(s, _mouseInput, _keyboardInput, *this);
    if (!running) {
        // 循环结束了：和 `Show()` 的收尾一致，否则下一次 Open 会拿到残留状态。
        releaseWindow(s);
    }
    return running;
}

int InkingWindow::RunFrames(int frameCount) {
    int ran = 0;
    for (int i = 0; i < frameCount; ++i) {
        if (!PumpOnce()) {
            break;
        }
        ++ran;
    }
    return ran;
}

void InkingWindow::PostFrameTask(std::function<void()> task) {
    if (!task) {
        return;
    }
    state().frameTasks.push_back({1, std::move(task)});
}

void InkingWindow::RunAfterFrames(int frames, std::function<void()> task) {
    if (!task) {
        return;
    }
    // `frames <= 0` 与 `PostFrameTask` 同义（都是"这一帧末"）。
    const int wait = frames > 0 ? frames : 1;
    state().frameTasks.push_back({wait, std::move(task)});
}

void InkingWindow::SetFixedDelta(float seconds) {
    _fixedDelta = seconds > 0.0f ? seconds : 0.0f;
}

float InkingWindow::GetFixedDelta() const noexcept { return _fixedDelta; }

bool InkingWindow::RenderToTexture(const std::string& path, int width, int height,
                                   const std::string& sceneName) {
    if (path.empty() || width <= 0 || height <= 0) {
        INK_LOG_ERROR(kModuleName,
                      "RenderToTexture：参数非法（路径空 / 宽高非正）：" + path);
        return false;
    }

    // ① 目标位图 + 软件渲染器。**不需要窗口，也不需要 dummy 视频驱动**
    //    ——那条依赖正是旧写法"换个机器就可能挂"的来源。
    SDL_Surface* target =
        SDL_CreateSurface(width, height, SDL_PIXELFORMAT_ARGB8888);
    if (target == nullptr) {
        INK_LOG_ERROR(kModuleName,
                      "RenderToTexture：建目标位图失败：" + std::string(SDL_GetError()));
        return false;
    }
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(target);
    if (renderer == nullptr) {
        INK_LOG_ERROR(kModuleName,
                      "RenderToTexture：建软渲染器失败：" + std::string(SDL_GetError()));
        SDL_DestroySurface(target);
        return false;
    }

    // ② 与真窗口同一套呈现规则：设计坐标 → letterbox。文本层的"按设备比例
    //    烘字"也走这条逻辑尺寸（`render/` 自己问渲染器），所以字号不会忽大忽小。
    SDL_SetRenderLogicalPresentation(renderer, inking::DesignWidth(),
                                     inking::DesignHeight(),
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    // ③ 清屏（和 drawFrame 的底色同一条规矩）。
    const std::uint32_t bg = Instance().GetBackgroundColor();
    SDL_SetRenderDrawColor(renderer, static_cast<Uint8>((bg >> 16) & 0xFFu),
                           static_cast<Uint8>((bg >> 8) & 0xFFu),
                           static_cast<Uint8>(bg & 0xFFu),
                           static_cast<Uint8>((bg >> 24) & 0xFFu));
    SDL_RenderClear(renderer);

    // ④ 临时切场景（空 = 用当前活跃那个），画完还原——这条函数是**只读**
    //    语义，不该把宿主的活跃场景改掉。
    InkingScene* previous = SceneLibrary::GetActiveScene();
    InkingScene* wanted =
        sceneName.empty() ? previous : SceneLibrary::FindScene(sceneName);
    if (wanted != nullptr) {
        SceneLibrary::SetActiveScene(wanted);
    }
    const bool rendered = SceneLibrary::RenderScene(renderer);
    if (wanted != nullptr && wanted != previous) {
        SceneLibrary::SetActiveScene(previous);
    }

    // ⑤ 落盘用与截图同一条路（PNG 走 SDL_SavePNG，其它走 BMP）。
    const bool saved = rendered && writeRendererImage(renderer, path);

    SDL_DestroyRenderer(renderer);  // 会一并处理它自己的纹理
    SDL_DestroySurface(target);
    return saved;
}

}  // namespace inking

