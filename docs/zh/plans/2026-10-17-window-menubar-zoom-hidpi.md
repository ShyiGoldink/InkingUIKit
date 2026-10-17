# 方案：无边框窗口 / 菜单栏快捷键 / 全屏隐藏 / 滚轮缩放 / 文本高清

> 状态：**已批准并落地**（2026-10-17）。批准时的调整与实施结果见文末 **§11 实施记录**  
> （A 项按裁定放弃；缩放档位改为运行时生成、允许 1px 黑边；ESC 先退全屏；退出加闸门；  
> 不做 hit-test 拖边）。  
> 日期：2026-10-17  
> 涉及层：`window/`（主）、`prefab/MenuBar`、`input/`、`render/InkingText`、`examples/demo`

---

## 0. 结论（TL;DR）

五条需求全部可做，且**没有一条需要改动分层或坐标模型**。落点如下：

| # | 需求                   | 落点                                                                   | 新增公开 API                                                                                         | 实质代价                            |
| - | -------------------- | -------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------ | ------------------------------- |
| A | 构造期可选无边框             | `window/InkingWindow`                                                | `WindowOptions` + `Instance(options)`                                                            | 低（现有 `SetBorderless` 的能力前移到构造期） |
| B | 菜单项绑定全局快捷键           | 新增 `input/Shortcut` + `input/ShortcutRegistry`，由窗口持有；`MenuBar` 提供便利口 | `Shortcut`、`ShortcutRegistry`、`MenuBar::AddButton` 第 4 参                                         | 中（要动到"本帧按键事件谁来消费"这条链路）          |
| C | 全屏下自动隐藏菜单栏           | 窗口新增全屏能力 + `fullscreenChanged` 事件；`MenuBar` 订阅                       | `SetFullscreen/IsFullscreen/ToggleFullscreen/fullscreenChanged`、`MenuBarStyle::hideInFullscreen` | 低                               |
| D | 菜单栏吃滚轮缩放（严格设计比例、无黑边） | `InkingWindow` 持有"缩放档位"，缩放 = **按档位改窗口像素尺寸**；`MenuBar` 转发滚轮           | `SetZoom/ZoomBy/GetZoom/GetZoomLadder`、`MenuBarStyle::zoomWheel`                                 | 中（档位表 + 中心保持 + 屏幕夹取）            |
| E | 高清（暂时只做文字）           | `render/InkingText` 内部按**渲染器实际比例**烘字                                 | **无**（纯内部改动）                                                                                     | 中（要改烘字尺寸与落点换算，且不能动布局）           |

一句话概括设计取向：**能落在窗口层的都落在窗口层**（无边框、全屏、缩放、快捷键都依赖 SDL 或进程唯一状态），**组件层只做"转发 + 记账"**（MenuBar 不认识 SDL、不认识全屏，只订阅事件、只调窗口的公开入口）。

---

## 1. 现状核实：先纠正四个前提

方案建立在当前代码的事实上，这四条与直觉不同，值得先钉住：

**1.1 无边框开关已经存在，缺的只是"构造期定型"。**  
`InkingWindow::SetBorderless()` 已实现（`InkingWindow.h:78`、`InkingWindow.cpp:717`），  
`Show()` 建窗口时也会带上 `SDL_WINDOW_BORDERLESS`（`InkingWindow.cpp:860-862`）。  
问题只有一个：`InkingWindow` 是私有构造的单例，**没有"构造时带参数"的入口**。

**1.2 MenuBar 已经存在，是一个完整的预制件。**  
`prefab/MenuBar`（21 个 prefab 文件之一）已有：任意 `InkingWidget` 子项、布局弹簧、  
`draggable`（拖窗口）、`captionButtons`（最小化/关闭）。缺的是快捷键、滚轮、全屏联动。

**1.3 全屏能力完全没有。** 窗口层没有 `SetFullscreen`、没有 `SDL_SetWindowFullscreen`  
调用、没有全屏状态位。需求 C 必须先补这一层。

**1.4 "没做 2x 渲染"的实际状况（这一条决定了需求 E 的做法）。**  
`Show()` 里设了一次 `SDL_SetRenderLogicalPresentation(LETTERBOX)`（`InkingWindow.cpp:914`），  
而**除窗口自己以外，所有节点的 `GetMagnification()` 恒为 1.0**  
（只有 `InkingWindow.cpp:463/954` 写倍率）。于是：

- `SubmitToRenderer` 传给 `onRender` 的 `pixelX/Y` 实际是**设计坐标**（`InkingWidget.cpp:790`）；
- 形状是按设备像素光栅化的多边形 → 放大后**依然锐**（顶点与羽化带都跟着 GPU 走）；
- 文字纹理按 `fontSize × 1.0` 烘成位图（`InkingText.cpp:795`），再被整幅逻辑缩放拉大 → **放大就糊**。

⇒ 需求 E 不需要引入 2x 渲染框架，只需要**让纹理按真实设备比例烘**。这也正是  
SDL3 头文件里点名的那条用法（"to make fonts look sharp, the app turns off logical  
presentation while drawing text"；我们用等价但更省事的做法，见 §6）。

