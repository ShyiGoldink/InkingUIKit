# 任务书：动态组件的滚轮（拉通道已通，缺消费方）

> 状态：**待执行**（本文件按任务逐条跟踪，完成规则见 §1）
> 日期：2026-11-04
> 来源：`TaskGuide-G.md:621` 登记的框架级待办（G-07 明确"动态面板不做滚轮"）；
> 核实结论与探针读数见 §2
> 目标版本：`0.9.1`（a 铺路 / b 消费与断言 / c 收口）
> 涉及层：`scene/`（只读查询）· `panel/`（消费）· `dataStruct/`（换算共用）· `examples/demo/` · `docs/zh/`

---

## 0. 结论（TL;DR）

登记的待办说"动态档没有可靠的滚轮读取时机"。**探针把这句话拆成两半**：读不到的那一半是
**逻辑步**（喂 30.0 只读到 20.0），而"每渲染帧一次"的 `onAnimationTick` **完全可靠**
（60 帧里调用 60 次、3 次喂入全部读到、累计 30.0 一格不差）。所以这件事不是"要改框架语义"，
是"没人消费 + 缺一条归属判据"。拆成 5 个任务、3 批。

| 编号 | 任务 | 批次 | 对应来源 | 落点 | 代价 |
| - | - | - | - | - | - |
| T-01 | 给场景加滚轮归属查询 | 0.9.1-a | §2 第 6 条 | `include/scene/InkingScene.h` + `src/scene/InkingScene.cpp` | 低 |
| T-02 | 把滚轮换算抽成一份 | 0.9.1-a | §2 第 4 条 | `include/dataStruct/InkingScroll.h` + `InkingStaticPanel.cpp` | 低 |
| T-03 | 让动态面板消费滚轮 | 0.9.1-b | `TaskGuide-G.md:621` | `src/panel/InkingDynamicPanel.cpp` | 中 |
| T-04 | 滚轮自检 `CheckDynamicPanelWheel` | 0.9.1-b | 惯例（DoD 第 2 条） | `examples/demo/selftest.cpp` | 中 |
| T-05 | 文档、CHANGELOG、版本收口 | 0.9.1-c | 惯例 | `docs/zh/` + `include/inking.h` | 低 |

两条不变量贯穿全部任务：

1. **两档滚同一格必须滚同一个距离。** 好手感不该分档；一旦分叉，用户看到的就是
   "静态面板滚 48、动态面板滚 60"，而这不写在任何文档里。
2. **归属唯一：一帧的滚轮只落在一个接收者身上。** 双滚（推通道一次 + 拉通道一次）比
   不滚更难查——现象是"滚得快一倍"，而没人会先怀疑这个。

---

## 1. 任务维护规则（本文件的写法约定）

### 1.1 每个任务必须写三段

| 段 | 要求 |
| - | - |
| **准备怎么改** | 决策与选型：做什么、走哪条路、为什么；**放弃的候选要写出来** |
| **大致改动逻辑** | 代码级顺序：改哪个文件、加哪个函数、谁调谁、哪一步不能反。不贴大段代码 |
| **与改动前对比** | 表格；**至少一行"改动后仍不变"的边界**，否则是过度承诺 |

另附 **落点**、**验收**（可断言）、**风险与回退**（至少各一条）。

### 1.2 完成即压缩（核心规则）

任务完成后，把该任务整段压缩成一行：

```
- T-01 <标题>-已完成（<YYYY-MM-DD>，<一句可验证的结论>）
```

删除「准备怎么改 / 大致改动逻辑 / 与改动前对比 / 验收 / 风险」五个细目，并把 §6.2 进度表
的状态改为 `已完成`。**两处必须同改。**

### 1.3 四种状态

| 情况 | 写法 |
| - | - |
| 完成 | `- T-01 <标题>-已完成（日期，结论）` |
| 放弃 | `- T-01 <标题>-已放弃（原因；替代方案）` |
| 拆分 | `- T-01 <标题>-已拆分为 T-01a / T-01b`，各自走完整三段 |
| 阻塞 | `状态：阻塞（原因）`，保留全部细目，不压缩 |

### 1.4 完成的定义（DoD）

1. 编译通过（`inking_strict` 的 0 warning 门也要过）；
2. 验收断言在 `examples/demo/selftest.cpp` 里跑绿（不是临时探针）；
3. 涉及文档同步改；
4. 「与改动前对比」每行都能对上实测。

