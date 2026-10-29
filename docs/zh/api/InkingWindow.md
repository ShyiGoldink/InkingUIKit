# 窗口与主循环

**回到API目录：**
[API index](../API.md)

`InkingWindow` 是整个项目的**根锚点**，所以它继承静态锚点；同时它是**私有构造的单例**
（`Instance()`），负责开窗口、泵事件、每帧刷展示倍率、把当前场景的绘制列表交给渲染器。

**窗口不是渲染树的根**——渲染树以场景为根。窗口只把"外面"（SDL、系统 DPI、键盘鼠标）
接进来，把"里面"（场景）画出去。

`#include <window/InkingWindow.h>`；设计尺寸常量在 `<config/window_config.h>`。

## 两套尺寸，别混

| | 谁 | 什么时候变 |
| --- | --- | --- |
| `GetWidth()` / `GetHeight()`（继承自基类） | **设计画布**大小（`kDesignWidth × kDesignHeight`） | 编译期定型，构造后不动 |
| `GetWindowWidth()` / `GetWindowHeight()` | **窗口像素**尺寸 | 运行期可调（`setWindowWidth` / 缩放 / 全屏） |

窗口怎么拉都不改设计坐标里的几何：设计空间固定，靠 letterbox 做整幅等比换算，
换算倍率由窗口层通过基类的 `SetMagnification` 写进来（**不标脏**）。

## 主循环的固定顺序（改之前先读这一节）

这张图现在是 `stepFrame()` 的**函数体**（0.5.0 D1 把它从 `runLoop` 里拆出来了）：
`Show()` 是 `while (stepFrame(...))`，`PumpOnce()` 是"调一次"。两条路共用同一份顺序，
所以**不存在"自检跑的和真机跑的不一样"**这种分叉。

```
每帧（stepFrame）:
  1.   事件泵（SDL 事件 → 退出 / 键盘 / 鼠标状态）
  1a.  全局快捷键 Dispatch        ← 排在所有派发之前
  1b.  关窗请求检查（RequestClose / 自绘关闭按钮）
  1c.  预加载收尾 DrainPreloadCompletions（帧首）
  2.   刷展示倍率 syncMagnification
  2b.  Scene::DispatchPointer     ← 先按位置算焦点
  2c.  Scene::DispatchKeyboard    ← 再把按键推给"新的"焦点
  3.   Timeline::Advance(delta)   ← 逻辑步 0..N 次 + 每帧回调 1 次
  3b.  AudioTick(delta)           ← 音频与场景无关，不能挂在场景通道上
  4.   drawFrame（清屏色 + 场景 + 指针标记 + Present）
  5.   Scene::ConsumeFrameDirty   ← 脏的统一消费（只对活跃场景）
  5b.  mouse/keyboard ResetFrameFlags（瞬时状态一帧清一次）
  5c.  runFrameTasks              ← 帧末任务（D3）：读到的都是这一帧定稿的值
  5d.  CommitPendingDestructions  ← 待销毁场景在这里真析构
  6.   节流（有 vsync 交给 SDL；固定 delta 时不节流）
```

四条"顺序不能改"的理由：

1. **快捷键最先**：它是应用级命令，语义上先于任何组件的局部反应。命中后会把那条 Down
   事件从本帧事件里消费掉，于是焦点输入框收不到 `Ctrl+S`，但"按着 Ctrl"这个跨帧状态不受影响。
2. **键盘在指针之后**：点击可能刚把焦点换给别人，本帧按键要推给**新的**焦点节点。
3. **指针 / 键盘在推进时间线之前**：这样本帧画出来的就是本帧的交互结果，不会差一帧。
4. **销毁在帧末**：销毁场景会连带拆掉整棵子树，而这一帧的 Render / Tick / 派发都在遍历
   那份绘制列表——遍历中途拆容器就是悬垂。攒到这一步再删，"这一刻没有人在看它"。

## 非阻塞跑帧（0.5.0 D 组）