**1.5 "没有边框就不能缩放"确实成立。** SDL 的 borderless 在 Windows 上是 `WS_POPUP`，  
没有 `WS_THICKFRAME`，系统不提供边缘拖拽命中。所以 D 不是可选装饰，是无边框的**代偿**。

---

## 2. 需求 A：构造期选择无边框

### 2.1 方案

新增 `include/window/WindowOptions.h`（纯数据、无 SDL）：

```cpp
struct WindowOptions {
    bool borderless = false;                    // 不要系统标题栏/边框（自绘菜单栏接管）
    bool resizable  = true;                     // 是否允许系统改尺寸（无边框下系统本来就不给）
    bool fullscreen = false;                    // 建窗即全屏
    int  width      = kDesignWidth;             // 逻辑窗口尺寸（= 设计尺寸 × 缩放）
    int  height     = kDesignHeight;
    std::string title = "InkingWindow";
};
```

`InkingWindow` 增加：

```cpp
static InkingWindow& Instance(const WindowOptions& options = {});  // 首次调用定型
const WindowOptions& GetOptions() const noexcept;
```

**语义（三条，写进注释与日志）**：

1. **首次调用定型**：`Instance(options)` 构造单例，`options` 生效；
2. **`Show()` 之前仍可改**：后续 `Instance(options)` 在窗口尚未建立时**应用**新选项  
   （因为 `_borderless/_title/_windowWidth` 本来就是纯记账，改它们没有代价）；
3. **`Show()` 之后只认 setter**：与已生效值不同就打一条 `INK_LOG_WARN`，不生效。  
   理由是"窗口开着的时候换标题栏要走 `SDL_SetWindowBordered`（一次真实的窗口重建）"，  
   那件事已经有 `SetBorderless()` 负责，不该被"构造参数"偷偷做掉。

### 2.2 为什么不是"公开构造函数 / 拆掉单例"

`WindowState`（SDL 窗口 + 渲染器 + vsync 标志）目前是**进程唯一**的  
（`InkingWindow.cpp:220-233`，且 `InkingWindow` 自己就是 SDL 全局状态的持有者）。  
拆单例 = 把 `WindowState` 搬进实例 + `SceneLibrary` 的活跃场景变成"每窗口一份"

- 事件泵/输入也要每窗口一份 —— 这是一次**多窗口**改造，工作量与风险都远大于本轮需求。

保留单例、把参数从构造期传入，是当前结构下最小且不封死未来的做法：将来真要多窗口，  
`WindowOptions` 与 `Instance(options)` 的签名可以原样保留（改成 `Create(options) -> Window&` 即可）。

---

## 3. 需求 B：菜单项绑定全局快捷键

### 3.1 三个词先定义清楚

- **"全局"= 应用内全局**：窗口有焦点时，无论焦点在哪个组件上（输入框也不行）都生效。  
  **不做** OS 级全局热键（`RegisterHotKey`）——那要求应用在后台也抢键，极易与系统/其它软件冲突，  
  且平台相关；列为扩展方向（§7.4）。
- **"快捷键"** = 修饰键组合 + 主键，例：`Ctrl+Shift+S`、`F5`、`Ctrl+=`。
- **"绑定"** = 一个快捷键 → 一个无参回调。菜单项只是**最常用的绑定点**，不是唯一绑定点。

### 3.2 `Shortcut`（`include/input/Shortcut.h`，纯数据）

```cpp
struct Shortcut {
    std::int32_t key = 0;              // SDL_Keycode 数值（不把 SDL 拖进公开头）
    std::uint8_t modifiers = 0;        // Shift/Ctrl/Alt/Super 位掩码
    bool allowRepeat = false;          // 默认长按不重复触发

    bool Empty() const noexcept;
    bool Matches(const KeyEvent& e) const noexcept;   // 修饰键必须**完全相等**
    static Shortcut Parse(std::string_view text, bool* ok = nullptr);  // "Ctrl+Shift+S"
    std::string ToString() const;                     // 回写配置 / 日志 / 自检
};
```

两个设计点：

- **修饰键完全匹配**：绑了 `Ctrl+S`，按 `Ctrl+Shift+S` **不**触发（否则将来  
  "另存为"会被"保存"吃掉）。真需要宽松匹配就再加一档 `matchAnyModifier`，不默认。
- **带 `Parse/ToString`**：本项目所有配置最终来自 CXXCSS json（`CXXCSS.md`），  
  快捷键迟早要从 json 读。现在就把字符串 ⇄ 结构体的双向定下来，避免那一轮再改 API。

### 3.3 `ShortcutRegistry`（`include/input/ShortcutRegistry.h/.cpp`）

```cpp
class ShortcutRegistry {
public:
    using Action = std::function<void()>;
    std::uint64_t Bind(const Shortcut& s, Action action, std::string label = {});
    bool Unbind(std::uint64_t id);
    std::size_t Count() const noexcept;
    bool IsTaken(const Shortcut& s) const noexcept;

    // 窗口每帧调一次：遍历本帧按键事件，命中就执行并把那条事件消费掉。
    std::size_t Dispatch(KeyboardInput& input);
};
```