---

## 2. 基线事实（核实结论）

探针 `build/_wheelprobe/main.cpp`（不进仓，`build/` 已忽略）：一个动态节点，场景按
`stepFrame` 的顺序手工驱动 60 帧（60fps 渲染 / 50Hz 逻辑），第 10/11/12 帧各喂
`SetPointerWheel(0, 10)`，在 `onAnimationTick` 与 `onTick` 两处读 `GetPointerWheelY()`。

| 条目 | 判定 | 证据 |
| - | - | - |
| 登记的"逻辑步读不可靠" | **成立** | 探针：喂 3 帧 × 10.0 = 30.0，`onTick` 读到 **2 次、累计 20.0**（差额 10.0 —— 有一帧滚轮被帧末清掉了）；61 帧里 **11 帧逻辑步为 0** |
| 登记的"动态档没有可靠读取时机" | **不成立** | 探针：`onAnimationTick` **调用 60 次 / 60 帧**，3 次喂入**全部读到**，累计 **30.0 == 喂入量** |
| `onAnimationTick` 位于"派发之后、清账之前" | 成立 | `InkingWindow.cpp:1100-1177` 的顺序；`Timeline.cpp:72` 每帧回调恰好一次；`InkingScene.cpp:279-330` 遍历绘制列表 |
| `GetPointerWheelX/Y` 有消费方 | **不成立（零调用）** | `grep -rn GetPointerWheel src/ include/ examples/` 只剩声明与注释 |
| 静态档的换算 | 成立 | `InkingStaticPanel.cpp:447-462`：`nextY -= deltaY * wheelStep`，`wheelStep` 默认 `48.0f`（`InkingScroll.h:43`） |
| `_wheelDispatched` 表示"有人接了" | **不成立** | `InkingScene.cpp:1727-1729` 先置位、再判 `_pointerInside` 找目标 ⇒ 它只表示"这一帧进过派发流程"，**没有接手的记录** |
| 动态面板 `SetScroll` 不标命中脏是对的 | 成立 | `InkingDynamicPanel.cpp:304-323`：子孙不进命中表、命中递归实时问；洞按滚动范围扩过 ⇒ 滚动不产生表重建 |
| 绘制列表反序 == 最上面 | 成立 | `InkingScene.cpp:786-812`：painter 序（父先于子、兄弟内 z 升序） |
| 动态面板已重写 `onAnimationTick` | 成立 | `InkingDynamicPanel.cpp:460-475`（滚动条外观过渡），滚轮消费加在同一处即可 |
| `HandlesSubtreeWheel()` 对动态面板"没有意义" | **部分成立**：当时没有消费方；接上消费方后它是"静态子孙 → 面板"的唯一入口 |
| 自检里"一条滚轮断言都不该有" | **部分成立**：理由是"偶发丢失"，而那个理由只对逻辑步成立 | `examples/demo/selftest.cpp:7449` |

---

## 3. 第一批：0.9.1-a（铺路：两个只读查询 + 一份换算）

### T-01 给场景加滚轮归属查询

- **状态**：未开始
- **落点**：`include/scene/InkingScene.h`（公开区 + 私有成员）· `src/scene/InkingScene.cpp`
  （`DispatchPointer` 的滚轮段 `:1720-1745`、`RemoveDrawNode:700-735`、`ConsumeFrameDirty:648-655`）

**准备怎么改**

动态面板要回答两个问题才能消费滚轮："这一帧的滚轮是不是已经有人接走了？""指针底下最上面的
动态节点是不是我？"两个答案都只有场景知道。

| 方案 | 做法 | 代价 | 结论 |
| - | - | - | - |
| A | 只加 `IsPointerWheelDispatched()`（暴露 `_wheelDispatched`） | 低 | **不够**：那个标志表示"进过流程"，没人接也是真（§2 第 6 条） |
| B | 加 `GetPointerWheelTarget()`（存下派发对象）+ `GetTopmostDynamicAt(x,y)` | 低 | **推荐**：两个查询各自回答一个独立问题，且都是只读，行为不变 |
| C | 把动态档放进命中表，让框架统一派发 | 高 | 不做：那是动态档的地基（几何每帧变 ⇒ 每帧重建表） |
| D | 反过来让场景给动态档发"滚轮通知" | 中 | 不做：多一条通道就多一处"谁先到"的时序问题，而拉通道已经证明可靠 |