```C++
inking::InkingWindow& window = inking::InkingWindow::Instance();
window.SetFixedDelta(1.0f / 60.0f);      // D4：确定性时间步（0 = 用墙上时钟）
window.Open("main");                     // D1：建窗口 + 预加载，**不进循环**
window.RunFrames(2);                     // 跑两帧：事件泵 / 派发 / 渲染全走一遍
window.PostFrameTask([]{ /* 帧末 */ });  // D3：这一帧末执行
window.RunAfterFrames(2, [&]{ /* ... */ });
window.RequestClose(failures);           // D5：退出码 = 失败项数，给 CI
// Show() 的返回值就是这个码
```

| 入口 | 回答的问题 |
| --- | --- |
| `Open(scene)` | 把"建窗口"与"跑循环"分开；返回是否真的打开 |
| `PumpOnce()` | 跑**一帧**就回来；返回"还能不能继续跑" |
| `RunFrames(n)` | 连跑 n 帧，中途关窗就提前停；返回实际帧数 |
| `PostFrameTask(fn)` | 这一帧收尾之后跑一次 |
| `RunAfterFrames(n, fn)` | 第 n 帧收尾之后跑一次（`n <= 0` = 这一帧） |
| `SetFixedDelta(s)` | 固定每帧 delta，**且关掉节流**（跑 100 帧不用等 1.6 秒） |
| `RenderToTexture(path,w,h,scene)` | 离屏出图：**不建窗口**、不需要 dummy 驱动 |

`RenderToTexture` 走的是**同一条** `SceneLibrary::RenderScene`（形状 / 文字 / 裁剪 /
装饰笔全在内），不是"另画一份"。落盘是"先编码到内存，再 `fopen`（失败才退回 SDL 文件层）"
——实测受限环境里 `SDL_SavePNG(surface, path)` 会把文件名截断却返回 true。

## 退出码（0.5.0 D5）

`RequestClose(code)` 记下退出码，`GetExitCode()` 读回，`Show()` 原样返回。
被退出闸门否决（`SetExitConfirmHandler` 返回 false）时**不改退出码**，也不置关窗标志
——"用户点了取消"不该让上一轮的失败码生效。

## 无边框（自绘标题栏那一档）

`SetBorderless(true)` 之前或之后调都行：窗口已开出就 `SDL_SetWindowBordered`，
还没开就记下来，`Show()` 建窗口时带上 `SDL_WINDOW_BORDERLESS`。

它**只管窗口装饰**：设计坐标 / letterbox / 命中换算一个字都不变。

第一版**有意不做**：

- 无边框下的自绘改尺寸（要边缘把手 + 命中 + 最小尺寸约束）——改尺寸仍走
  `setWindowWidth` / `setWindowHeight`；
- 系统级吸附 / 双击最大化；
- 拖动是**按屏幕像素**累加的记账（`MoveWindowBy`），DPI 缩放下的"跟手程度"留给人眼判。

## 退出：一个闸门，没有旁路

```
ESC / 标题栏 × / 自绘关闭按钮 / 业务代码
              ↓
        RequestExit() 或 RequestClose()
              ↓
   SetExitConfirmHandler 装了吗？→ 返回 false ⇒ 本次否决，标志不置位
              ↓ 返回 true（或没装）
        _closeRequested = true  →  主循环下一帧退出
```

**为什么是回调而不是虚函数**：本类是私有构造的单例，外面派生不出实例，"可重写"
唯一落得下的形态就是"可替换的回调"。要弹自绘确认框（异步）就让回调返回 false、
自己记下"待确认"状态，用户点"确定"之后再调一次 `RequestExit()`。

**别自己 `exit()`**：那样主循环来不及收尾（场景帧末析构、纹理释放都会跳过）。

## 缩放：改窗口尺寸，不改内容倍率

letterbox 下黑边的唯一来源是"窗口像素比 ≠ 设计像素比"。要让黑边消失，只能让窗口比例等于
设计比例——那就是**等比改窗口尺寸**。改内容倍率只会把黑边换成裁剪或拉伸。

档位表（`GetZoomLadder()`）是**运行时生成**的：候选比值里挑出"设计尺寸 × zoom 能装进
显示器可用区域"的那些。有窗口时跟着窗口所在显示器（多屏下才正确），没有就用主显示器，
视频子系统还没初始化则只夹在 `[kMinZoom, kMaxZoom]`。

