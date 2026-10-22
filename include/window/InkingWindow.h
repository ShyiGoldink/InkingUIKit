#pragma once

#include <config/window_config.h>
#include <widget/InkingStaticWidget.h>
#include <input/KeyboardInput.h>
#include <input/MouseInput.h>
#include <input/ShortcutRegistry.h>
#include <event/InkingEvent.h>
#include <functional>
#include <string>
#include <vector>

namespace inking {

// 只做前向声明：公开头里不拖场景实现进来。
class InkingScene;

/**
 * 窗口：整个项目的根锚点，所以它继承**静态锚点**。
 *
 * 两套尺寸要分清：
 *   - 基类那对（GetWidth / GetHeight）是"根锚点的尺寸"，也就是设计画布
 *     大小（运行期由 `CXXCSS/config.json` 决定，默认 1280×720；
 *     见 `config/window_config.h`），窗口构造那一刻定型、之后不动；
 *   - 自己那对（GetWindowWidth / GetWindowHeight）是**窗口像素尺寸**，
 *     运行期可调。
 *
 * 窗口怎么拉都不改设计坐标里的几何：设计空间固定，靠 letterbox 做整幅
 * 等比换算，换算倍率由窗口层通过基类的 SetMagnification 写进来（不标脏）。
 *
 * 注意窗口**不是**渲染树的根——渲染树以场景为根（docs/API.md）。窗口只负责
 * 开出来、把事件泵进 MouseInput、每帧把展示倍率刷一遍，然后把当前场景的
 * 绘制列表交给渲染器。
 */
class InkingWindow : public InkingStaticWidget {
public:
    static InkingWindow& Instance();

    InkingWindow(const InkingWindow&) = delete;
    InkingWindow& operator=(const InkingWindow&) = delete;

    /// 窗口像素尺寸（运行期属性）。注意与基类的 GetWidth / GetHeight 区分。
    int GetWindowWidth() const noexcept;
    int GetWindowHeight() const noexcept;
    bool setWindowWidth(int width);
    bool setWindowHeight(int height);

    /**
     * 当前正在展示（渲染）的场景；没有时为 nullptr。
     *
     * 活跃态由 `SceneLibrary` 统一维护，窗口**不缓存**场景指针——
     * 缓存了就得在场景关闭 / 析构时回头清掉，而现查一次更省心，
     * 也不会留下指向已销毁场景的野指针。
     */
    InkingScene* GetCurrentScene() const;

    /**
     * @brief 跑主循环（**阻塞**）到窗口关闭，返回**进程退出码**（D5）。
     *
     * 以前返回 void，自检只能自己数失败项再 `return`；现在 `RequestClose(code)`
     * 记下的那个码会原样传出来，`return inking::InkingWindow::Instance().Show();`
     * 就是 CI 要的那条信号。
     *
     * 需要"跑几帧就回来"的非阻塞写法见 `PumpOnce()` / `RunFrames(n)`（D1）。
     */
    int Show(const std::string& sceneName = "");

    // ---------------- 非阻塞跑帧与确定性时间（D1 / D3 / D4） ----------------
    //
    // 这一组是"自检从跑一次看截图，升级到 CI 里跑一百个断言"的前提：
    //
    //   Open("scene");              // 建窗口 + 预加载，**不进循环**
    //   RunFrames(2);               // 跑两帧（事件泵 / 派发 / 渲染全走一遍）
    //   scene->Activate("ok");      // 语义点击，不碰指针
    //   RunAfterFrames(1, [&]{ ... });
    //   RequestClose(failures);     // 退出码进 CI
    //
    // 与 `Show()` 的关系：`Show()` 就是 `Open()` + `while (PumpOnce())` +
    // 释放窗口。两条路共用 `stepFrame`，所以"手动跑一帧"与"主循环里的一帧"
    // **顺序完全一致**——不存在"自检过了、真机不过"这种分叉。

    /**
     * @brief 建窗口并准备好渲染，**不进主循环**（D1）。
     *
     * 已经打开时按"切场景"处理（与 `Show()` 第二条路径同义）。
     *
     * @return 真的打开了才返回 true；SDL 初始化 / 建窗口失败返回 false
     *         （错误已记日志，不会抛）。
     */
    bool Open(const std::string& sceneName = "");