- **冲突策略**：先到先得。重复绑同一个组合 → `Bind` 返回 `0` 并 `WARN`（不静默替换，  
  替换会让"后来者悄悄干掉前者"变成最难查的一类 bug）。
- **消费语义**：命中后调用 `KeyboardInput::ConsumeFrameEvent(index)`（新增），  
  **只删本帧的这条 Down 事件，不动跨帧的按键状态**。于是：焦点输入框收不到 `Ctrl+S`，  
  但 `IsKeyDown(Ctrl)` 仍然为真（游戏/拖拽逻辑要的就是跨帧状态）。
- **为什么不放场景层**：场景的键盘派发是"推给焦点节点"，天生**不全局**。  
  快捷键必须能绕过焦点规则，所以它必须住在焦点之上——窗口层是唯一合适的落点。

### 3.4 每帧顺序（`runLoop` 里改三行）

```
pumpEvents()                       // 攒状态与事件
→ shortcuts.Dispatch(keyboard)     // ① 快捷键先吃（可消费事件）
→ dispatchPointer()                // ② 位置换了人？
→ dispatchKeyboard()               // ③ 剩下的推给焦点
```

顺序理由：快捷键是"应用级命令"，语义上**先于**任何组件的局部反应；而它又必须在  
`dispatchPointer` 之前，这样"点进输入框 + 同帧按 Ctrl+S"仍按类工具的习惯做事。

### 3.5 MenuBar 侧

```cpp
InkingStaticButton* AddButton(const ButtonData& look, const std::string& text,
                              ButtonCallback onActivate = {},
                              const Shortcut& shortcut = {});   // 新增第 4 参
std::uint64_t BindShortcut(const Shortcut&, ButtonCallback);     // 手动绑
std::size_t GetBoundShortcutCount() const noexcept;              // 自检观测口
```

- `~MenuBar()` 里**逐个 `Unbind`**。这是硬要求：`ShortcutRegistry` 活在进程级单例里，  
  而 MenuBar 会随场景销毁——不退订就是"回调里 `this` 已死"的悬垂。  
  （`InkingEvent` 有令牌退订，`ShortcutRegistry` 同样用 id 退订，语义一致。）
- 快捷键触发时**不**要求菜单栏可见 → 全屏下（菜单栏已隐藏）快捷键照样能用，  
  这正是"全局"的实用价值：全屏退出键可以绑在菜单项上。

---

## 4. 需求 C：全屏 + 菜单栏自动隐藏

### 4.1 窗口层（新增）

```cpp
bool SetFullscreen(bool fullscreen);       // 幂等；返回是否真的变了
bool IsFullscreen() const noexcept;
void ToggleFullscreen();
InkingEvent<bool> fullscreenChanged;       // 只在真的变化时 Emit
```

- 实现走 `SDL_SetWindowFullscreen(window, bool)`（SDL3 签名，无 SDL2 的 enum），  
  窗口没建起来时只记账，`Show()` 建完窗口补应用一次（和 `SetRoundedCorners` 同一套写法）。
- **同时接 `SDL_EVENT_WINDOW_ENTER_FULLSCREEN` / `LEAVE_FULLSCREEN`**：全屏不一定是我们  
  自己切进去的（系统手势、其它按键、未来某个组件）。只信自己的开关，就会出现  
  "状态位说不是全屏、画面是全屏"的静默不一致。
- 键位：内置 **F11** 切换（窗口层处理，与 ESC 同一处 `pumpEvents`）。

### 4.2 ESC 的优先级（**需你裁定，见 §10-1**）

现状：ESC 直接退出主循环（`InkingWindow.cpp:323-329`）。全屏之后这条会变成  
"想退出全屏，结果退了程序"。建议改成：**全屏时 ESC 先退全屏，非全屏时 ESC 才退出**。  
代价：一行分支；好处：全屏里不会一脚踩空。若你希望保持"ESC 永远是退出"，就不改，  
但那时全屏只能靠 F11 退出，需要在文档里写明。

### 4.3 菜单栏侧

```cpp
// MenuBarData.h
bool hideInFullscreen = true;   // 全屏时自动隐藏（默认开）
```

- 构造函数里订阅 `InkingWindow::Instance().fullscreenChanged`，回调里：  
  `_hiddenByFullscreen = full; SetVisible(!full);`（先记账再隐藏，退出全屏时只恢复  
  "被这条规则藏起来的那次"，不强行覆盖业务自己的 `SetVisible(false)`）。
- `~MenuBar()` 里 `Unsubscribe(token)`（同上：单例事件比组件活得久）。
- **为什么不轮询**：`onRender` 是提交期，在里头改可见性等于"遍历绘制列表时改结构"  
  —— 那正是本仓库反复避开的悬垂来源（`InkingScene.cpp:620-625` 的帧末析构就是为此）。

---

## 5. 需求 D：滚轮缩放（严格设计比例，不产生黑边）

### 5.1 为什么"缩放"必须表现为**改窗口尺寸**

当前黑边来自 letterbox：`letterboxMagnification = min(px/1280, py/720)`  
（`InkingWindow.cpp:197-211`），窗口比不是 16:9 时多出来的那一轴就是黑边。  
**无黑边的充要条件：窗口像素比 == 设计比**。所以在"不引入内容裁剪"的前提下，  
缩放只能落在窗口尺寸上：