`SetZoom` 会**吸附到最近的档位**：档位之外的尺寸乘出来常常不是整数像素，两轴各取整一次
比例就偏了——黑边会回来（1px 量级，肉眼几乎看不出，但"严格等比"这句话就破了）。
要精确尺寸直接用 `setWindowWidth/setWindowHeight`（那是另一条语义，**不承诺比例**）。

**全屏下缩放是空操作**（尺寸由系统给）。

## 全屏

`SetFullscreen(bool)` / `ToggleFullscreen()` / `IsFullscreen()`，内置 **F11**，
**ESC 先退全屏再退程序**，也接 SDL 的 ENTER/LEAVE_FULLSCREEN 事件。

状态**真的**变了会 `fullscreenChanged.Emit(新值)`。为什么要事件而不是让菜单栏每帧轮询：
"在 `onRender` 里改可见性"是提交期改结构，正是本仓库反复避开的悬垂来源。

窗口还没建起来时只记账，`Show()` 建完窗口补应用一次。

## 用到的数据结构

>**ExitConfirmHandler**

```C++
using ExitConfirmHandler = std::function<bool()>;
```

返回 `true` 才真的退出。默认没装 = 直接退。

>**InkingEvent&lt;bool&gt; fullscreenChanged**

`InkingEvent` 的用法见 [InkingRuntime.md](InkingRuntime.md)：`Subscribe` 拿令牌、
`Unsubscribe(令牌)` 退订、`Emit` 派发（派发时先快照，允许回调里退订）。

>**config/window_config.h**

```C++
inline constexpr int kDefaultDesignWidth  = 1280;   // 默认值，不随 SetDesignSize 变
inline constexpr int kDefaultDesignHeight = 720;
int  DesignWidth() noexcept;                        // 运行期**实际值**
int  DesignHeight() noexcept;
void GetDesignSize(int& width, int& height) noexcept;
bool SetDesignSize(int width, int height);          // 必须在构造场景 / 窗口之前
```

设计尺寸是**运行期**的（`CXXCSS/config.json` 的 `design` 段决定的），库内一律读访问器。
`kDesignWidth` / `kDesignHeight` 仍保留为**默认值别名**，只为源码兼容——它们永远不随
`SetDesignSize` 变化，别拿它当"当前设计尺寸"用。

`SetDesignSize` 在窗口或场景**已经按旧尺寸定型**之后会拒绝改动（返回 false + ERROR），
不做"一半新一半旧"的半生效。详细理由见 `CXXCSS.md` §3.11。

>**window/WindowConfig.h**

```C++
struct WindowConfig {                 // 每一项都是 std::optional：空 = 不动
    std::optional<bool>          borderless;
    std::optional<bool>          roundedCorners;
    std::optional<bool>          transparent;    // 透明窗口（只能 Show() 之前）
    std::optional<std::uint32_t> background;     // 0xAARRGGBB
    std::optional<bool>          fullscreen;
};
int ApplyWindowConfig(const WindowConfig& config);   // 返回没生效的字段数
```

**启动配置 + 回读校验**。为什么不自己连调 `SetBorderless` 那四个：它们的返回值是"真的变了
没有"，幂等调用返回 false，和失败长得一样；`SetBackgroundColor` 干脆是 void。所以判据只能
是"读完再比"——这件事收在这一个函数里，失败逐条打 ERROR。

它**会构造窗口单例**，所以要排在 `SetDesignSize()` 之后（窗口根锚点按设计尺寸定型）。
`CXXCSS/config.json` 的 `window` 段就是它；生成的 `ApplyConfig()` 已经把顺序写死了。

## APIS

### Public

>**Instance()**

```C++
static InkingWindow& Instance();
```

私有构造的单例。**不要拷贝**（拷贝 / 赋值已 delete）。

>**GetWindowWidth() / GetWindowHeight() / setWindowWidth() / setWindowHeight()**

```C++
int GetWindowWidth() const noexcept;
int GetWindowHeight() const noexcept;
bool setWindowWidth(int width);
bool setWindowHeight(int height);
```