    /**
     * @brief 跑**一帧**就返回（D1）。
     *
     * 窗口还没打开时先自动 `Open("")`——无头自检因此可以"一句都不写窗口代码"
     * 就开始跑帧。
     *
     * @return 这一帧之后还能继续跑吗。收到关窗请求 / 冒烟时间到 → false。
     */
    bool PumpOnce();

    /**
     * @brief 连跑 `frameCount` 帧（D1），中途收到关窗请求就提前停。
     * @return 实际跑了多少帧。
     */
    int RunFrames(int frameCount);

    /**
     * @brief 排一个**帧末任务**：这一帧收尾之后执行一次（D3）。
     *
     * "帧末"的准确位置是：场景脏消费之后、待销毁场景提交之前。所以任务里读到
     * 的命中 / 尺寸 / 动画进度都是**这一帧定稿的**值，而场景还活着。
     *
     * 回调抛异常会一路逃出主循环（那是长驻线程的入口，SKILL.md §7 的禁令）——
     * 这里不多做一层兜底：任务是你自己写的同步代码，异常该在任务里处理。
     */
    void PostFrameTask(std::function<void()> task);

    /**
     * @brief 等 `frames` 帧之后执行一次（D3）：`frames <= 0` 等价于
     *        "这一帧末就执行"（与 `PostFrameTask` 同义）。
     *
     * 典型用法："窗口出现之后再断言"——`RunFrames(1)` 保证第一帧真的画过。
     */
    void RunAfterFrames(int frames, std::function<void()> task);

    /**
     * @brief 固定每帧的 delta（D4）：动画 / 主题过渡的截图在 CI 里才稳定。
     *
     * 传 `0`（默认）恢复"按墙上时钟算"。**固定 delta 下不节流**——否则跑
     * 100 帧要等 1.6 秒，CI 的时间全花在 sleep 上。
     *
     * 它只影响**喂给时间线的 delta**，不影响 `SDL_GetTicks` 之类的外部时间源。
     */
    void SetFixedDelta(float seconds);

    /// 当前固定 delta（0 = 用真实流逝时间）。
    float GetFixedDelta() const noexcept;

    /**
     * @brief **离屏渲染一张图**（D2）：不建窗口、不需要 dummy 视频驱动。
     *
     * 做法是一条最短路径：`SDL_CreateSurface` 出目标位图 → `SDL_CreateSoftwareRenderer`
     * 出一个软渲染器 → 走**与真窗口同一条** `SceneLibrary::RenderScene` →
     * 存成图片。所以它验的是真实提交路径（形状 / 文字 / 裁剪 / 装饰笔全在内），
     * 不是"另画一份"。
     *
     * 逻辑呈现按**设计尺寸**做 letterbox：`width × height` 与设计比不同时，
     * 多出来的地方是黑边——和真窗口一致，截图才可比。
     *
     * @param path       输出路径；`.png` 需要 SDL ≥ 3.4，其它扩展名写 BMP。
     * @param width      输出像素宽。
     * @param height     输出像素高。
     * @param sceneName  画哪个场景；空 = 当前活跃场景。
     *
     * @return 真的写出了文件才返回 true（失败已记 ERROR 日志）。
     */
    static bool RenderToTexture(const std::string& path, int width, int height,
                                const std::string& sceneName = "");

