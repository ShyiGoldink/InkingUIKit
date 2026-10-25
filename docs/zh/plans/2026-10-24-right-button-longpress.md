# 方案：右键输入 + 触摸长按（左键语义的对称补齐）

> 状态：**已实现**（0.5.0，2026-10-24）。自检新增 21 条断言全绿，全仓失败项 0。
> 与方案的偏差见 §12。
> 日期：2026-10-24
> 目标版本：`0.5.0`（**含破坏性接口改动**：指针钩子签名变更）
> 涉及层：`input/`（按钮维度）、`detail/`（指针判定）、`scene/`（分槽捕获 + 长按）、
> `window/`（喂状态 + 触摸合成开关）、`button/` `panel/` `text/` `image/` `prefab/`（迁移 8 处）、
> `examples/`、`docs/`

---

## 0. 结论（TL;DR）

**右键不是"缺一个事件"，而是缺一整个维度**。当前指针链路从窗口层到组件契约，
全程只有"一个 bool 的按下"，按钮身份在三个地方被抹掉：

| 层 | 现状 | 证据 |
| - | - | - |
| 窗口层（SDL → 状态） | 右键**已经在记** | `MouseInput.cpp:73-105` 两个 `case SDL_BUTTON_RIGHT` 都写了 |
| 窗口层（状态 → 场景） | **断了**：只喂左键 | `InkingWindow.cpp:476` 只传 `state.leftButtonDown` |
| 场景层 | **单槽捕获**：`_pressedNode` 一个指针 + `_pointerWasDown` 一个 bool | `InkingScene.h:760-802` |
| 组件契约 | **无按钮参数**：`onPointerPress(bool)` / `onPointerClick()` | `InkingPointerHandler.h:115-118` |
| 动态档 | **单键判定**：`PointerProbe::Update(hit, down)` | `InkingPointerProbe.h:62` |

所以方案不是"加一条 if"，而是**把按钮这一维贯穿五层**，同时把触摸长按解释成 Secondary：

1. **统一输入（本库的跨平台立场，不是对触摸的补偿）**：组件层永远只看到 `Primary` /
   `Secondary` 两种按下状态，**永远不需要知道输入设备是什么**。设备差异只在窗口层被吸收：
   鼠标左 / 右键一一对应，触摸**长按自动映射为 Secondary**。这条是下面所有取舍的判据——
   任何"这个组件只支持鼠标"的分支都是设计错误。
2. **语义位而非设备位**：`PointerButton::Primary`（主键）/ `Secondary`（次键）。
   将来加中键或笔侧键 = 加一个枚举值，按槽并行的代码自动跟上（§4.1）。
3. **分槽捕获**：`_pressedNode` 变成按按钮索引的数组。左右同按互不覆盖——单槽会静默丢
   `press(false)`，按钮永久停在 Pressed 色（§4.1 的实测理由）。
4. **长按 = 主键捕获作废 + 次键捕获接管**，不是"抑制左键 click"。
   复用现成的 `captureAborted` 语义，**零新增旁路标志**（§4.3）。
5. **长按判定住在场景层**（`TickLogic`），不是 `MouseInput`。`MouseInput` 的契约是
   "只维护状态、不做判定"（其头注释），而阈值 / slop 是可替换的交互策略。
6. **左键语义一条不改**：press / click / hover / drag / wheel / 键盘焦点仍只由 Primary 驱动。
7. **click 携带坐标**（按下点）。右键菜单"在哪弹"就是它的语义本身，坐标不是可选项。

三条必须**同时**落地的耦合改动（漏一条就是静默坏掉，§4.4）：

- 关掉 SDL 的触摸→鼠标合成（`SDL_HINT_TOUCH_MOUSE_EVENTS=0`），否则来源会抖、长按永远不成立；
- `inside` 判据补 `SDL_WINDOW_INPUT_FOCUS`（禁掉合成后 `SDL_WINDOW_MOUSE_FOCUS` 在纯触摸下不置位，
  触摸会整条失效）；
- `RemoveDrawNode` / `DetachSceneFromNodes` 要清**两个**槽（少清一个就是野指针）。

| 编号 | 任务 | 批次 | 落点 | 代价 |
| - | - | - | - | - |
| T-01 | 新增 `PointerButton.h`（按钮 / 来源 / 按下状态） | a | 新建 `include/input/PointerButton.h` | 低 |
| T-02 | 指针钩子加按钮维度 + click 带坐标 | a | `include/input/InkingPointerHandler.h` | 中（破坏） |
| T-03 | 迁移 8 处重写点（右键一律不进三态） | a | 见 §5 表格 | 低 |
| T-04 | `PointerProbe` 改双键判定 + `anyDownEdge` | a | `include/detail/InkingPointerProbe.h`、`InkingFixedPopup.cpp` | 中 |
| T-05 | 窗口层：喂 `PointerDownState` + `source` + 触摸合成开关 | b | `InkingWindow.cpp:460-479`、`1641-1660` | 低 |
| T-06 | 场景层：分槽捕获 + 次键派发（不含长按） | b | `InkingScene.h/.cpp` | 中 |
| T-07 | 场景层：长按状态机（`TickLogic` + 下一帧消费） | c | `InkingScene.cpp:189` / `1027` | 中 |
| T-08 | 示例改右键弹菜单 + 自检 9 条断言 | d | `examples/demo/main.cpp:232-273`、`selftest.cpp` | 低 |
| T-09 | 文档同步（API 页 / CHANGELOG / 输入设计） | d | `docs/zh/` 四处 | 低 |

---

## 1. 语义定案

### 1.1 两张表

设备 → 语义（**吸收点只在窗口层**，往上一律是 Primary / Secondary）：