**大致改动逻辑**

1. `InkingScene` 加私有成员 `InkingWidget* _wheelTarget = nullptr`（与 `_hoveredNode` /
   `_pressedNode` 同一族的瞬时指针）。
2. 滚轮派发段：找到 `target` 之后记 `_wheelTarget = target`（**只在真的找到目标时记**，
   没找到留空——这正是与 `_wheelDispatched` 的差别）；`ConsumeFrameDirty` 里与
   `_wheelX/_wheelY` **一起**清空（漏了就是跨帧野指针）。
3. `RemoveDrawNode` 的清理块（`:700-735`）加一格 `if (_wheelTarget == node) _wheelTarget = nullptr;`
   ——那一块已经按"槽"数了一遍（悬停 / 两个按键槽 / 键盘焦点），这是第五个槽。
4. 加两个公开只读查询：
   - `InkingWidget* GetPointerWheelTarget() const noexcept`；
   - `InkingWidget* GetTopmostDynamicAt(float designX, float designY)`：先 `sortDrawList()`，
     再**从列表尾往前**找第一个 `IsDynamic() && IsVisibleInTree() && HitTest(x, y)`。
5. **不能反的一步**：`GetTopmostDynamicAt` 必须在 `sortDrawList()` 之后遍历。不排序时列表
   是"上一次构建的顺序"，拿它判最上面就是拿旧 z 判新层级——错得安静，只在改过 z 之后现形。

**与改动前对比**

| 场景 | 改动前 | 改动后 |
| - | - | - |
| 滚轮派发给静态目标 | 派发，无人可查 | 派发，`GetPointerWheelTarget()` 返回它（**行为不变**） |
| 滚轮没人接 | `_wheelDispatched` 仍为真 | `GetPointerWheelTarget()` 返回 `nullptr`（**新增可观测**） |
| 绘制 / 命中 / 帧顺序 | — | **不变**：两个查询都不标脏、不改状态 |
| 目标节点在帧内析构 | 已有清理逻辑不涉及它 | 新增第五个槽一起清（**这格漏了就是野指针**） |

**验收**：`CheckDynamicPanelWheel()` 里三条 —— ① 指针外喂滚轮 ⇒ `GetPointerWheelTarget()`
为 `nullptr`；② 指针落在静态按钮上喂滚轮 ⇒ 返回那个按钮；③ 两个重叠动态面板时
`GetTopmostDynamicAt` 返回**后建**的那个。

**风险与回退**：风险是新增瞬时指针的清理漏格（析构后仍被读）。回退就是删掉两个查询与
一个成员，无外部影响（都是新增 API，没有改动既有签名）。

---

### T-02 把滚轮换算抽成一份

- **状态**：未开始
- **落点**：`include/dataStruct/InkingScroll.h`（新增自由函数）·
  `src/panel/InkingStaticPanel.cpp:447-462`

**准备怎么改**

两档滚同一格必须滚同一个距离（不变量 1）。现在的换算埋在静态面板的 `onPointerWheel` 里，
动态档要用只能抄——**抄一份就是第二个真相**，将来改了步长只会改一处。

| 方案 | 做法 | 代价 | 结论 |
| - | - | - | - |
| A | 抽 `InkingApplyWheelToScroll(const InkingScrollSpec&, dx, dy, float& x, float& y)` | 低 | **推荐**：纯函数、无状态，两档同调 |
| B | 动态档抄六行 + 双档一致性断言 | 低 | 退路：断言能红，但错误在写的时候就发生了 |
| C | 抽成基类虚函数 | 中 | 不做：两档没有共同基类，为此造一个 `ScrollLike` 是把"暂时相同"当契约 |

**大致改动逻辑**

1. 自由函数放 `InkingScroll.h`（与 `InkingClampScroll` / `InkingScrollRange` 同一族），
   签名收 `const InkingScrollSpec&` 与两个 `float&`；`horizontal` / `vertical` 两个开关的
   处理照抄现状（**方向不能反**：`deltaY > 0` 看更上面 ⇒ 滚动量减）。
2. `InkingStaticPanel::onPointerWheel` 改成"取 `_scrollX/_scrollY` → 调它 → `SetScroll`"。
3. 相对位置：`SetScroll` 仍然负责夹紧与幂等，函数**不夹紧**——夹紧只该有一处，否则
   "夹到什么范围"会有两个答案（内容视口要不要扣掉滚动条）。