    // ---------------- 无边框窗口（C2：自绘标题栏 / 菜单栏） ----------------
    //
    // **为什么这个开关落在窗口层**：`SDL_CreateWindow` 的窗口标志只有窗口层知道，
    // 组件层连 SDL 都不认识（`AGENTS.md` §6 第 8 条）。所以"不要系统标题栏"是
    // 一条窗口级能力，和"展示倍率由窗口层写"同源：谁离 SDL 近，谁提供这个词汇。
    //
    // 它**只管窗口装饰**：设计坐标 / letterbox / 命中换算一个字都不变——
    // 无边框只是把"窗口从哪里开始算内容"交给系统之外的人，窗口像素尺寸没变，
    // 于是 `letterboxMagnification` 与"窗口坐标 → 设计坐标"照旧。
    //
    // 三条边界（第一版有意不做）：
    //   - **不做无边框下的自绘改尺寸**（要自绘边缘把手 + 命中 + 最小尺寸约束）：
    //     改尺寸仍走 `setWindowWidth` / `setWindowHeight`；
    //   - 不做系统级吸附 / 双击最大化；
    //   - 拖动是**按屏幕像素**累加的记账（`MoveWindowBy`），DPI 缩放下的
    //     "跟手程度"留给人眼判（r5）。

    /**
     * 无边框窗口：不要系统标题栏与边框，由自绘标题栏接管（默认 **false**）。
     *
     * 窗口已经开出来时立刻改（`SDL_SetWindowBordered`）；还没开就记下来，
     * `Show()` 建窗口时带上 `SDL_WINDOW_BORDERLESS`。
     * @return 真的改了才返回 true（幂等）。
     */
    bool SetBorderless(bool borderless);

    /// 现在是无边框吗。
    bool IsBorderless() const noexcept;

    // ---------------- 透明窗口 ----------------
    //
    // **为什么需要它**：`SetBackgroundColor` 收的是 `0xAARRGGBB`，最高字节看起来
    // 像"背景的透明度"，但不透明窗口上那一字节写进去之后没有任何人采信——桌面
    // 合成器按不透明窗口处理，`SDL_RenderClear` 也不做混合（直接写目标）。
    // 于是"把 alpha 配成 0 就能透"是一条**看起来支持、其实无效**的通道。
    //
    // 真透明只能靠建窗标志：SDL3 的 `SDL_WINDOW_TRANSPARENT` 在
    // `SDL_CreateWindow` 那一刻决定，SDK 没有"事后变透明"的 API
    // （`SDL_SetWindowOpacity` 是整窗不透明度，不是逐像素 alpha）。
    //
    // 三条边界，都是**实测过的限制**，不是没做：
    //   - **只能在建窗前设**：窗口已经开出来时调用返回 false + 一条 WARN，
    //     而不是悄悄记下来等下次——"下次"永远不会来；
    //   - **后端不保证**：SDL 只在 Windows 的 D3D11 后端补了透明 swapchain，
    //     D3D12 明确不支持、Vulkan 多数环境不行，SDL 源码自己注了混合 GPU 下
    //     "not work on all hardware configurations"。所以"标志落地"不等于
    //     "真的透"，后者读不回来；
    //   - **半透明元素的色值要真机校**：DWM 拿窗口 alpha 合成那一路按
    //     premultiplied 解释，而组件层画的是 straight alpha。全不透明的内容
    //     不受影响；带 alpha 的描边 / 阴影会**偏亮 / 过饱和**（2026-10-22 实测：
    //     `SDL_RenderClear(255,0,0,128)` 读回 `255,0,0,128`，RGB 没有预乘）。

    /**
     * 透明窗口：清屏色的 alpha 真的会透出桌面（默认 **false**）。
     *
     * **必须在 `Show()` / `Open()` 之前**。窗口已经开出来时返回 false 并打一条
     * WARN（SDL 没有事后切换透明的入口）。
     *
     * @return 记下了（值真的变了）为 true；窗口已打开或值没变则为 false。
     */
    bool SetTransparent(bool transparent);

    /**
     * **请求值**：我们让窗口透明，与"合成器是否照做"分开。
     *
     * `window/WindowConfig.h` 的回读校验比的是这一个——透明到底透没透读不回来，
     * 拿 `IsTransparentApplied()` 去比会把"后端不支持"误报成"配置没生效"。
     */
    bool IsTransparentRequested() const noexcept;

    /**
     * 建窗时 `SDL_WINDOW_TRANSPARENT` **真的进了窗口标志**吗。
     *
     * 它证明的是"标志落地"，**不是"桌面真的透出来了"**——后者没有任何回读口，
     * 只能真机目视 / 截图读 alpha 两步验收。没有窗口时恒为 false。
     */
    bool IsTransparentApplied() const noexcept;