| 设备输入 | 语义 | 何时判定 |
| - | - | - |
| 鼠标左键按下 / 抬起 | `Primary` | 事件当场 |
| 鼠标右键按下 / 抬起 | `Secondary` | 事件当场 |
| 触摸轻点（按下 → 抬起） | `Primary` press + click | 抬起时 |
| 触摸长按（按住不动 ≥ 阈值） | `Secondary` press | 阈值到达的那一帧 |
| 触摸长按后抬起（仍在同一点） | `Secondary` click | 抬起时 |
| 中键 | **只记状态、不派发**（§9） | — |

结论一句话：**`Primary` 与 `Secondary` 在语义上是同构的两种输入状态**——都有 press / release /
click、都带坐标、都在同一套捕获规则下。区别只有三条（见 §1.2 的 1~3 条），而"哪只手按的"
在组件层不可见。

`PointerSource` 只用来回答"要不要长按转义"，**不进入组件契约**（`InkingPointerHandler` 的
任何钩子都不带它）。它的存在是为了让"设备差异被吸收"这件事有一个明确的落点，
而不是让上层写 `if (touch)`。

### 1.2 七条不变量（改之前先读这一节）

1. **hover 只跟主键**。右键不改 `_hoveredNode`：hover 是"我指着谁"，不是"我按着谁"。
2. **右键不抢键盘焦点**。焦点只由 Primary 的 `justPressed` 产生。否则右键点空白会清掉
   输入框焦点，未提交的编辑被吞——Windows / macOS 都不这么做。
3. **右键不参与拖动与滚轮**。拖动、滚轮、`GetDraggingNode()` 都只认 Primary 槽。
4. **长按期间手指移动超过 slop ⇒ 取消长按候选**（转为左键拖动 / 滚动）。这是"按住后滑走"
   不该弹菜单的唯一依据。
5. **长按成立 ⇒ 主键捕获当场作废**（发 `Press(Primary,false)`，**不发 click**）。
   "取消"必须发生在捕获层，不是在 click 判定处加 flag。
6. **次键 click 在抬起时派发**，且要求"抬起时指针仍在按下时那个节点上"——与主键同一套
   `stillOnIt` 判据，不另立规则。
7. **click 的坐标是"按下点"**，不是抬起点。理由：触摸长按的锚点是按下处，手指微动几像素
   不该让菜单跟着挪；鼠标右键按下与抬起通常同点，两者无差异。实现上它与拖动基准、
   长按 slop 基准共用同一份 `_pressOrigin`（§4.2），不新增第二份坐标记忆。

### 1.3 数值

| 常量 | 值 | 理由 |
| - | - | - |
| `kLongPressSeconds` | `0.5f` | 桌面触摸屏的通用值；与 iOS 持平，比 Android（0.4）略长，误触更少 |
| `kLongPressSlop` | `8.0f` **设计像素** | 与全仓库几何同单位；随窗口缩放会等比变化，这是设计坐标体系的既有取舍 |

两个都放 `InkingScene` 的 `protected` 静态常量，派生场景可改（阈值是产品决策）。
**本轮不调参**：先取通用值让能力成立，真机手感留到有触摸设备时再定——不阻塞落地。

---

## 2. 数据层（T-01）

新建 `include/input/PointerButton.h`，**不依赖 SDL**（与 `MouseInput.h` 同一条纪律：

```cpp
#pragma once

#include <cstddef>
#include <cstdint>

namespace inking {

/// 指针按钮。**语义位**而不是设备位：Primary = 主键，Secondary = 次键。
/// 加第三个值（中键 / 笔侧键）时，`kPointerButtonCount` 与 `PointerDownState`
/// 一起改，按槽并行的代码不用动——这正是"分槽"而不是"特判右键"的收益。
enum class PointerButton : std::uint8_t {
    Primary = 0,    ///< 鼠标左键 / 触摸轻点
    Secondary = 1,  ///< 鼠标右键 / 触摸长按
};

inline constexpr std::size_t kPointerButtonCount = 2;

/// 事件来源。**只有触摸**会走"长按 → Secondary"的转义。
enum class PointerSource : std::uint8_t {
    Mouse = 0,
    Touch = 1,
};

/// 各键的按下状态（替代原来的单个 `bool down`）。
struct PointerDownState {
    bool primary = false;
    bool secondary = false;

    bool any() const noexcept { return primary || secondary; }

    bool IsDown(PointerButton button) const noexcept {
        // 具名字段比 `bool[2]` 可读，代价是这里要穷举。加枚举值时编译器不会
        // 提醒（`switch` 有 default 才不警告），所以下面用 static_assert 钉住。
        return button == PointerButton::Primary ? primary : secondary;
    }

    void SetDown(PointerButton button, bool down) noexcept {
        if (button == PointerButton::Primary) {
            primary = down;
        } else {
            secondary = down;
        }
    }

    friend bool operator==(const PointerDownState&, const PointerDownState&) = default;
};

/// 槽位下标。所有"按按钮并行"的数组都用它索引（`_pressedNode` / `_pressOrigin` /
/// `PointerProbe::_buttons`），避免各处散落 `static_cast<std::size_t>`。
inline constexpr std::size_t ToIndex(PointerButton button) noexcept {
    return static_cast<std::size_t>(button);
}

static_assert(kPointerButtonCount == 2,
              "加了新的 PointerButton 就必须同步扩 PointerDownState 与 IsDown");

}  // namespace inking
```

> 下文为了可读性把 `_pressedNode[ToIndex(PointerButton::Primary)]` 简写成
> `_pressedNode[Primary]`；实现时按前者写。