**与改动前对比**

| 场景 | 改动前 | 改动后 |
| - | - | - |
| 静态面板滚一格 | `-48` | `-48`（**数值不变**，`CheckScroll` 既有断言一条不改） |
| `horizontal=false` | 忽略 `deltaX` | 忽略 `deltaX`（**不变**） |
| `scroll.enabled=false` | 早退 | 早退（**不变**，判在调用方） |
| 越界滚 | `SetScroll` 夹紧 | **不变**（函数不夹紧） |

**验收**：`CheckScroll` 既有断言全绿（一条不改）；新增一条直接测函数：`spec.wheelStep=48` 时
`dy=+1` ⇒ `y` 减 48、`dx=-1` 且 `horizontal=true` ⇒ `x` 加 48。

**风险与回退**：风险是抽函数时把"扣掉滚动条"误算进去（那会让滚动距离依赖条在不在）。
回退：函数内不出现 `ContentViewport*`；出问题把静态档改回内联、动态档暂缓 T-03。

---

## 4. 第二批：0.9.1-b（消费与断言）

### T-03 让动态面板消费滚轮

- **状态**：未开始
- **落点**：`include/panel/InkingDynamicPanel.h`（关键决定二整段改写 + `HandlesSubtreeWheel`
  覆写）· `src/panel/InkingDynamicPanel.cpp:460`（`onAnimationTick`）

**准备怎么改**

把"不做滚轮"翻案，但**只在有把握的那条通道上做**：每渲染帧一次的 `onAnimationTick`。
逻辑步仍然不读（探针证明它会丢），这条写进注释，免得后人"顺手优化"回去。

归属判据（不变量 2）用 T-01 的两个查询拼出来，顺序不能反：

1. `_data.scroll.enabled` 为假 ⇒ 直接返回（最便宜的判断放最前）；
2. `GetPointerWheelY()/X()` 都为 0 ⇒ 返回；
3. `GetPointerWheelTarget() != nullptr` ⇒ 返回。**这一条必须先于自判**：指针底下压着一个
   静态节点时，推通道已经把它交给"最近的声明者"了；若那个声明者是本面板，它已经收到
   `PushPointerWheel`，再拉一次就是双滚；
4. `GetTopmostDynamicAt(pointerX, pointerY) != this` ⇒ 返回（重叠的动态面板只有最上面那个滚）。

`HandlesSubtreeWheel()` 覆写成 `true`：让"指针压在面板的静态子孙上"那一档也归本面板
（否则那段区域的滚轮会被子控件链吞掉，手感上就是"滚到有按钮的地方就停"）。

| 方案 | 做法 | 代价 | 结论 |
| - | - | - | - |
| A | 上面四步判据 + `onAnimationTick` 消费 | 中 | **推荐**：读取时机已被探针证明，归属只有两条判据 |
| B | 只在 `HandlesSubtreeWheel` 里接收（不做自判） | 低 | 不够：面板的空白区没有静态目标，推通道到不了 |
| C | 在 `onTick`（逻辑步）里读 | 低 | 不做：探针实测会丢 1/3 的喂入量 |
| D | 由场景主动推给动态档 | 中 | 不做：见 T-01 的方案 D |

**大致改动逻辑**

1. 头文件：`class InkingDynamicPanel` 里加 `bool HandlesSubtreeWheel() const noexcept override;`
   并重写顶部"关键决定二"整段——原文的理由（逻辑步会丢）保留，但要写清"每帧回调不丢"
   以及"为什么仍然不在逻辑步读"。
2. 实现 `HandlesSubtreeWheel()` 返回 `_data.scroll.enabled`（不能滚的面板不该截滚轮，
   否则它上面的一切滚轮都静默消失）。
3. `onAnimationTick` 开头插入消费段（在 `_look.Advance` **之前**：本帧的条位置要用新的
   `_scrollY`）；消费时调 T-02 的函数 + `SetScroll`。
4. 收到 `PushPointerWheel` 时走同一条路径（覆写 `onPointerWheel`，与静态档同签名），
   内部同样调 T-02 的函数 + `SetScroll` —— 两处**只有一份算术**。