    /// 窗口左上角的**屏幕坐标**（无边框下自绘标题栏拖动时的观测口）。
    int GetWindowX() const noexcept;
    int GetWindowY() const noexcept;

    /**
     * 把窗口按屏幕像素挪一段（自绘标题栏的拖动落点）。
     *
     * 记账 + 同步：位移先累加到窗口自己的坐标上（所以**没有真实窗口时也能断言**
     * 位置变化量），窗口若已开出来就顺手 `SDL_SetWindowPosition`。
     * 传 (0, 0) 是空操作。
     */
    void MoveWindowBy(int dx, int dy);

    /// 最小化（无边框下的自绘按钮用）。没有窗口时是空操作。
    void MinimizeWindow();

    /**
     * 请求关闭主循环。
     *
     * 不能直接 `SDL_PushEvent(QUIT)`：那条路要求窗口层活着、而且绕开了
     * "谁在请求"这件事（自检也断言不了）。这里只置一个标志，
     * 主循环每帧查一次——和 ESC 退出同一个落点，且**可断言**
     * （`IsCloseRequested()`）。
     *
     * **会先过退出闸门**（`SetExitConfirmHandler`）：装了确认回调而它返回
     * false 时，这次请求被否决、标志不置位。自绘标题栏的关闭按钮、业务代码、
     * ESC 全走这一条，所以"要不要问一句再退"只需要装一次回调。
     */
    void RequestClose();

    /**
     * 请求关闭，并**指定进程退出码**（D5）。
     *
     * 与无参版同一条路（同一个退出闸门、同一个 `IsCloseRequested()` 落点），
     * 区别只是记下"退出时返回几"。自检插件 / CI 里"报了失败还要让进程以非 0
     * 退出"就靠它——以前只能让宿主自己算，插件拿不到这个口子。
     *
     * @param exitCode `Show()` 返回它；`0` 表示正常。
     */
    void RequestClose(int exitCode);

    /**
     * 这次运行要返回给操作系统的退出码（默认 0）。
     *
     * 一旦有 `RequestClose(code)` 记过，之后 `RequestClose()`（无参）不会把它
     * 清回 0——"已经报过失败"这件事不该被后来的关窗动作抹掉。
     */
    int GetExitCode() const noexcept;

    /// 有人请求过关窗吗（主循环每帧查一次）。自检观测口。
    bool IsCloseRequested() const noexcept;

    // ---------------- 退出闸门（"要不要问一句再退"） ----------------
    //
    // 为什么是回调而不是虚函数：本类是**私有构造的单例**，外面派生不出实例，
    // 所以"可重写"在这里唯一落得下的形态就是"可替换的回调"。
    //
    // 调用时机是**请求退出的那一刻**（同步）：返回值 false = 先别退。
    // 要弹自绘的确认框（异步）就让回调返回 false、自己记下"待确认"状态，
    // 用户点"确定"之后再调一次 `RequestExit()`。

    /// 退出确认：返回 true 才真的退出（默认没装 = 直接退）。
    using ExitConfirmHandler = std::function<bool()>;

    /// 装 / 清（传空 = 清掉）退出确认回调。
    void SetExitConfirmHandler(ExitConfirmHandler handler);

    /**
     * 请求退出程序：过闸门 → 置 `IsCloseRequested()`。
     *
     * ESC、标题栏关闭按钮、业务按钮都该走这条，而不是自己 `exit()`——
     * 那样主循环来不及收尾（场景帧末析构、纹理释放都会跳过）。
     */
    void RequestExit();

    // ---------------- 全屏 ----------------

    /**
     * 全屏开关（幂等）。窗口还没建起来时只记账，`Show()` 建完窗口补应用一次。
     *
     * 全屏状态下**缩放是空操作**（尺寸由系统给）；退出全屏后窗口回到
     * `GetZoom()` 对应的尺寸。
     *
     * @return 真的改了才返回 true；并且会 `fullscreenChanged.Emit(新值)`。
     */
    bool SetFullscreen(bool fullscreen);