> 缩放 N 档 → 窗口逻辑尺寸 = (1280, 720) × zoom → 像素尺寸 = 逻辑尺寸 × DPI 比例  
> → 两轴比例相等 → `min()` 的两项相等 → **letterbox 倍率精确等于 zoom，黑边为 0**。

这同时解释了为什么"严格按设计比例"是对的：只有等比，`min()` 才不会退化成某一轴。

### 5.2 档位表（这是"严格"的技术保证）

设计尺寸 1280×720，`gcd` 决定的"整数且严格 16:9"的缩放值恰是 **k/80**：

| zoom | 窗口逻辑尺寸   | zoom | 窗口逻辑尺寸    |
| ---- | -------- | ---- | --------- |
| 0.25 | 320×180  | 1.25 | 1600×900  |
| 0.40 | 512×288  | 1.50 | 1920×1080 |
| 0.50 | 640×360  | 2.00 | 2560×1440 |
| 0.75 | 960×540  | 3.00 | 3840×2160 |
| 1.00 | 1280×720 | 4.00 | 5120×2880 |

```cpp
static constexpr std::array<float, 10> kZoomLadder;   // 上表
bool SetZoom(float zoom);      // 吸附到最近档位（不是任意值）；返回是否变化
bool ZoomBy(int steps);        // 滚轮：档位表前进/后退（一格 = 1 档）
float GetZoom() const noexcept;
std::size_t GetZoomStep() const noexcept;
```

**为什么"吸附"而不是接受任意倍数**：任意 `zoom` 乘出来常常不是整数像素  
（1280×0.9 = 1152 ✓、720×0.9 = 648 ✓；但 1280×0.87 = 1113.6 ✗），  
取整之后两轴比例差 1 px，黑边就回来了 —— 那是"看起来没问题、量出来不严格"的坑。  
用档位表把"严格等比"变成**构造上的保证**，代价是缩放不连续（这正是鼠标滚轮该有的手感）。

`ZoomBy` 的起点不存 `_zoom`，而是从**当前窗口实际尺寸**反推最近档位  
（`min(px/1280, py/720)`）：这样即使业务用 `setWindowWidth` 把窗口调成了非档位尺寸，  
滚一下也能回到档位序列，不会"跳回上一个历史值"。

### 5.3 应用缩放时的两条细节

1. **窗口中心不动**：记旧尺寸与旧位置 → 新位置 = 旧中心 − 新尺寸/2  
   → 用 `SDL_GetDisplayUsableBounds` 夹进可用区域（避免缩放把标题栏/边框推到屏幕外）。
2. **全屏下是空操作**：全屏的尺寸由系统给，此时改窗口尺寸没有意义（记一条 DEBUG 即可）；  
   退出全屏后 `_zoom` 仍是用户上次选的那档，窗口回到那个尺寸。

### 5.4 MenuBar 侧（"让它吃鼠标滚轮"）

```cpp
// MenuBarData.h
bool zoomWheel = true;     // 默认开
int  zoomWheelStep = 1;    // 一格滚轮走几档
```

两个实现要点：

- **必须重写 `HandlesSubtreeWheel()`**（`InkingPointerHandler.h:43`）。  
  否则指针停在菜单栏上的某个**按钮**时，滚轮会派给按钮（场景只把滚轮给"指针下最上面那个"），  
  而按钮不处理滚轮 → 什么都不会发生。声明之后场景会沿父链找到菜单栏  
  （`InkingWidget.cpp:803-810`），滚轮归它。  
  实现：`return _style.zoomWheel || InkingStaticPanel::HandlesSubtreeWheel();`
- **`onPointerWheel` 里先缩放、不滚动**：菜单栏自己也可能是滚动容器，两者同时开时  
  以缩放优先（写进注释，避免"滚一下两件事都发生了"）。

### 5.5 与需求 E 的联动（这是本方案最划算的一处）

缩放改变窗口尺寸 → `syncMagnification` 每帧重算倍率 → 文本烘字比例跟着变  
→ **放大后的文字也是清晰的**，而不是把 1x 位图拉大。两个需求共用同一条"设备比例"通道。

---

## 6. 需求 E：文本高清（不做 2x 渲染框架）

### 6.1 现状与目标（单位要算清）

绘制调用发生在**逻辑坐标系**里（`SDL_SetRenderLogicalPresentation` 生效），  
1 逻辑单位 = `ratio` 个设备像素。当前文字按 `deviceSize = fontSize × 1.0` 烘，  
再被整幅放大 `ratio` 倍 → 每个字形被重采样一次。


目标：**按 `fontSize × ratio` 设备像素烘，按 `fontSize` 逻辑单位画**。  
屏幕上占的物理像素数不变，但纹理与像素 1:1 → 不再重采样。

### 6.2 `ratio` 从哪来（不新增全局状态）

不新增槽位、不让 `render/` 反向依赖 `window/`。直接问渲染器：

```cpp
ratio = min(renderOutputW / logicalW, renderOutputH / logicalH)   // 取不到 → 1.0
```