窗口像素尺寸。**不承诺保持设计比例**——要等比用 `SetZoom`。

>**GetCurrentScene()**

```C++
InkingScene* GetCurrentScene() const;
```

当前正在展示的场景。窗口**不缓存**场景指针：缓存了就得在场景关闭 / 析构时回头清掉，
现查一次更省心，也不会留下指向已销毁场景的野指针。

>**Show(sceneName)**

```C++
void Show(const std::string& sceneName = "");
```

**阻塞式**：建窗口 → 应用记账的窗口属性（无边框 / 全屏 / 圆角 / 缩放 / 底色）→
同步预加载该场景（`PreloadNow`，因为紧接着就要渲染它）→ 进主循环 → 退出时释放。
`sceneName` 为空则不预设场景（由业务自己 `SetActiveScene`）。

>**SetBorderless(borderless) / IsBorderless()**

```C++
bool SetBorderless(bool borderless);
bool IsBorderless() const noexcept;
```

默认 `false`。幂等：真的改了才返回 true。

>**SetAlwaysOnTop(alwaysOnTop) / IsAlwaysOnTop()（0.6.0）**

```C++
bool SetAlwaysOnTop(bool alwaysOnTop);
bool IsAlwaysOnTop() const noexcept;
```

窗口置顶（默认 `false`），幂等。与无边框不同，**运行期可切**：窗口已开出就直接
`SDL_SetWindowAlwaysOnTop`；还没开就记账，建窗时带 `SDL_WINDOW_ALWAYS_ON_TOP`。

`IsAlwaysOnTop()` 是**请求值**——平台到底照不照做读不回来（和 `IsTransparentApplied()`
那种"能回读"的不同）。

典型用途：桌宠、悬浮歌词、看板、计时器。配合 `SetBorderless(true)` + `SetTransparent(true)`
才是"贴在桌面上的一块角色"。⚠️ 透明在 Windows 上**只有 D3D11 后端成立**
（SDL 只给那条路补了透明 swapchain）；后端挑到 D3D12 / Vulkan 时窗口会是
"置顶、无边框、背景一整块黑"。需要透明请自己 `SDL_SetHint(SDL_HINT_RENDER_DRIVER, "d3d11")`
（`examples/pet/main.cpp` 就是这么写的）。

>**GetWindowX() / GetWindowY() / MoveWindowBy(dx, dy)**

```C++
int GetWindowX() const noexcept;
int GetWindowY() const noexcept;
void MoveWindowBy(int dx, int dy);
```

窗口左上角的**屏幕坐标** / 按屏幕像素挪一段（自绘标题栏拖动）。位移先累加到窗口自己的
坐标上（所以**没有真实窗口时也能断言**位置变化量），窗口已开出就顺手
`SDL_SetWindowPosition`。传 `(0,0)` 是空操作。

>**MinimizeWindow()**

```c++
void MinimizeWindow();
```

无边框下的自绘按钮用。没有窗口时是空操作。

>**RequestClose() / IsCloseRequested() / RequestExit() / SetExitConfirmHandler(handler)**

```C++
void RequestClose();
bool IsCloseRequested() const noexcept;
void RequestExit();
void SetExitConfirmHandler(ExitConfirmHandler handler);
```

两者**都过退出闸门**，区别只是"谁在请求"（`RequestClose` 是标题栏 ×，`RequestExit` 是
程序自己）——实际上都落到同一个标志，**没有旁路**。传空回调 = 清掉。

>**SetFullscreen(bool) / IsFullscreen() / ToggleFullscreen()**

```C++
bool SetFullscreen(bool fullscreen);
bool IsFullscreen() const noexcept;
void ToggleFullscreen();
```

幂等；真的改了才返回 true，并且会 `fullscreenChanged.Emit(新值)`。

>**kMinZoom / kMaxZoom / GetZoomLadder() / SetZoom(zoom) / ZoomBy(steps) / GetZoom()**