    bool IsFullscreen() const noexcept;

    /// 在两者之间切（F11 / 菜单项用）。
    void ToggleFullscreen();

    /**
     * 全屏状态**真的**变了（自己切的，或被系统 / 别的途径切的）。
     *
     * 为什么要有事件：菜单栏要在全屏时自动隐藏（C 需求）。让它每帧去轮询
     * `IsFullscreen()` 也能实现，但"在 `onRender` 里改可见性"是提交期改结构，
     * 正是本仓库反复避开的悬垂来源（见 `InkingScene` 帧末析构那段）。
     */
    InkingEvent<bool> fullscreenChanged;

    // ---------------- 缩放（无边框窗口的代偿） ----------------
    //
    // 为什么缩放是"改窗口尺寸"而不是"改内容的倍率"：letterbox 下黑边的唯一
    // 来源是"窗口像素比 ≠ 设计像素比"。要让黑边消失，只能让窗口比例等于设计
    // 比例——那就是等比改窗口尺寸。改内容倍率只会把黑边换成裁剪或拉伸。
    //
    // 档位（`GetZoomLadder`）是**运行时生成**的：设计尺寸由 `CXXCSS/config.json`
    // 决定（运行期落在 `config/window_config.h` 的 `DesignWidth()` 上），所以
    // "能不能到 2 倍"取决于**当前显示器**装不装得下（设计尺寸 × 2）。
    // 写死一张表在换设计尺寸 / 换小屏的那天就会错。

    /// 最小缩放（再小就看不清了，且窗口会有 0 尺寸风险）。
    static constexpr float kMinZoom = 0.25f;

    /// 最大缩放的上限（具体能不能到由显示器可用区域决定，见 `GetZoomLadder`）。
    static constexpr float kMaxZoom = 4.0f;

    /**
     * 缩放档位表：**当前这台机器**上可用的档位（升序）。
     *
     * 生成规则：候选比值里挑出 `设计尺寸 × zoom` 能装进"显示器可用区域"的那些。
     * 有窗口时跟着**窗口所在**的显示器（多屏下才正确）；没有窗口时用主显示器；
     * 视频子系统还没初始化（`Show()` 之前、或纯逻辑环境）则不按屏幕夹，只夹在
     * `[kMinZoom, kMaxZoom]` —— 这样 `Show()` 之前也能规划尺寸。
     */
    std::vector<float> GetZoomLadder() const;

    /**
     * 设一个缩放（会**吸附到最近的档位**）。@return 真的变了才 true。
     *
     * 为什么吸附而不是接受任意倍数：档位之外的尺寸乘出来常常不是整数像素，
     * 两轴各取整一次比例就偏了——黑边会回来（1px 量级，肉眼几乎看不出，
     * 但"严格等比"这句话就破了）。要精确尺寸请直接用
     * `setWindowWidth/setWindowHeight`，那是另一条语义（不承诺比例）。
     */
    bool SetZoom(float zoom);

    /// 按档位前进 / 后退（滚轮一格 = `steps = ±1`）。到头了返回 false。
    bool ZoomBy(int steps);

    /// 当前缩放（1.0 = 窗口逻辑尺寸等于设计尺寸）。窗口被外部改过尺寸时它仍是
    /// "上一次设置的值"；`ZoomBy` 会以**实际尺寸**反推档位，不会跳回历史值。
    float GetZoom() const noexcept;


    // ---------------- 清屏色（窗口底色） ----------------

    /**
     * 窗口清屏色（`0xAARRGGBB`），默认 `0xFF121218`（原来的深灰）。
     *
     * 为什么这件事归窗口层：那一笔是"窗口自己的底"，发生在场景渲染之前
     * （`drawFrame` 里的 `SDL_RenderClear`），组件一个字都不知道。
     * 亮色场景（浅底 + 白卡）必须把它一起改掉，否则白卡浮在深底上会很怪。
     *
     * **最高字节（alpha）只在透明窗口下有意义**：不透明窗口上 `SDL_RenderClear`
     * 直接把颜色写进帧缓冲、不参与混合，帧缓冲的 alpha 又没人采信 —— 写 0 也
     * 不会透出桌面。那种情况会由 `stepFrame` 打一条 WARN（见 `SetTransparent`）。
     */
    void SetBackgroundColor(std::uint32_t argb) noexcept;