5. **不能反的一步**：判 `GetPointerWheelTarget()` 必须在自判 `GetTopmostDynamicAt` 之前。
   反过来的话，指针压在静态子控件上时两条通道都会执行——双滚，而且只在"面板里有按钮"时出现。

**与改动前对比**

| 场景 | 改动前 | 改动后 |
| - | - | - |
| 指针在动态面板空白区滚一格 | 不动 | 滚 `wheelStep`（48 设计像素） |
| 指针在面板里的静态按钮上滚 | 不动（被按钮链吞掉） | 面板滚 48，**一次** |
| 一次喂入被逻辑步/渲染帧各读一次 | — | 只读渲染帧 ⇒ 只有一次 |
| 滚到顶 / 底 | — | 夹紧在 `[0, range]`，**不累加**（`SetScroll` 的既有行为） |
| 面板 `scroll.enabled=false` | 不滚 | **不变**，且 `HandlesSubtreeWheel` 返回假 ⇒ 不截滚轮 |
| 静态面板 | 滚 48 | **不变**（T-02 抽函数时逐字保留） |
| 两个动态面板重叠 | 都不滚 | 只有上面那个滚 |

**验收**：见 T-04 的断言清单（同批，消费与断言互为依据，拆开无法验收）。

**风险与回退**：
- 风险 1：双滚。回退：把 `onAnimationTick` 里那段消费整体删掉，功能退化成"只认静态子孙"
  （方案 B），不伤其他行为。
- 风险 2：`HandlesSubtreeWheel` 返回真之后，面板里的静态子控件再也滚不动自己的滚动条
  （嵌套两层滚动容器）。这一条**本轮不解决**（见 §7），但断言里要钉住"只有一层时不串台"。

---

### T-04 滚轮自检 `CheckDynamicPanelWheel`

- **状态**：未开始
- **落点**：`examples/demo/selftest.cpp`（新函数 + `main()` 调用点；紧邻 `CheckDynamicPanel`）

**准备怎么改**

单独一个函数而不是塞进 `CheckDynamicPanel`：那个函数已经 300 行，再加滚轮会让"红了跑哪一条"
变成考古（`probe_template/main.cpp` 顶部那条判据）。场景自己建一份，不共享。

**大致改动逻辑**

1. 建 200×200 视口 / 300 内容高的动态面板（`scroll.enabled`），`UpdateScrollRange()`；
   再建一个同配置的静态面板做对照。
2. 每条断言都走**真路径**：`SetPointerState` → `SetPointerWheel` → `DispatchPointer` →
   `TickFrame(1/60)` → `ConsumeFrameDirty`（顺序照抄 `stepFrame`，别直接调 `SetScroll`）。
3. 断言（七条）：① 空白区喂一格 ⇒ `GetScrollY()` 减 48；② 静态面板同喂一格 ⇒ 也是 48
   （**两档同一个数**）；③ 只喂一次、连跑 3 帧 ⇒ 总量仍是 48（**不双滚**）；
   ④ 喂 100 格 ⇒ `GetScrollY() == GetScrollRangeY()`（夹紧，不越界）；
   ⑤ 指针移出面板再喂 ⇒ 不动；⑥ 指针落在面板里的静态按钮上喂 ⇒ 面板滚 48（`HandlesSubtreeWheel`
   接上了，且没滚两次）；⑦ 后建的第二个面板盖在前面 ⇒ 只有上面那个滚。
4. **不能反的一步**：每条断言前都要 `ConsumeFrameDirty()` 结清上一帧的滚轮，否则残留量会
   把下一条断言污染成"多滚了一格"——而症状看起来像是夹紧失效。

**与改动前对比**

| 场景 | 改动前 | 改动后 |
| - | - | - |
| 自检总数 | 889 条 | +7 条（实测后写进 CHANGELOG） |
| `CheckDynamicPanel` 既有断言 | 全绿 | **不变**（新函数独立建场景） |

**验收**：`selftest` 末行 **失败项 0**；`ctest` 全绿。

**风险与回退**：风险是断言写成"直接调 `SetScroll` 再断言"——那只能证明算术、证明不了入口接上
（G-07 踩过一次）。回退：断言红且实现看着对时，先怀疑探针顺序而不是实现。

---

## 5. 第三批：0.9.1-c（收口）

### T-05 文档、CHANGELOG、版本收口

- **状态**：未开始
- **落点**：`docs/zh/TaskGuide-G.md:621`（待办结案）· `docs/zh/api/InkingComponents.md` ·
  `docs/zh/CHANGELOG.md` · `include/inking.h:20` · `CMakeLists.txt`