> 用 `operator==` 默认实现（C++20，项目已是 `CMAKE_CXX_STANDARD 20`）是为了
> `SetPointerState` 的"真的变了没"判据能整包比较——那里少比一个字段，
> 右键按下就不会通知动态组件，定点浮窗的"点外部关闭"对右键失灵（静默）。

---

## 3. 契约层（T-02 / T-03 / T-04）

### 3.1 `InkingPointerHandler`：加参数，不并存两套

```cpp
// 前
void PushPointerPress(bool press) noexcept;
void PushPointerClick() noexcept;
virtual void onPointerPress(bool /*press*/) {}
virtual void onPointerClick() {}

// 后
#include <input/PointerButton.h>

void PushPointerPress(PointerButton button, bool press) noexcept {
    onPointerPress(button, press);
}
void PushPointerClick(PointerButton button, float designX, float designY) noexcept {
    onPointerClick(button, designX, designY);
}

virtual void onPointerPress(PointerButton /*button*/, bool /*press*/) {}
virtual void onPointerClick(PointerButton /*button*/,
                            float /*designX*/, float /*designY*/) {}
```

**不保留旧的 `onPointerPress(bool)`**。理由是本仓库已经两次吃过"两套入口"的亏
（`InkingButtonLike` 与 `InkingPointerHandler` 的职责拆分、静态 / 动态两档语义分叉）：
派生类只要重写了其中一个，"另一个还会不会被调"就没有答案，最可能的表现是
**双触发**或**静默不生效**（和"漏写 `override`"是同一类 bug）。
`0.x` 阶段一次改完 8 处重写点（§5），成本远低于长期歧义。

`onPointerHover` **不加参数**：只有主键有 hover（不变量 1）。
`onPointerPress` **不带坐标**：按下不关心位置（要位置问场景即可）。

**为什么 click 带坐标、而 press 不带**——这两者不是同一类东西：

| | `onPointerStateChanged` / `onPointerPress` | `onPointerClick` |
| - | - | - |
| 性质 | **信号 / 转变**：现在是什么状态 | **事件**：发生过一次 |
| 之后还能读吗 | 能。状态一直挂着，随问随答 | 不能。事件过去，状态已经变了 |
| 因此 | 不带参数（带了就是把状态抄第二份） | **必须带**（不带就永久丢失） |

`onPointerDrag(designX, designY, dx, dy)` 早就是这个规矩，click 只是跟上。
`onPointerStateChanged` 那句"带坐标等于把约定抄一遍"针对的是**每帧可读的持续状态**，
不适用于一次性事件——这一点必须在实现时写进头注释，否则会有人拿那句话来反对这个签名。

### 3.2 `PointerProbe`（动态档）

```cpp
struct PointerButtonResult {
    bool downEdge = false;  ///< **发生了**一次按下（不管在谁身上）
    bool pressed = false;   ///< 按下发生在本节点上（捕获开始）
    bool released = false;  ///< 捕获结束
    bool clicked = false;   ///< 按下与抬起都落在本节点上
};

struct PointerProbeResult {
    bool hoverChanged = false;
    bool hovered = false;
    bool anyDownEdge = false;   ///< 任一键的 downEdge（"点外部就关"用这个）
    PointerButtonResult primary;
    PointerButtonResult secondary;

    const PointerButtonResult& For(PointerButton b) const noexcept {
        return b == PointerButton::Primary ? primary : secondary;
    }
};

class PointerProbe {
public:
    PointerProbeResult Update(bool hit, const PointerDownState& down) noexcept;
    void Reset() noexcept;                       // 两个槽一起复位
    bool IsHovered() const noexcept;
    bool IsCaptured(PointerButton button) const noexcept;
private:
    struct Button { bool wasDown = false; bool captured = false; };
    bool _hovered = false;
    Button _buttons[kPointerButtonCount]{};
};
```

内部把 `Update` 的判定体抽成对 `Button` 的循环——**与场景层（§4.2）同一份边沿语义**，
两处不许各写一遍（`PointerProbe` 存在的理由就是这句话，见其文件头注释）。

调用点两处：

| 文件 | 前 | 后 |
| - | - | - |
| `InkingDynamicButton.cpp:219` | `_probe.Update(hit, scene->IsPointerDown())` | `_probe.Update(hit, scene->GetPointerDown())`，读 `r.primary` |
| `InkingFixedPopup.cpp:267` | `r.downEdge && !r.hovered` | `r.anyDownEdge && !r.hovered` |

### 3.3 `InkingButtonLike`：右键**不进三态**，另开一个事件槽

按钮的 `Press` 只该由 Primary 驱动，否则右键按下会让按钮变 Pressed 色——用户以为左键生效了。
所以：

```cpp
// 新增（与 `clicked` 并列）
InkingEvent<float, float> contextClicked;   ///< 次键点击（右键 / 长按 / 触摸长按），带按下点
using ButtonContextClickCallback = std::function<void(float designX, float designY)>;

bool TriggerContextClick(float designX, float designY);   ///< 同 TriggerClick，返回是否真的派发
bool HasOnContextClicked() const noexcept;
void SetOnContextClicked(ButtonContextClickCallback callback);  // 语义同 SetOnClicked：替换
```

`InkingStaticButton` 的两个钩子变成：

```cpp
void InkingStaticButton::onPointerPress(PointerButton button, bool press) {
    if (button != PointerButton::Primary) {
        return;   // 右键不换外观：三态是"主键手势"的可视化
    }
    MousePress(press);
}
void InkingStaticButton::onPointerClick(PointerButton button, float x, float y) {
    if (button == PointerButton::Primary) {
        TriggerClick();
    } else {
        TriggerContextClick(x, y);
    }
}
```

`InkingDynamicButton` 同构（`InkingButtonLike` 两档共用，所以它自动拿到 `contextClicked`）：