    std::uint32_t GetBackgroundColor() const noexcept;

    // ---------------- 窗口圆角（DWM） ----------------

    /**
     * 请求窗口四角圆角（Windows 11 的 DWM）；其它系统 / 旧版本上静默无效。
     *
     * 为什么走系统这条路而不是自己画：DWM 画的是**真圆角**（阴影、命中区域一起
     * 处理），自己画要开透明窗口 + 逐像素 alpha，代价大得多。旧系统上动态加载
     * 失败就退回方角，所以**不需要版本分支**。
     *
     * 默认**开**：Win11 上带边框窗口本来就是圆角，显式要求一次只是把偏好钉住；
     * 无边框窗口（C2）则要靠它才可能有圆角。
     *
     * 返回值是"系统接受了没有"——**真正的圆角由 DWM 画**，所以还提供了
     * `IsRoundedCornersApplied()`（把属性**读回**来的答案）。窗口还没建起来时
     * 只记账，`Show()` 建完窗口会应用一次。
     */
    bool SetRoundedCorners(bool rounded);

    /// 读回的答案：系统是不是真的按圆角处理（Win10 / 非 Windows 恒 false）。
    bool IsRoundedCornersApplied() const noexcept;

    /**
     * **请求值**（我们让系统做什么），与"系统是否照做"分开。
     *
     * 为什么两个都要：`IsRoundedCornersApplied` 在 Win10 上恒为 false，那是
     * **系统不支持**，不是"配置没应用"。`window/WindowConfig.h` 的回读校验比的
     * 是这一个——拿 `Applied` 去比会把"系统不支持"误报成"配置没生效"。
     */
    bool IsRoundedCornersRequested() const noexcept;

    /// 真窗口建起来了没有（`Show()` 之后为 true、`Release()` 之后为 false）。
    /// 自检用它区分"跑在真窗口上"与"只有逻辑、没有窗口"两种场合。
    bool HasWindow() const noexcept;

    // ---------------- 截图（P2-6） ----------------
    //
    // **为什么落在窗口层**：只有它知道渲染器在哪（`SDL_Renderer` 在 `Show()` 里
    // 建、随窗口销毁），也只有它知道"这一帧画完了没有"。业务侧要截图不该自己
    // 去绕 `ImmediateDraw` 抓渲染器，更不该手写 BMP 编码。
    //
    // 两种调用姿势，差别只有一个：**什么时候读像素**。
    //   - `RequestCapture()`（推荐）：排队到本帧末、`Present` **之前**执行。
    //     按钮回调、快捷键处理、逻辑步里随便调都安全——那些时刻后缓冲里
    //     是上一帧甚至未定义的内容；
    //   - `Capture()`：立即读**当前**渲染目标。只有"一帧画完、还没 Present"
    //     的调用点才有意义（自检 / 工具链）。`Present` 之后读到的内容未定义。
    //
    // 格式按扩展名定：`.png` 走 `SDL_SavePNG`（要求 SDL ≥ 3.4，本仓库当前是
    // 3.4.16）；其它（含省略）走 `SDL_SaveBMP`。**不引 SDL3_image**——截图不该
    // 给一个可选依赖新增必需项。
    //
    // 截的是**整个窗口帧缓冲**（含 letterbox 黑边），不是逻辑画布那块；
    // 也不含指针小方块（那一笔画在截图之后）。

    /// 把**下一帧**存成图片（本帧末、`Present` 前）。返回"已排队"。
    bool RequestCapture(const std::string& path);

    /// 立即把当前渲染目标存成图片。返回是否真的写成功。
    bool Capture(const std::string& path);

    /// 有没有排着队的截图请求（观测口；自检与工具用来判断这一帧会不会落盘）。
    bool HasPendingCapture() const noexcept;