即 `SDL_GetCurrentRenderOutputSize` + `SDL_GetRenderLogicalPresentation`。  
它**与窗口层的 `letterboxMagnification` 恒等**（同一个 min），但取值点是绘制现场，  
和"谁写的倍率"解耦 —— 渲染器换了、窗口换了、全屏了都自动跟得上，而 `render/` 依旧不认识窗口层。

### 6.3 要改的三处（都在 `src/render/InkingText.cpp`）

| 位置                             | 现状                            | 改为                                                                |
| ------------------------------ | ----------------------------- | ----------------------------------------------------------------- |
| `PaintText`（:788）              | `deviceSize = fontSize * mag` | `rasterScale = mag * ratio`；`deviceSize = fontSize * rasterScale` |
| `DrawTextureTransformed`（:213） | 直接按纹理像素尺寸画                    | 画出来尺寸 = `纹理尺寸 / ratio`（除回逻辑单位）                                    |
| 逐字形路径（:701）                    | 落点 `cell.x / mag`             | `cell.x / rasterScale`，位图尺寸 `/ ratio`                             |

三条都是一次乘法/除法的替换，**不改任何调用方签名**：`PaintText` 的公开形态  
（`include/render/InkingText.h:101`）一个字都不变，`MeasureText` 仍返回设计单位。

### 6.4 三个必须一起处理的边界

1. **上限**：`ratio` 夹到 `[1.0, 3.0]`。超过 3 倍继续加清晰度收益递减、显存线性涨  
   （每段文字的纹理面积是 `ratio²`）。3 倍以上仍按 3 倍烘，由 GPU 放大。
2. **比例变化时清缓存**：`ratio` 变了说明窗口/DPI 变了，旧的按旧比例烘的纹理与旧图集  
   全成了垃圾（缓存键里含设备字号，所以它们不会命中，只会**永久占着内存**）。  
   处理：比例变化超过 0.01 时放掉纹理表与字形图集（**保留字体表**，字体与比例无关）。  
   记账到一个计数器上，自检可断言"缩放之后纹理确实重烘了"。
3. **图集容量**：`GlyphAtlas(1024)`（`InkingText.cpp:63`）。2 倍下 24px 字形的占位面积 ×4，  
   约 450 个字形就会满。**不会出事故**——`AcquireGlyph` 返回无效槽时逐字形路径返回  
   `false`，`PaintText` 落回整段渲染（`InkingText.cpp:806-810`）。本轮**不做**动态扩容，  
   只把"图集满 → 回退"的计数暴露给自检（已有 `glyphStats.failed`）。

### 6.5 验收判据（这条比"看起来更清楚"重要）

**布局零位移**：同一段文字在 `ratio = 1` 与 `ratio = 2` 下绘制，墨迹包围盒（非背景像素的  
外接矩形）之差 ≤ 1 px；字号、行距、居中位置都不变。  
"清晰度"本身无法自动断言，按本项目惯例归入"观感判定"（人眼看真窗口）。

### 6.6 为什么不做整幅 2x 渲染

| 方案                                  | 收益            | 代价                                                     | 结论                                     |
| ----------------------------------- | ------------- | ------------------------------------------------------ | -------------------------------------- |
| 本轮：只把文字按设备比例烘                       | 文字立刻清晰；零布局风险  | 形状仍由 GPU 直接光栅化（本来就锐），图片纹理仍按源分辨率采样                      | **采用**                                 |
| 整幅 2x 渲染目标（1280×720 → 2560×1440 再缩） | 图片/线宽也受益，实现简单 | 填充率 ×4；所有坐标多一层"设计/2x 设备"二义；与 letterbox、命中换算、裁剪矩形全部要过一遍 | 暂不做（将来若要做图片放大，优先走"按需生成高分辨率纹理"而不是整幅 2x） |
| 所有节点改成 `mag = 真实倍率`（设备像素直画）         | 理论最干净         | 与现有 LETTERBOX 冲突（会二次缩放），等于重写整条绘制链路                     | 不做                                     |

---

## 7. 可选的更优 / 扩展性更强的方向

按"值得现在就做 / 值得留位 / 不值得"分三档。**这些都不在本轮范围内**，除非你点名。

### 7.1 值得现在就留位的（成本几乎为零）

- **`WindowOptions` 作为长期入口**：以后 `vsync`、`renderDriver`、`letterbox 模式`、  
  `DPI 策略`（`config/window_config.h:9` 那句"扩展位"）都往这里加，不再动签名。
- **`Shortcut::Parse/ToString`**：等 `CXXCSS` 真接进来，菜单配置里直接写 `"shortcut": "Ctrl+S"`。
- **`ShortcutRegistry` 独立成类**：将来"命令面板""全局搜索"也是同一张表，不必再发明一套。

### 7.2 值得做但要单独一轮的

- **无边框的真边缘拖拽改尺寸**（`SDL_SetWindowHitTest`）：给窗口四周 4~6 px 返回  
  `SDL_HITTEST_RESIZE_*`，系统就会给出**真**拖边手感（含光标变化、Windows 的吸附/贴边）。  
  约 60 行回调。收益明显（缩放只是代偿，不能替代拖拽），但它和"无边框自绘"是另一条独立能力，  
  建议单列一轮，别塞进本轮把风险搅在一起。