```cpp
const auto r = _probe.Update(hit, scene->GetPointerDown());
if (r.primary.clicked)   { TriggerClick(); }
if (r.secondary.clicked) { TriggerContextClick(x, y); }   // x/y 见下
```

**`clicked` 仍然是 `InkingEvent<>`（不带坐标），`contextClicked` 带坐标——这个不对称是故意的**：

- 左键的业务语义是"这个按钮被激活了"，位置无意义（按钮在哪就是它的身份）；且 `clicked`
  已有大量订阅者，改签名的破坏面远超收益。
- 右键的业务语义是"**在这一点的上下文**"，位置就是语义本身。业务要拿它 `OpenAt(x, y)`。

### 3.4 坐标从哪来（一处定义，三处复用）

不新增"指针坐标"的第二份记忆，直接用**按下点**（不变量 7）：

| 用途 | 取值 |
| - | - |
| click 的坐标 | `_pressOrigin[Primary/Secondary]` |
| 拖动增量的基准 | 同一份（`justPressed` 时写入） |
| 长按 slop 的基准 | 同一份（`_pressOrigin[Primary]`） |

场景在 `SetPointerState` 里写一次（**不是**在 `DispatchPointer` 的 `justPressed` 分支），
之后只读：

```cpp
// SetPointerState：与 _pointerDown 同步更新，一次按下只写一次
for (std::size_t i = 0; i < kPointerButtonCount; ++i) {
    const auto button = static_cast<PointerButton>(i);
    if (down.IsDown(button) && !_pointerDown.IsDown(button)) {
        _pressOrigin[i] = MousePoint{designX, designY};
    }
}
```

**为什么写点必须在 `SetPointerState`**：`TickLogic` 里的 slop 判定要读它，而自检 / 手动跑帧
可以只调 `SetPointerState` + `TickLogic`（不调 `DispatchPointer`）。写在 `DispatchPointer` 里
就会读到上一次交互的残留值，slop 一次比一次大，"长按永远不成立"且**没有任何报错**。

动态档自己读（它是"拉状态"的）：`scene->GetPointerX/Y()` 在 `onPointerStateChanged` 里
就是本帧的指针位置；要严格对齐"按下点"则用场景新增的
`const MousePoint& GetPressOrigin(PointerButton) const`。**推荐后者**，否则长按后手指
微动会让菜单偏移几像素（`PointerProbe` 只报 `clicked`，不报坐标，正是因为它不该再存一份）。

---

## 4. 场景层（T-06 / T-07）

### 4.1 为什么是"分槽"而不是"复用 `_pressedNode`"

| 方案 | 左右同按时 | 判定 |
| - | - | - |
| 共用 `_pressedNode` | 右键按下覆盖左键槽 ⇒ 抬起时左键的 `press(false)` **永远不来** ⇒ 按钮永久停在 Pressed 色 | **否** |
| 只处理右键、拒绝左右同按 | 需要一个"已有捕获就忽略另一个键"的分支，且要决定谁优先 | 否（额外规则） |
| **按按钮分槽** | 两个槽各走各的边沿，互不影响 | **是** |

左右同按在桌面上是真实操作（右键菜单弹出时顺手左键点别处），而且失败形态是
**静默的视觉错误**——这类 bug 在本仓库的定位成本最高（`AGENTS.md` §6 多次记录）。
分槽还顺手给了"加中键"一条零成本的扩展路径。

成员改动（`InkingScene.h:758-802`）：

```cpp
PointerDownState _pointerDown{};        ///< 本帧（替代 bool）
PointerDownState _pointerWasDown{};     ///< 上一帧
PointerSource _pointerSource = PointerSource::Mouse;  ///< **锁定在按下那一刻**
InkingWidget* _pressedNode[kPointerButtonCount] = {nullptr, nullptr};
MousePoint _pressOrigin[kPointerButtonCount]{};       ///< 按下点（§3.4 的唯一一份坐标记忆）
```

`GetPressedNode()` 返回 `_pressedNode[Primary]`（语义不变，兼容既有调用方与自检），
新增 `GetSecondaryPressedNode()` 与 `GetPressOrigin(PointerButton)`。

### 4.2 `DispatchPointer` 的改动骨架

改动集中在 `InkingScene.cpp:1102-1148` 这一段，**其余五段（hover / drag / wheel / 焦点 / 动态通知）
的判定体不动，只把 `_pressedNode` 换成 `_pressedNode[Primary]`**：

```cpp
// ---- 2. 按下 / 抬起（按按钮分槽） ----
for (std::size_t i = 0; i < kPointerButtonCount; ++i) {
    const auto button = static_cast<PointerButton>(i);
    const bool isDown  = _pointerDown.IsDown(button);
    const bool wasDown = _pointerWasDown.IsDown(button);
    const bool justPressed  = isDown && !wasDown;
    const bool justReleased = !isDown && wasDown;
    InkingWidget*& slot = _pressedNode[i];

    if (justPressed) {
        slot = topmost;
        // 按下点不在这里写：`_pressOrigin` 由 `SetPointerState` 与 `_pointerDown`
        // 同步更新（§3.4），这里只读。两处都写就会分叉。
        if (button == PointerButton::Primary) {
            _lastDragX = _pointerX;      // 拖动基准只属于主键
            _lastDragY = _pointerY;
            SetKeyboardFocus(...);       // 焦点只由主键产生（不变量 2）
        }
        if (InkingWidget* pressed = dispatchTarget(slot)) {
            pressed->PushPointerPress(button, true);
        }
    }

    // 捕获作废：指针离开窗口。**逐槽判**，两个槽都要能独立作废。
    const bool captureAborted = (slot != nullptr) && !_pointerInside;

    if (justReleased || captureAborted) {
        InkingWidget* pressed = dispatchTarget(slot);
        const bool stillOnIt = justReleased && slot != nullptr
                            && slot == topmost && _pointerInside;
        const MousePoint origin = _pressOrigin[i];   // 先存值：下面 slot 要清掉
        slot = nullptr;                  // 先清再派发：回调里可能析构本节点
        if (pressed != nullptr) {
            pressed->PushPointerPress(button, false);
            if (stillOnIt) {
                // 坐标取**按下点**而不是当前指针（不变量 7）
                pressed->PushPointerClick(button, origin.x, origin.y);
            }
        }
    }
}
_pointerWasDown = _pointerDown;
```