    // ---------------- 全局快捷键 ----------------
    //
    // 表住在窗口层：场景的键盘派发是"给焦点"，天生不全局；而快捷键要求
    // "不管焦点在哪都响应"。详见 `input/ShortcutRegistry.h` 顶上那段。
    //
    // 注册者（例如 `prefab::MenuBar`）**必须**在析构时 `Unbind`——
    // 这张表比它们活得久，挂着已死的 `this` 就是悬垂。

    ShortcutRegistry& GetShortcuts() noexcept { return _shortcuts; }
    const ShortcutRegistry& GetShortcuts() const noexcept { return _shortcuts; }

private:
    InkingWindow();
    ~InkingWindow();

    /// 把缩放落到窗口上（尺寸、位置、记账三件事）。`SetZoom` 与 `ZoomBy` 的落点。
    void applyZoom(float zoom);

    /// 当前"看起来是几倍"：有窗口就按真实逻辑尺寸反推，没有就用记录值。
    float effectiveZoom() const noexcept;

    /// 窗口所在显示器能装下的最大缩放（拿不到显示器信息时返回 `kMaxZoom`）。
    float maxUsableZoom() const noexcept;


    int _windowWidth  = inking::DesignWidth();
    int _windowHeight = inking::DesignHeight();

    /// 无边框开关（C2）。`Show()` 之前设置就是"建窗口时带上标志"。
    bool _borderless = false;

    /// 透明窗口的**请求值**：`Show()` 之前设置就是"建窗口时带上标志"。
    bool _transparent = false;

    /// 建窗后从 `SDL_GetWindowFlags` 读回的答案（标志有没有真的进窗口）。
    bool _transparentApplied = false;

    /// 窗口左上角的屏幕坐标（自绘标题栏拖动时累加；`Show()` 时从 SDL 读一次）。
    int _windowX = 0;
    int _windowY = 0;

    /// 关窗请求（无边框下的自绘关闭按钮 / 业务代码都可以置它）。
    bool _closeRequested = false;

    /// 退出码（D5）：`RequestClose(code)` 记下，`Show()` 原样返回。0 = 正常。
    int _exitCode = 0;

    /**
     * 固定帧 delta（D4）：> 0 时用它代替真实流逝时间，并且**关掉节流**。
     * 0（默认）= 按墙上时钟算，节流照旧。
     */
    float _fixedDelta = 0.0f;

    /// 窗口清屏色（`0xAARRGGBB`）。默认是原来的深灰，亮色场景自己设。
    /// alpha 只在 `_transparent` 为真时才有意义（否则 `stepFrame` 会提一条 WARN）。
    std::uint32_t _backgroundColor = 0xFF121218u;

    /// 窗口圆角：请求值（默认开）+ 系统读回的那份答案。
    bool _roundedCorners = true;
    bool _roundedCornersApplied = false;

    /// 全屏状态（构造后为 false；`SetFullscreen` / SDL 事件两条路都会写它）。
    bool _fullscreen = false;

    /// 缩放档位（1.0 = 窗口逻辑尺寸等于设计尺寸）。窗口不存在时也记账。
    float _zoom = 1.0f;

    /// 退出确认（空 = 直接退）。
    ExitConfirmHandler _exitConfirm{};

    /// 全进程的快捷键表（窗口是它的唯一持有者，见 §全局快捷键）。
    ShortcutRegistry _shortcuts{};

    MouseInput _mouseInput;

    /// 键盘输入（A3）：与 `_mouseInput` 对称——窗口层负责"收事件、攒状态"，
    /// 谁收到（焦点归属）与什么时候收到（每帧一次）都由场景层决定。
    KeyboardInput _keyboardInput;
};

/**
 * 上一帧真的渲染了哪个场景；没有场景时为 nullptr。
 *
 * 纯自检用的观察点：`Show()` 返回时窗口已经释放、场景名也被清掉，
 * 所以"窗口确实按名字解析到了场景并把它渲染出来了"只能靠这个记录来验。
 * 窗口从没跑过主循环时也是 nullptr。
 */
InkingScene* GetLastRenderedScene() noexcept;

}  // namespace inking