- **快捷键配置化**：从 json 读 + 运行期改键 + 冲突提示。依赖 CXXCSS 那一层。
- **菜单项显示快捷键文本**（右对齐的 `Ctrl+S`）：现在 MenuBar 是横排按钮，没有下拉列表，  
  显示位置还没有答案；等有了下拉菜单再做。

### 7.3 明确不做

- **OS 级全局热键**（应用不在前台也响应）：要 `RegisterHotKey`/`XGrabKey` 分支，  
  且与"只在窗口有焦点时收键"的输入模型冲突。真要做，单开一层 `platform/hotkey`。
- **多窗口**：见 §2.2。
- **整幅 2x 渲染**：见 §6.6。

---

## 8. 改动清单（文件级）

**新增**

| 文件                                                                    | 内容                                |
| --------------------------------------------------------------------- | --------------------------------- |
| `include/window/WindowOptions.h`                                      | 构造期选项（纯数据、无 SDL）                  |
| `include/input/Shortcut.h` + `src/input/Shortcut.cpp`                 | 快捷键值类型 + `Parse/ToString/Matches` |
| `include/input/ShortcutRegistry.h` + `src/input/ShortcutRegistry.cpp` | 绑定表 + 每帧派发                        |

> `CMakeLists.txt:60` 用的是 `GLOB_RECURSE ... CONFIGURE_DEPENDS`，新 `.cpp` 自动入构建，不用改构建脚本。

**修改**

| 文件                                                    | 改动                                                                                                                                                                                  |
| ----------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `include/window/InkingWindow.h`                       | `Instance(options)`、`WindowOptions` 存取；全屏三件套 + `fullscreenChanged`；缩放四件套；`GetShortcuts()`；私有成员 `_options/_zoom/_fullscreen`                                                         |
| `src/window/InkingWindow.cpp`                         | 构造吃 options；建窗口用 `_options.title/resizable/fullscreen`；`SetFullscreen` 实现 + 两个 SDL 事件分支；F11/（可选）ESC 优先级；`SetZoom/ZoomBy`（含中心保持、usable bounds 夹取）；`runLoop` 里插入 `shortcuts.Dispatch` |
| `include/input/KeyboardInput.h` (+`.cpp`)             | `bool ConsumeFrameEvent(std::size_t index)`                                                                                                                                         |
| `include/prefab/MenuBarData.h`                        | `hideInFullscreen`、`zoomWheel`、`zoomWheelStep`                                                                                                                                      |
| `include/prefab/MenuBar.h` / `src/prefab/MenuBar.cpp` | `AddButton` 第 4 参、`BindShortcut`、`GetBoundShortcutCount`、`HandlesSubtreeWheel` 重写、`onPointerWheel` 重写、订阅/退订 `fullscreenChanged`、`~MenuBar`                                          |
| `src/render/InkingText.cpp`                           | 按 `ratio` 烘字（三处换算）、比例变化清缓存、上限 3.0                                                                                                                                                   |
| `include/render/InkingText.h`                         | 仅补注释（`magnification` 与设备比例的关系），签名不动                                                                                                                                                 |
| `examples/demo/main.cpp`                              | 用 `Instance({.borderless=true, .title="Inking 示例"})`；加一条 MenuBar（文件/视图/全屏 + 快捷键）演示                                                                                                  |
| `examples/demo/selftest.cpp`                          | 新增 §9 的断言                                                                                                                                                                           |

---

## 9. 验证方案

延续本项目的两级验证：**无头自检给判据，真窗口给人眼**。

### 9.1 自检新增（`inking_demo_selftest`，预计 +12 项）

| #  | 断言                                                                                          | 说明             |
| -- | ------------------------------------------------------------------------------------------- | -------------- |
| 1  | `Instance({.borderless=true}).IsBorderless() == true`                                       | 构造期选项生效        |
| 2  | `Show()` 之后 `Instance({.borderless=false})` 不改现值且打 WARN                                     | "首次定型"语义       |
| 3  | `SetZoom(1.25)` → `GetWindowWidth/Height == 1600/900`                                       | 档位吸附 + 整数尺寸    |
| 4  | 各档位 `width*720 == height*1280`（严格 16:9）                                                     | "零黑边"的数学判据     |
| 5  | `ZoomBy(+1)` 从 1.0 → 1.25；到顶再 `ZoomBy(+1)` 不变（返回 false）                                     | 档位与夹取          |
| 6  | `SetFullscreen(true)` → `IsFullscreen()`；`fullscreenChanged` 回调**恰好**收到 1 次                 | 幂等 + 事件        |
| 7  | 全屏时 `SetZoom(2.0)` 不动窗口尺寸                                                                   | 空操作约定          |
| 8  | `Shortcut::Parse("Ctrl+Shift+S")` 往返 `ToString()` 一致；`Parse("Ctrl+Foo")` → `ok=false`       | 解析健壮性          |
| 9  | `Ctrl+S` 命中；`Ctrl+Shift+S` **不**命中；无修饰的 `S` 不命中                                             | 修饰键严格匹配        |
| 10 | `Dispatch` 命中后 `KeyboardInput` 本帧少一条 Down 事件、`IsKeyDown` 仍为真                                | 消费语义（不破坏跨帧状态）  |
| 11 | 重复 `Bind` 同一组合 → 第二次返回 0、`Count()` 不变                                                       | 冲突策略           |
| 12 | 文本：`ratio=1` 与 `ratio=2`（用 2560×1440 的窗口 + 软件渲染器）绘制同段文字，墨迹包围盒差 ≤ 1px；且 `texturesCreated` 增加 | **布局零位移**是核心判据 |