其余四段的键替换（逐条对照，避免漏改导致"右键也能拖动"）：

| 位置 | 前 | 后 |
| - | - | - |
| `:1184` hover 独占 | `_pressedNode != nullptr && topmost != _pressedNode` | `_pressedNode[Primary] != nullptr && topmost != _pressedNode[Primary]` |
| `:1197` 捕获者退出 hover | 同上 | 同上 |
| `:1218` 拖动 | `_pressedNode` | `_pressedNode[Primary]` |
| `:1243` 滚轮目标 | `_pressedNode ? _pressedNode : topmost` | `_pressedNode[Primary] ? ... : topmost` |
| `:1111` 焦点 | 已在循环内，只 Primary | — |

### 4.3 长按状态机

**位置**：`InkingScene::TickLogic`（`InkingScene.cpp:189`）开头，在 `onTick` **之前**——
它决定本帧的输入语义，组件 Tick 读到的应该是裁决后的状态。

```cpp
// 长按只在"触摸 + 主键按着"时计时；其余情况一律复位。
if (_pointerSource != PointerSource::Touch || !_pointerDown.primary) {
    _longPress = {};                 // tracking / fired / seconds 全清
} else if (!_longPress.fired) {
    if (!_longPress.tracking) {
        _longPress.tracking = true;
        _longPress.seconds  = 0.0f;
        // 基准**不在这里记**：直接用按下点（§3.4 的唯一一份坐标记忆）。
        // 这里再存一份，就会出现"slop 按 A 点算、菜单弹在 B 点"的分叉。
    }
    _longPress.seconds += static_cast<float>(fixedStepSeconds);

    const MousePoint& origin = _pressOrigin[ToIndex(PointerButton::Primary)];
    const float dx = _pointerX - origin.x;
    const float dy = _pointerY - origin.y;
    if (dx * dx + dy * dy > kLongPressSlop * kLongPressSlop) {
        _longPress.tracking = false;       // 走开了 ⇒ 这次是拖 / 滚，不是长按
    } else if (_longPress.seconds >= kLongPressSeconds) {
        _longPress.fired = true;
        _longPressPending = true;          // 下一帧 DispatchPointer 消费
    }
}
```

**消费**（`DispatchPointer` 里、命中之后、press 循环之前）：

```cpp
if (_longPressPending) {
    _longPressPending = false;
    _longPress.held = true;            // 虚拟次键"按着"，交给下面的 press 循环

    // 命中按**按下点**重算：slop 内微动几像素不该改判目标
    // （"按在按钮边缘却弹在邻居身上"就是这么来的）。
    const MousePoint origin = _pressOrigin[ToIndex(PointerButton::Primary)];
    if (_pointerInside) {
        const HitResult atPress = QueryHit(origin.x, origin.y);
        topmost = atPress.IsHit() ? atPress.target : nullptr;
    } else {
        topmost = nullptr;
    }

    // 次键的按下点 = 手指按下的那一点。真实右键由 SetPointerState 写，
    // 长按这条是合成路径，不补就是上一次右键的残留值。
    _pressOrigin[ToIndex(PointerButton::Secondary)] = origin;

    // 主键捕获当场作废：发 press(false)、**不发 click**。
    if (InkingWidget* primary = dispatchTarget(_pressedNode[Primary])) {
        primary->PushPointerPress(PointerButton::Primary, false);
    }
    _pressedNode[Primary] = nullptr;
}
```

**次键的 press 不在这里手写**——那样就得顺着写一条 release、再写一条 click，三份边沿
判定分家。实际做法是合成一份"有效按下状态"，让**同一个** press 循环去派发：

```cpp
PointerDownState effective = _pointerDown;
if (_longPress.held && _pointerDown.primary) {
    effective.secondary = true;    // 手指还按着 ⇒ 虚拟次键就还按着
}
// …… 循环用 effective 判边沿，末尾 `_pointerWasDown = effective;`
```

于是手指抬起那一帧 `effective.secondary` 自动变 false，`justReleased(Secondary)`
照常触发——不需要为长按另写"抬起"路径。`_longPress.held` 在次键的 release 分支里结清。

**为什么这样能"不误发左键 click"**：接下来手指抬起时，`_pointerDown.primary` 从 true 变 false，
但 `_pressedNode[Primary]` 已经是 `nullptr` ⇒ `justReleased` 那一支里 `pressed == nullptr`，
什么都不发。这是**既有语义的直接结果**（"捕获没了就不算点击"），不是新加的旁路。

对比"抑制标志"方案：那需要一条 `_suppressPrimaryClick` 从窗口层传到场景、再在每个
`justReleased` 分支里判一次，且任何将来新增的"抬起路径"都得记得判它——正是本仓库反复
避免的"第二份真相"。

**时序**：`TickLogic` 跑在 `timeline.Advance()`（`stepFrame` 第 3 步），即 `DispatchPointer`
（第 2b 步）**之后**。所以长按成立到次键按下最多晚一帧（≈16ms，阈值 500ms 的 3%）。
**不要**为此把 `TickLogic` 提前——那会改变所有逻辑步与渲染的相对位置（整仓回归面）。