```C++
static constexpr float kMinZoom = 0.25f;
static constexpr float kMaxZoom = 4.0f;

std::vector<float> GetZoomLadder() const;   // 升序
bool SetZoom(float zoom);                   // 吸附到最近档位
bool ZoomBy(int steps);                     // 滚轮一格 = ±1；到头了 false
float GetZoom() const noexcept;             // 1.0 = 窗口逻辑尺寸 == 设计尺寸
```

`GetZoom()` 在窗口被外部改过尺寸时仍是"上一次设置的值"；`ZoomBy` 会以**实际尺寸**
反推档位，不会跳回历史值。

>**SetBackgroundColor(argb) / GetBackgroundColor()**

```C++
void SetBackgroundColor(std::uint32_t argb) noexcept;
std::uint32_t GetBackgroundColor() const noexcept;
```

窗口清屏色（`0xAARRGGBB`），默认 `0xFF121218`。那一笔发生在场景渲染之前，组件一个字都
不知道——**亮色场景必须把它一起改掉**，否则白卡浮在深底上会很怪。

**最高字节只在透明窗口下有意义。** 不透明窗口上 `SDL_RenderClear` 直接写帧缓冲、不参与
混合，帧缓冲的 alpha 又没人采信：写 0 也透不出桌面，画面仍是那个 RGB 的不透明底。这种
情况 `stepFrame` 会打一条 WARN（静默失效清单第 9 条）。

>**SetTransparent(transparent) / IsTransparentRequested() / IsTransparentApplied()**

```C++
bool SetTransparent(bool transparent);
bool IsTransparentRequested() const noexcept;
bool IsTransparentApplied() const noexcept;
```

透明窗口：清屏色的 alpha 真的透出桌面。默认 **false**。

- **只能建窗前设**（`Show()` / `Open()` 之前）。SDL3 的透明是建窗标志
  `SDL_WINDOW_TRANSPARENT`，没有事后切换的 API；窗口已打开时调用返回 false + 一条 WARN。
- `IsTransparentRequested()` 是**请求值**，`SetTransparent` 的返回值分不清"幂等"和
  "窗口已开"，所以 `WindowConfig` 的回读比的是它。
- `IsTransparentApplied()` 是"标志**进了窗口**"（建窗后读 `SDL_GetWindowFlags`），
  **不是"桌面真的透了"**——合成结果读不回来，只能真机目视 / 截图读 alpha。

**后端限制**：SDL 只在 Windows 的 D3D11 后端补过透明 swapchain；D3D12 明确不行，
Vulkan 多数环境不行，混合 GPU 也可能失效。已知不行的后端会在建渲染器后补一条 WARN。

> 本机实测（Windows + `direct3d11`，2026-10-22）：`SDL_WINDOW_TRANSPARENT` 进了窗口
> 标志，清屏 `alpha=0` / `alpha=128` 都能从后缓冲**原样读回**（`a=0` / `a=128`）。
> 但读回的 RGB 是 **straight** 值（`255,0,0,128` 而不是预乘的 `128,0,0,128`）——
> 如果 DWM 按 premultiplied 解释，带 alpha 的内容会**偏亮 / 过饱和**。全不透明的内容
> 不受影响；半透明描边 / 阴影要按真机校色。"桌面真的透了"仍只能人眼验收。

>**SetRoundedCorners(rounded) / IsRoundedCornersApplied()**

```C++
bool SetRoundedCorners(bool rounded);
bool IsRoundedCornersApplied() const noexcept;
```

Windows 11 的 DWM 圆角；其它系统 / 旧版本静默无效（动态加载失败就退回方角，
**不需要版本分支**）。返回值是"系统接受了没有"——真正的圆角由 DWM 画，
所以另有 `IsRoundedCornersApplied()` 把属性**读回来**。默认**开**。

>**HasWindow()**

```c++
bool HasWindow() const noexcept;
```

真窗口建起来了没有（`Show()` 之后 true、释放之后 false）。自检用它区分
"跑在真窗口上"与"只有逻辑、没有窗口"两种场合。

>**RequestCapture(path) / Capture(path) / HasPendingCapture()**

```c++
bool RequestCapture(const std::string& path);   // 排到本帧末（Present 前）
bool Capture(const std::string& path);          // 立即读当前渲染目标
bool HasPendingCapture() const noexcept;
```