> 无头能验的边界：dummy 视频驱动不给真的 DPI 缩放（`SDL_GetWindowSizeInPixels` 就等于请求尺寸），  
> 所以第 12 项用"窗口尺寸 / 设计尺寸 = 2"来构造 `ratio = 2`，而不是依赖真实 DPI。

### 9.2 真窗口人工清单（`inking_demo`）

1. 无边框启动：没有系统标题栏/边框；菜单栏可拖窗口；右上角最小化/关闭有效；
2. 指针停在菜单栏上滚轮 → 窗口按档位缩放，**四边都没有黑边**（逐个档位看一眼）；
3. 缩放时窗口**中心不动**，且不会被推出屏幕；
4. 放大到 1.5 档后文字明显更清晰（对比 1.0 档），且按钮/文字位置**没有位移**；
5. F11 进全屏：菜单栏消失、无黑边（16:9 显示器）、快捷键仍然有效；再 F11 退出：菜单栏回来；
6. ESC 行为符合裁定（§10-1）；
7. 关窗后 stderr 为空（本项目的老规矩）。

### 9.3 回归

现有 17 项自检必须全过；`cmake --build build` 全绿（新增文件由 glob 自动纳入）。

---

## 10. 风险与待裁定（6 条）

| # | 事项         | 建议                                                               | 若你不同意                          |
| - | ---------- | ---------------------------------------------------------------- | ------------------------------ |
| 1 | 全屏下 ESC 语义 | **ESC 先退全屏，再按才退出程序**                                             | 保持"ESC 恒退出"，全屏用 F11 退          |
| 2 | 缩放档位是否吸附   | **吸附到 k/80 档位表**（严格等比的技术保证）                                      | 允许任意倍数 → 必须接受 ≤1px 黑边          |
| 3 | 缩放的屏幕夹取    | 超过 `SDL_GetDisplayUsableBounds` 的档位**不进入表**（1.5 档在 1080p 上通常不可达） | 允许窗口大于屏幕（部分内容不可见）              |
| 4 | 快捷键冲突      | **先到先得 + WARN**，不静默替换                                            | 后到覆盖（难查）                       |
| 5 | 快捷键的"全局"范围 | **应用内全局**（窗口有焦点即生效）                                              | 要 OS 级则单开 `platform/hotkey` 一轮 |
| 6 | 无边框真拖边改尺寸  | 本轮**不做**（缩放代偿），单列一轮做 `SDL_SetWindowHitTest`                      | 本轮一起做（+1 天，风险与缩放混在一起）          |

另有两条**实现期**的注意（不需要裁定，写进注释即可）：

- `MenuBar` 现在会在构造时 `Instance()`，**这会抢先构造窗口单例**。方案里的  
  "`Show()` 之前仍可改选项"正是为它准备的：示例/业务先 `Instance(options)` 再建场景即可，  
  即使顺序反了，只要在 `Show()` 之前就仍然生效。
- 全屏切换会改变 `letterboxMagnification`，而 `syncMagnification` 每帧重算  
  （`InkingWindow.cpp:457-465`），所以不需要为新状态补任何刷新逻辑 —— 这也顺带保证了  
  §6 的文本比例在 F11 之后自动正确。

---

**批准方式**：直接说"按方案做"或指出上表 1~6 的取舍；我按批准后的版本开工，先做 A→C（窗口层），  
再做 B（快捷键链路），最后 D/E（缩放与文本），每步跑一次自检。

---

## 11. 实施记录（2026-10-17）

### 11.1 与批准稿的差异（按你的裁定）

| 项        | 批准稿                         | 实际落地                                              | 原因                                                           |
| -------- | --------------------------- | ------------------------------------------------- | ------------------------------------------------------------ |
| A 构造期无边框 | 新增 `WindowOptions`          | **不做**                                            | 已有 `SetBorderless()`，`Show()` 前调用即可（`Show()` 建窗口时直接带标志，不会重建） |
| D 缩放档位   | 设计尺寸 1280×720 推出 k/80 的精确档位 | **运行时生成**：候选比值 × 当前显示器可用区域筛；1px 黑边可接受             | 设计尺寸可自定义（`window_config.h` 将来由 inkgen 生成），档位表不能写死            |
| C ESC    | 建议先退全屏                      | 采纳                                                | —                                                            |
| 退出程序     | 建议可重写                       | 落成 `SetExitConfirmHandler(std::function<bool()>)` | 本类是私有构造单例，派生不出实例，"可重写"只能落成"可替换回调"                            |
| B 快捷键消费  | 命中即消费                       | 采纳（先消费再执行回调）                                      | "消费且未被穿透才触发"留给将来那套完整消费链                                      |
| 无边框真拖边   | 建议单列一轮                      | 不做                                                | —                                                            |