时序表（触摸长按）：

| 时刻 | 窗口层 | 场景层 | 组件收到 |
| - | - | - | - |
| t=0 | `FINGER_DOWN`：`primary=true, source=Touch` | `DispatchPointer`：`justPressed(Primary)` | `Press(Primary,true)` |
| t=0.5s | 无事件 | `TickLogic`：`fired` / `pending` | — |
| t=0.516s | 无事件 | `DispatchPointer` 消费：作废主键槽、抓次键槽 | `Press(Primary,false)` → `Press(Secondary,true)` |
| t=0.8s | `FINGER_UP`：`primary=false` | `justReleased(Secondary)`，`stillOnIt` | `Press(Secondary,false)` → `Click(Secondary)` |

### 4.4 三个必须同时改的耦合点

这三条单独看都"不是右键的事"，但漏掉任何一条，触摸长按就是**静默不工作**：

1. **关掉触摸→鼠标合成**。`InkingWindow::Open`（`InkingWindow.cpp:1641-1660`）在
   `SDL_Init` 之后、`SDL_CreateWindow` 之前加：
   ```cpp
   SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
   ```
   现在库内**一次都没设**（全仓只有自检里设过 `AUDIO_DRIVER` / `VIDEO_DRIVER`）。
   默认开启时，一次触摸会同时产生 `FINGER_*` 和 `MOUSE_*` 两套事件：`leftButtonDown`
   被两条路驱动、`source` 在 Touch/Mouse 之间抖动 ⇒ 长按计时被反复复位（永远不成立）。
   **这是本方案最容易踩的一条**，因为现象是"长按没反应"，而不是报错。
2. **`inside` 判据补输入焦点**（`InkingWindow.cpp:468-470`）。现在只看
   `SDL_WINDOW_MOUSE_FOCUS`；禁掉合成后，纯触摸不再产生鼠标事件，这个位可能永远不置位
   ⇒ `inside=false` ⇒ `hit` 直接是空结果 ⇒ 手指按哪儿都"没命中"。
   ```cpp
   const SDL_WindowFlags flags = SDL_GetWindowFlags(s.window);
   const bool inside = (flags & (SDL_WINDOW_MOUSE_FOCUS | SDL_WINDOW_INPUT_FOCUS)) != 0
                    || mouse.IsTouchActive();
   ```
   第三条 `IsTouchActive()` 是保险：手指能按在窗口上，本身就证明指针在窗口里。
3. **两个槽的生命周期**（`InkingScene.cpp:499-504`）。`RemoveDrawNode` 只清了
   `_pressedNode` 一处；分槽后必须把 `_pressedNode[Secondary]` 一起清，否则"右键按下时
   删掉那个节点"就是野指针。`DetachSceneFromNodes`（`:524`）同理。
   这是 `AGENTS.md` §6 第 18 条"生命周期成对处理"的直接适用。

### 4.5 `SetPointerState` 的签名与 source 锁定

```cpp
void SetPointerState(float designX, float designY, const PointerDownState& down,
                     bool inside, PointerSource source) noexcept;
```

`source` **不能每帧直接赋值**：触摸屏上用户可能同时动鼠标，`MouseInput` 的
`state.source` 会被鼠标移动事件刷成 `Mouse`，长按计时就断了。规则：

```cpp
// 来源只在**按下那一刻**锁定，按着期间不再变（不变量：一次按下一个来源）。
if (down.primary && !_pointerDown.primary) {
    _pointerSource = source;
}
```

同一个"按下瞬间"里还要按 §3.4 的循环写 `_pressOrigin`——两件事同源（都是"这次按下从哪来、
从哪开始"），分开写必然有一处漏掉。

"变了没"的判据整包比较：

```cpp
const bool moved = (designX != _pointerX) || (designY != _pointerY)
                || (down != _pointerDown) || (inside != _pointerInside);
```

`down != _pointerDown` 走 `PointerDownState::operator==`，右键按下也会置脏 ⇒ 动态组件
（定点浮窗）能收到"右键点了外部"的通知。

新增只读查询：`bool IsSecondaryPointerDown()`；`GetPointerDown()` 返回整包
（`IsPointerDown()` 保持"主键按着没"，语义不变，`PointerProbe` 的调用点改读整包）。

---

## 5. 组件迁移表（T-03）

8 处重写点的**具体右键行为**——这不是机械改签名，每处都要一个决定：

| 文件:行 | 组件 | `Primary` | `Secondary` |
| - | - | - | - |
| `InkingStaticButton.cpp:292-302` | 按钮 | 原样 | **不进三态**；`TriggerContextClick(x,y)`（坐标 = 按下点，直接喂 `OpenAt`） |
| `InkingStaticImage.cpp:238` | 图片 | 原样（定格拖动模式） | 忽略 |
| `InkingStaticPanel.cpp:541` | 容器 | 原样（滚动 / 拖动） | 忽略 |
| `InkingTextInput.cpp:519` | 输入框 | 原样（开始编辑 / 选区） | 忽略（将来的"右键粘贴"入口） |
| `ListView.cpp:444` | 列表 | 原样 | 忽略 |
| `MenuBar.cpp:354` | 菜单栏 | 原样（无边框拖动） | 预留：右键点标题弹出该菜单（本版不做） |
| `InkingDynamicButton.cpp:243` | 动态按钮 | `r.primary.clicked` | `r.secondary.clicked` → `TriggerContextClick`（坐标读 `GetPressOrigin`） |
| `InkingFixedPopup.cpp:267` | 定点浮窗 | 原样 | `anyDownEdge` 参与"点外部关闭" |