把画面存成图片。**落在窗口层**的理由只有两条：渲染器是窗口建的、随窗口销毁；
"这一帧画完了没有"也只有窗口知道。业务侧要截图不该绕 `ImmediateDraw` 去抓
渲染器，更不该手写 BMP 编码。

| 方法 | 什么时候读像素 | 用在哪 |
| --- | --- | --- |
| `RequestCapture` | 本帧末、`Present` **之前** | **推荐**。按钮回调、快捷键、逻辑步里随便调 |
| `Capture` | 立刻 | 只有"画完还没 Present"的调用点有意义（自检 / 工具）；`Present` 之后内容未定义 |

格式**按扩展名定**：`.png` 走 `SDL_SavePNG`（要求 SDL ≥ 3.4，本仓库是 3.4.16）；
其它（含不带扩展名）走 `SDL_SaveBMP`。**不引 SDL3_image**——截图不该给可选依赖
新增必需项。

三条边界：

- 截的是**整个窗口帧缓冲**（含 letterbox 黑边），不是逻辑画布那一块；
- **不含指针小方块**（那一笔调试脚手架画在截图之后）；
- 没有渲染器时**当场返回 false 并报错**，不留一个永远不落盘的排队请求。

>**GetShortcuts()**

```C++
ShortcutRegistry& GetShortcuts() noexcept;
const ShortcutRegistry& GetShortcuts() const noexcept;
```

进程级快捷键表（唯一持有者）。**注册者必须在析构时 `Unbind`**——这张表比它们活得久，
挂着已死的 `this` 就是悬垂。详见 [InkingInput.md](InkingInput.md)。

### 自由函数

>**ApplyWindowConfig(const WindowConfig&)**

```C++
int ApplyWindowConfig(const WindowConfig& config);   // namespace inking
```

见上面「用到的数据结构 → window/WindowConfig.h」。返回**没生效的字段数**（0 = 全部生效）。

>**GetLastRenderedScene()**

```C++
InkingScene* GetLastRenderedScene() noexcept;   // namespace inking
```

上一帧真的渲染了哪个场景；没有场景时为 `nullptr`。纯自检观察点：`Show()` 返回时窗口已经
释放、场景名也被清掉，所以"窗口确实按名字解析到场景并渲染出来了"只能靠这个记录来验。

## 环境变量

| 变量 | 作用 |
| --- | --- |
| `INK_AUTOQUIT` | 设了就自动退出（跑 `inking_demo` 做冒烟测试用），时长由内部常量决定 |
| `INK_TEST_FONT` | 自检指定一个可读字体路径（沙箱里 `SDL_IOFromFile` 可能失败） |
| `INK_CAPTURE` | **零代码截图**：设成图片路径，启动后的**第一帧**就落一张（`.png` / `.bmp`）。排错时"先看一眼画面"比加日志快得多 |

`INK_CAPTURE` 的典型用法（无头也能跑，dummy 视频驱动 + 软件渲染器）：

```bash
SDL_VIDEO_DRIVER=dummy INK_CAPTURE=/tmp/shot.png INK_AUTOQUIT=1 ./inking_demo.exe
```

## 常见坑

1. **拿 `GetWidth()` 当窗口宽度**：那是设计画布，不是窗口像素。
2. **改设计尺寸只改 `kDesignWidth`**：场景根锚点、CXXCSS 里的尺寸、缩放档位都会跟着偏，
   改之前先看清谁在读它。
3. **自己 `exit()` 而不走 `RequestExit()`**：跳过帧末析构与纹理释放。
4. **在 `Show()` 之前依赖 `HasWindow()`**：还没建窗口时它一直是 false。
5. **指望全屏下 `SetZoom` 生效**：全屏下是空操作。
6. **在按钮回调里调 `Capture()`**：那一刻后缓冲里是上一帧、甚至未定义的内容
   （`Present` 之后不保证保留）。回调里请用 `RequestCapture()`。
7. **以为截图只截逻辑画布**：截的是整个窗口帧缓冲，含 letterbox 黑边。

---

> 本文档对应版本：**0.6.6（0.x 发布版）**。