**准备怎么改**

版本升 `0.9.1`，五处一致（`InkingVersion` / `project(VERSION)` / 文档页脚 / README 横幅 /
CHANGELOG 标题）——这是 T-08 那条清单，照抄。

**大致改动逻辑**

1. `TaskGuide-G.md` §7 那条"动态组件滚轮属框架待办"改成结案，指向本文件与 T-03；
2. `InkingDynamicPanel.h` 顶部"关键决定二"在 T-03 已改写，这里只核一遍它与文档口径一致；
3. `CHANGELOG` 新增 `0.9.1` 一节：新增（动态面板滚轮 / 两个场景查询 / 一个换算函数）、
   变更（`HandlesSubtreeWheel` 语义）、已知限制（嵌套两层滚动容器只有外层生效）；
4. `selftest.cpp:7449` 那段"这一组里一条滚轮断言都不该有"的注释改写——它现在**过时了**。
5. 版本五处同改。

**与改动前对比**

| 场景 | 改动前 | 改动后 |
| - | - | - |
| 自检启动行 | `版本 0.9.0-release` | `版本 0.9.1-release` |
| `TaskGuide-G` §7 | 一条待办 | 结案（带任务编号） |
| 历史文档（旧任务书 / CHANGELOG 历史节） | 原文 | **不变**：只加注不改写（§4 硬约束） |

**验收**：`selftest` 启动行打印 `0.9.1-release`；`grep -c "0.9.1"` 五处命中；`ctest` 全绿。

**风险与回退**：风险是只改了三处版本（历史坑：`inkingConfigVersion.cmake` 一直是 0.7.1）。
回退：按 T-08 的清单逐条 `grep` 复核。

---

## 6. 批次门槛与进度

### 6.1 门槛

| 批次 | 发布前必须满足 |
| - | - |
| 0.9.1-a | 构建 0 warning；`selftest` 失败项 0；`CheckScroll` 既有断言**一条不改**就过；`nm` 差分只多两个查询符号与一个换算函数 |
| 0.9.1-b | `CheckDynamicPanelWheel` 七条全绿；两档同一格滚同一个数；连跑 3 帧不双滚 |
| 0.9.1-c | 版本五处一致；`TaskGuide-G` 待办结案；`ctest` 全绿 |

### 6.2 进度表

| 编号 | 任务 | 批次 | 状态 |
| - | - | - | - |
| T-01 | 给场景加滚轮归属查询 | 0.9.1-a | 未开始 |
| T-02 | 把滚轮换算抽成一份 | 0.9.1-a | 未开始 |
| T-03 | 让动态面板消费滚轮 | 0.9.1-b | 未开始 |
| T-04 | 滚轮自检 `CheckDynamicPanelWheel` | 0.9.1-b | 未开始 |
| T-05 | 文档、CHANGELOG、版本收口 | 0.9.1-c | 未开始 |

完成任务后，本节状态改 `已完成`，§3～§5 对应段落按 **§1.2** 压缩成一行——两处必须同时改。

---

## 7. 明确不做（避免反复讨论）

| 不做的事 | 原因 |
| - | - |
| 在逻辑步（`onTick`）里读滚轮 | 探针实测：喂 30.0 只读到 20.0，且有 11/61 帧逻辑步为 0。日志说没问题、手感是"偶尔不滚" |
| 嵌套两层滚动容器"内层优先" | 需要按指针路径做逐层仲裁，且每层都要知道"子层已经消费了吗"。本轮只有一层时是对的，两层时外层生效——写进已知限制 |
| 把滚轮改成跨帧状态（不帧末清零） | 那会制造"切场景 / 移动鼠标后莫名滚一下"，与 `SetPointerWheel` 的既有契约冲突 |
| 动态档进命中表 | 几何每帧可变 ⇒ 每帧重建整表，等于把动态档的成本抬到静态档之上 |
| 滚轮的惯性 / 平滑滚动 | 要改时间轴（是一段动画而不是一次位移），属独立特性 |
| 让 `InkingDynamicPanel` 之外的动态控件也进这次改动 | 判据只有一个消费方就够了；第二个消费方出现再抽 `Like`（`AGENTS.md` §6 那条"三次法则"的镜像：抽早了全是猜） |