`InkingStaticPanel::onPointerPress` 里 `_dragAxis = 0` 那条"抬起"分支必须**只在 Primary**
执行——否则右键抬起会把正在进行的左键滚动拖动结束掉。

---

## 6. 备选方案与取舍

| | A. 统一按钮维度（**推荐**） | B. 平行右键钩子 | C. 窗口层合成"虚拟右键" |
| - | - | - | - |
| 做法 | 钩子加 `PointerButton` 参数 | 另加 `onPointerSecondaryPress/Click` | `MouseInput` 内部把长按直接置成 `rightButtonDown` |
| 破坏性 | **有**（8 处重写点 + 消费者代码） | 无 | 无（场景只看到右键按了） |
| 通道数 | 1 套 | 2 套（加中键就是 3 套） | 1 套 |
| 长按判定位置 | 场景（可替换策略） | 场景 | 窗口层（`MouseInput` 变策略引擎，违背其头注释） |
| 左键误发 click | 天然不发（捕获已作废） | 天然不发 | **必须加抑制旁路**，且窗口层要伪造"抬起→再按下" |
| 无头自检 | 直接 `TickLogic(0.5)` 可验 | 同 A | 要构造 SDL 事件或暴露私有状态，难验 |
| 加中键 | 加一个枚举值 | 再加一套钩子 | 窗口层增加分支 + 场景仍单槽（左右同按又会踩） |

选 A 的核心理由：**右键不是特例，是缺失的维度**。B 把维度问题当特例处理，第三个按钮出现时
要重来一遍；C 把策略塞进"只维护状态"的层，并且必然引入抑制旁路（§4.3 已论证为什么不要）。

代价承认：A 是 breaking。缓解是 `0.x` 阶段 + 全仓 8 处重写点都在本仓库内 +
CHANGELOG 明确标出迁移方法（"重写时加 `if (button != PointerButton::Primary) return;`"）。

---

## 7. 验收

### 7.1 自检断言（`selftest.cpp` 新增，全部无头可跑）

1. 静态按钮右键按下 → `Press(Secondary,true)`；抬起 → `Press(Secondary,false)` + `Click(Secondary)`
   各 1 次，`Click(Primary)` **0 次**，`GetState()` 全程 `Normal`（不进 Pressed）。
2. 静态按钮左键点击 → `Click(Primary)` 1 次，`Click(Secondary)` 0 次（回归）。
3. 左右同按再依次抬起 → 两个键各收到成对的 `press(true/false)`，无丢失、无越界
   （这是分槽的存在理由，必须钉住）。
4. 右键不改变 `GetHoveredNode()`，也不改变 `GetKeyboardFocus()`。
5. 触摸轻点：`down(Primary, Touch)` → `TickLogic(0.3)` → `up` ⇒ 只有 `Click(Primary)`。
6. 触摸长按：`down(Primary, Touch)` → `TickLogic(0.5)` → `DispatchPointer()`
   ⇒ 主键 `press(false)` 无 click，次键 `press(true)`；再 `up` ⇒ `Click(Secondary)` 1 次。
7. 触摸长按前移动超 slop：`down` → 移动 20 设计像素 → `TickLogic(1.0)` ⇒ 次键**零事件**。
8. `source` 抖动：`down(Primary, Touch)` → 下一次 `SetPointerState` 传 `Mouse`（仍按着）
   → `TickLogic(0.5)` ⇒ 长按仍成立（证明 source 锁定）。
9. **坐标 = 按下点**（鼠标右键）：在 (300,200) 按下、指针移到 (320,210) 再抬起
   （仍在该节点内）⇒ `Click(Secondary)` 收到的坐标是 `(300,200)`，不是 `(320,210)`。
   触摸版是断言 6 的变体：长按成立后手指微动 3 px 再抬起，坐标仍是按下点。

### 7.2 手工验收

`inking_demo` 里：在"打开菜单"按钮的**任意位置**右键 ⇒ 菜单在**按下点**弹出（不是按钮左下角，
也不是抬起点）；右键点空白 ⇒ 菜单关闭且不重开；右键点时菜单内部的项 ⇒ 菜单不关。
触摸屏上：轻点 = 左键点击；按住 0.5s 不动 = 菜单在按下点弹出；按住后滑动 = 拖动 / 滚动，
不弹菜单。

---

## 8. 落地顺序

| 批 | 内容 | 硬门槛 |
| - | - | - |
| a | T-01 ~ T-04（契约 + 迁移 8 处 + `PointerProbe`） | **一次编译通过**。这一批是纯接口改造，行为不变；跑完现有全部自检必须仍然全绿 |
| b | T-05 + T-06（窗口层喂状态 + 场景分槽，**不含长按**） | 右键真的能到组件；左键全套回归绿 |
| c | T-07（长按） | 断言 5~9 绿 |
| d | T-08 + T-09（示例 / 文档 / CHANGELOG） | 手工验收通过 |

`a` 批为什么必须"一次编译通过"：签名变更后全仓必然编译失败，中间态不可维护。按
"契约头 → 8 处重写点 → 调用点"顺序改完再编译一次。

---

## 9. 明确不做（避免下一轮返工）

1. **中键派发**。`MouseState.middleButtonDown` 继续只记状态。要开的时候：`PointerButton`
   加 `Middle` + `kPointerButtonCount = 3`，`PointerDownState` 加字段，`static_assert` 会先报出来。
2. **右键拖动**。`_lastDrag*` 不分槽，右键不产生 `onPointerDrag`。桌面右键拖拽是少见手势，
   开它要连带解决"右键拖动与菜单关闭的竞争"，收益不划算。