### 11.2 落地清单

**新增**

- `include/input/Shortcut.h` + `src/input/Shortcut.cpp`：值类型 + `Parse/ToString/Matches`（修饰键**完全匹配**、`allowRepeat` 默认 false）。
- `include/input/ShortcutRegistry.h` + `src/input/ShortcutRegistry.cpp`：绑定表，冲突**先到先得 + WARN**，`Dispatch` 收集命中 → 消费本帧事件 → 再执行回调（回调里可能析构注册者）。

**修改**

- `include/input/KeyboardInput.h/.cpp`：`ConsumeFrameEvent(index)`（只删本帧事件，不动跨帧状态）。
- `include/window/InkingWindow.h/.cpp`：`SetFullscreen/IsFullscreen/ToggleFullscreen` + `fullscreenChanged`；`SetZoom/ZoomBy/GetZoom/GetZoomLadder`（中心保持、显示器可用区域夹取、全屏下空操作）；`GetShortcuts()`；`SetExitConfirmHandler/RequestExit`；`pumpEvents` 接 **F11**、**ESC 先退全屏**、**SDL 的 ENTER/LEAVE_FULLSCREEN**；`runLoop` 在指针/键盘派发之前调 `GetShortcuts().Dispatch`。
- `include/prefab/MenuBarData.h`：`hideInFullscreen` / `zoomWheel` / `zoomWheelStep`（默认 true/true/1）。
- `include/prefab/MenuBar.h` / `src/prefab/MenuBar.cpp`：`AddButton` 第 4 参绑快捷键、`BindShortcut/GetBoundShortcutCount`、`HandlesSubtreeWheel` 重写、`onPointerWheel` 缩放优先、订阅/退订 `fullscreenChanged`、**析构注销全部快捷键**。
- `src/render/InkingText.cpp`：`ratio` 从渲染器自问（`输出尺寸 / 逻辑尺寸`），烘字用 `fontSize × mag × ratio`、画出来除回 `ratio`；上限 3.0；比例变化时丢字形图集（保留图片纹理）。
- `CMakeLists.txt`：补 `INK_HAS_SDL3_TTF` / `INK_HAS_SDL3_IMAGE`（见 11.3）。
- `examples/demo/main.cpp`：无边框 + 菜单栏（拖动/最小化/关闭 + 放大/缩小/全屏/退出四项，各带快捷键）+ 退出闸门示例。
- `examples/demo/selftest.cpp`：新增 24 条断言（缩放档位与等比、全屏幂等与联动、菜单栏自动隐藏、快捷键解析/匹配/冲突/消费/注销、文字高清的比例来源与布局零位移）。

### 11.3 实施中发现的两件事（都不是本轮改动引入的）

**① `INK_HAS_SDL3_TTF` / `INK_HAS_SDL3_IMAGE` 从未被定义。**  
`src/render/InkingText.cpp` 里有两个可选依赖的 `#if` 分支，而 `CMakeLists.txt` 只  
`find_package` + 链接，**没有给编译定义**。后果是文字层一直走"占位方块"、图片一直  
"解不出来"——**不报错、不警告**，界面上看起来"有字"，只是每段文字都是方块。  
已经补上定义（四个包本来就在 REQUIRED 链接）。这同时意味着：本轮之前任何关于  
"文字清晰度"的验收都会是假通过。

**② `inking_demo_selftest` 在进程退出时挂住**（打印完"失败项：N"之后不返回）。  
用一个只调 `TaskQueue::instance().addTask(...)` 的 10 行探针复现 ⇒ 与本次改动无关：  
`TaskQueue` 的函数内静态量在 **DLL 卸载（DllMain，持加载器锁）** 时析构并 join 管家  
线程，worker 若在此时触碰需要加载器锁的东西就会死锁。因此"退出码 = 失败项数"这条  
CI 约定目前实际不可用（要读 stdout 的最后一行）。**未在本轮修**——那属于线程收尾  
语义，应当单独一轮处理（detach / 不把长驻线程放进静态量 / 明确 quick_exit 策略）。

### 11.4 验证结果

- `cmake --build build` 全绿（唯一警告仍是既有的 `NOMINMAX` 重定义）。
- `inking_demo_selftest`：**失败项 0**（原 17 项 + 新增 24 项，其中 1 项如实在沙箱里  
  跳过，见下）。
- `inking_demo`：`SDL_VIDEO_DRIVER=dummy` + `INK_AUTOQUIT` 冒烟通过，主循环正常进入与  
  退出，stderr 为空。
- **环境限制（不是代码问题）**：当前沙箱里 `SDL_IOFromFile` 对所有路径都失败  
  （同一路径用 `fopen` 正常）。`TTF_OpenFont` 走的就是这条 IO，所以文字无法真正栅格化，  
  自检里"文字走了字形图集"那条**如实标记为 skip**（可用 `INK_TEST_FONT` 指定一份  
  可读字体再验）。其余高清判据（比例来源、原点/尺寸不变、2 倍设备像素）照常执行并通过。

---

> 本文档对应版本：**0.1.0-test（测试版本）**。