3. **触摸多指的右键**。仍只认第一根手指（`MouseInput.h:99-105` 的结论不变）。
4. **长按的震动 / 视觉反馈**。需要平台 API，不在输入层。
5. **笔（Pen）事件**。SDL3 有 `SDL_EVENT_PEN_*`，`PointerSource` 预留了扩展位，本轮不接。
6. **`MenuBar` 的右键弹菜单**。组件能力留给下一版（`MenuBar` 的菜单是自绘下拉，接右键要
   先决定"右键标题是否等价左键"）。

---

## 10. 风险清单

| 风险 | 表现 | 判据 / 兜底 |
| - | - | - |
| 触摸合成没关 | 长按永远不成立，轻点偶尔重复 | 断言 8（source 抖动）+ 断言 6 双保险 |
| `inside` 判据没改 | 触摸全哑（点哪儿都无命中） | 手工：触摸屏轻点按钮无反应；断言 5 |
| `RemoveDrawNode` 漏清次槽 | 右键按下时节点被删 → 野指针，debug/release 表现不同 | 代码评审逐条比对；断言 3 的变体（按下删节点） |
| 右键进了三态 | 按钮被右键点亮，像"左键生效了" | 断言 1 的 `GetState()` |
| 长按阈值被 `TickLogic` 停摆影响 | 停放 / 关闭场景不长按（正确）；静默场景仍长按（正确） | `TickLogic` 已有 `_rejected/_closed` 早退，语义正好 |
| 消费者代码编译失败 | 重写 `onPointerPress(bool)` 的派生类报"不是 override" | CHANGELOG 给出迁移样板；`override` 关键字保证**编译期**暴露而不是运行期 |

---

## 11. 已拍板（原"待确认"两条，2026-10-24 用户裁定）

1. **阈值调参：暂不处理**。`0.5f` / `8.0f` 先落地，能力成立即可；真机手感留到有触摸设备时再定，
   **不阻塞**。派生场景要改就改 `protected` 常量。
2. **`contextClicked` 带坐标：是**。理由不是"方便"，而是右键的业务语义就是"在这一点的上下文"——
   `OpenAt(x, y)` 的入参没有第二处来源。坐标取**按下点**（不变量 7），与拖动基准、长按 slop
   基准共用同一份 `_pressOrigin`。

由此确立一条更高的原则，写在这里供以后判事：**"统一输入"不等于"按钮形状统一"，而是
"两种输入状态在所有层同构"**——同构包括事件集（press / release / click）、坐标、捕获规则。
凡是只给其中一种状态提供能力（坐标、拖动、快捷键）的设计，都违背这个策略。

---

## 12. 落地记录（实现时与方案的偏差）

六处，都是写代码时才暴露的，其中第 1、5 条是会**静默出错**的那种：

1. **`LongPressState` 的字段换了**：方案是 `{tracking, fired, seconds}`，实现是
   `{resolved, held, seconds}`。
   - `tracking` 不需要——手指一抬整体复位，`seconds` 自然从 0 开始；
   - `fired` 拆成两个：`resolved`（本次按下**已有结论**：转义或放弃）+ `held`
     （虚拟次键按着）。
   - **为什么必须拆**：只用一个 `fired` 的话，"移动超 slop 放弃"之后手指还按着，
     下一次 `TickLogic` 看到 `!fired` 就重新计时，500ms 后照样弹菜单——
     "按住拖到别处松手，居然开菜单"。这个 bug 不报错，只在手感上表现为"怪"。
2. **次键的 press 不手写**：方案里是"作废主键 + 手动发次键 press"。实现改成合成
   `effective`（`held && primary`），让**同一个** press 循环派发。否则要顺着写一条
   release、再一条 click，三处边沿判定分家——正是 `PointerProbe` 存在的理由。
3. **阈值参数从 `static constexpr` 改成成员**：`constexpr` 的值编译期定死，
   派生类改不动基类 `TickLogic` 里用的那个，"派生场景可改"会是一句空话。
4. **`SetPointerState` 保留了一个 `bool` 便捷重载**（只喂主键）：自检 9 处旧调用
   零改动，且语义明确。这不是"两套入口"——它转发到完整版本，只有一处定义。
5. **长按的**命中**也要用按下点**（方案只改了坐标）：`QueryHit(_pointerX, _pointerY)`
   在 slop 内微动跨过节点边界时会命中邻居，表现是"菜单弹在别处"，而坐标却是对的。
6. **断言写 `!= Pressed` 而不是 `== Normal`**：指针在按钮上时状态是 `Hover`，
   按 `== Normal` 写会误报（第一版就是这么红的）。
7. **多了一份 `_effectiveDown`（物理状态 + 长按合成），并强制一次动态通知**。
   方案里只写了"作废主键 + 次键按下"，没考虑**动态组件怎么知道长按发生了**：
   它读的是 `GetPointerDown()`，若返回物理状态，次键永远是 false ⇒ 定点浮窗
   永远收不到"长按点在外部"⇒ 菜单关不掉。修法两步：
   - `GetPointerDown()` / `IsSecondaryPointerDown()` 返回 `_effectiveDown`；
   - 长按成立时置 `_pointerDirty = true; _pointerNotified = false;`——长按要求
     "手指不动"，`SetPointerState` 算不出 `moved`，不强制这一下就没有通知。
   这条不修的话，**右键关菜单正常、长按关菜单永远不生效**，而两者在组件层
   本该没有区别。

自检覆盖面（22 条）：右键 press/release/click 与"不进三态"、左键回归、左右同按分槽、
右键不改 hover 与焦点、click 坐标是按下点、触摸轻点、触摸长按整趟、长按移动取消、
取消后不迟到、来源抖动、长按坐标、**长按对动态组件可见**。全部无头跑
（`TickLogic` 直接推进）。

