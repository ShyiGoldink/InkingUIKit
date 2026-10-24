# 实测报告（UsageReport）

> ## ⚠ 本文对应版本 0.4.0（0.x 发布版）
>
> 报告描述的是 `0.4.0` 这一版**实际能做什么、代价多大、哪里不成立**。
> 接口仍会变；升级前请对照 `docs/zh/CHANGELOG.md`。

**重建说明**：本文件在本仓库中原本缺失，现按头文件注释里的引用点
（`§6.7`、`§7.4`、"B7 那段"、"预制件那一节"、"不成立"表）与代码行为复原。
标 `†` 的小节是**推断内容**（原始报告的章节结构与实测数据不可考）；文中所有
绝对耗时、帧率、内存数字都只能当量级看，**数值请以自己的实测为准**。

> 编号说明：为了兑现注释里的引用，本文保留 `§6.7` 与 `§7.4` 两个编号；
> 未被引用的相邻小节（`§6.1`–`§6.6`、`§7.1`–`§7.3`）没有原始材料，不复原（†）。

---

## 1. 结论摘要

| 结论 | 状态 | 一句话依据 |
| --- | --- | --- |
| 静态组件进烘焙命中表，动态组件不进表 | **成立** | `HitTable` + `rebuildHitTable()`；自检断言 `GetHitTableEntryCount() > 0` |
| 抽屉 / 面板开合不重烘命中表 | **成立** | 变换通道；容器只登记一条盖住子树的包络 |
| 同纹理连续绘制能合批 | **成立** | `BatchStats::textureDraws` 抓得住；批按纹理分界 |
| 静止帧复用顶点、只做平移 | **成立** | `VertexCacheSlot` 的键与 `vertexReuses` 计数 |
| 列表 1000 项也只建十几二十行 | **成立** | 窗口化 + 两头占位块；`GetBuiltRowCount()` |
| 单行文本框的光标左右移动 / 选区 | **成立** | `TextEditState::MoveLeft/MoveRight(select)` |
| 光标上下移动 + 跟随滚动 | **不成立（这版不做）** | 见 §7.6；`TextEditState.h:32` |
| 柔边阴影 / 真模糊 | **做不到（当前渲染路径）** | 阴影是偏移实心副本，见 §8 |
| 局部层级（A8b） | **成立** | 树序 + 兄弟内 z；父子/兄弟/跨子树三项都有自检断言，含真渲染像素断言 |
| 子节点逃出父级（portal） | **这版不做** | 子 z 再大也盖不过父级的兄弟；见 README「已知限制」4 |
| 多指针 | **这版不做** | 只认第一根手指；见 §8 |
| Linux / macOS | **未验证** | 只在 Windows + MSYS2 UCRT64 构建过 |

---

## 2. 验证环境与方法

| 项 | 值 |
| --- | --- |
| 版本 | `0.4.0`（`kVersion` / `kVersionStage`） |
| 平台 | Windows 11 Home China + MSYS2 UCRT64 |
| 图形 | SDL3；自检用 dummy 视频驱动 + 软件渲染器 |
| 构建 | Release，Ninja |
| 自检 | `inking_demo_selftest`：**69 项通过 / 0 项失败**（`CHANGELOG.md` 本次记录） |
| 字体 | 沙箱内 `SDL_IOFromFile` 可能失败；用 `INK_TEST_FONT` 指定可读字体 |

方法：

1. **无头自检优先**：能在 dummy 驱动 + 软件渲染器里断言的，先做成数值判据。
2. **离屏读回**：颜色 / 透明 / 形状这类结果，读像素而不是"看着对"。
3. **同一帧序列对照**：取观测口差值，而不是读累计值的绝对值。
4. **需要"看 / 摸"的**（真窗口手感、字体观感、视频解码）留给人工，不写进
   "已验收"。

> 沙箱里"没有字体"不等于"文字路径有缺陷"；反过来，"字体能打开"也不等于
> 排版在每种语言下都对。

---

## 3. 观测口与读数口径

先确认名字。**没有 `GetRepaintedCount` 这个函数**；同义的观测口叫
`InkingScene::GetRepaintingNodeCount()`。

| 观测口 | 读什么 | 口径 / 注意 |
| --- | --- | --- |
| `detail::BatchStatsSnapshot()` | 提交层累计计数 | **累计值**，自检取前后差值 |
| `BatchStats::geometrySubmissions` | 真的调了几次 `SDL_RenderGeometry` | 合批生效时增长变慢 |
| `BatchStats::vertexRebuilds` / `vertexReuses` | 重算 / 只平移复用顶点 | 静止帧应只有 `reuses` |
| `BatchStats::textureSubmissions` / `textureDraws` | 绕开批次的直接绘制 / 带纹理提交 | 文字与 `contain` 图片会走前者 |
| `InkingScene::GetDrawItemCount()` | 绘制列表条目数 | **含隐藏节点**；不是"可见节点数" |
| `InkingScene::GetHitTableEntryCount()` | 命中表静态条目数 | 容器子孙不算；动态组件记为"洞" |
| `InkingScene::GetHitTableClusterCount()` / `GetHitTableCellCount()` | 簇 / 格子数 | 空白区没有簇 |
| `InkingScene::GetHitTableMaxCellCandidates()` | 单格候选上界 | 判断 `cellSize` 是否太粗 |
| `InkingScene::GetRepaintingNodeCount()` | 处于重绘脏的节点数 | 动画停下约 `kRepaintQuietFrames = 5` 帧后回 0 |
| `InkingScene::GetHitDirtyNodeCount()` | 处于命中脏的节点数 | 约 `kHitQuietFrames = 3` 帧后回 0 |
| `InkingScene::GetStructureGeneration()` | 骨架变化次数 | 判"有没有白重建"的硬判据 |
| `InkingStaticButton::UnknownNameCount()` | 按名字取配置失败的次数 | 静默失败变成可断言 |
| `prefab::ListView::GetBuiltRowCount()` | 真正建出来的行节点数 | 与 `GetItemCount()` 对照 |
| `prefab::OptionBar::GetBuiltItemCount()` | 建出来的项节点数 | 等于拥有型子节点数 |
| `InkingStaticCanvas::GetPaintCount()` / `GetReplayCount()` | 绘制器调用 / 命令表回放 | 脏驱动是否生效 |
| `GetDisplayTransformRevision()` / `GetDisplayTransformRejectedCount()` | 外部变换生效 / 被拒次数 | 拒绝不静默 |

读法建议：

- **累计值必须取差**。`BatchStats` 不从 0 开始，也不随场景销毁归零。
- **条目数先问"口径"**：绘制列表含隐藏节点；命中表不含容器子孙；`GetOwnedChildCount()`
  只数拥有型子节点，`GetChildren().size()` 才是全部（`InkingWidget.h:337`）。
- **脏计数看趋势**：动画在跑时应持续 > 0；停下 3 / 5 帧后应回 0。回不到 0，
  说明有人每帧在标脏。

---

## 4. 成立的部分

| # | 结论 | 证据 / 观测口 |
| --- | --- | --- |
| 1 | 静态组件构造即进命中表，动态组件只留"洞" | `GetHitTableEntryCount()`；`HitTable::AddHole` |
| 2 | 面板开合只改变换，不重烘表 | 开合前后 `GetHitTableEntryCount()` 不变 |
| 3 | 抽屉里的按钮仍可点 | 场景查表到容器后 `HitTestNode` 深挖一层 |
| 4 | 同纹理连续绘制合并 | `BatchStats::textureDraws` 差值小于绘制笔数 |
| 5 | 静止帧不重算轮廓 | `vertexRebuilds` 不增长、`vertexReuses` 增长 |
| 6 | 列表窗口化 | 1000 项时 `GetBuiltRowCount()` 仍是十几二十 |
| 7 | 文本框 UTF-8 按码点退格 | 中文不会被切成半个字；`PrevCodePoint` |
| 8 | 配置名字打错可断言 | `UnknownNameCount()` + 错误日志，不再静默退回默认外观 |
| 9 | 预制件不新增底层 | `OptionBar` / `ListView` / `Select` / `FilePicker` 都派生自 `InkingStaticPanel` |

---

## 5. 成本与量级总表 †

下表是**复杂度量级**，不是 benchmark。仓库没有保留逐项计时，绝对值请自测。

| 操作 | 量级 | 触发条件 | 备注 |
| --- | --- | --- | --- |
| 绘制列表重建 + 排序 | `O(n log n)` | 内容 / 顺序脏 | 一帧内多次改动只做一次 |
| 命中表重建 | `O(条目数 × 覆盖格数)` | 随绘制列表重建 | 与画布面积无关；空白区无簇 |
| 一次命中查询 | 单格候选数 + 动态洞线性扫 | 每次指针派发 | 动态组件通常只有几个 |
| 一次布局 | 子树节点数 × 测量次数 | 显式 `Relayout` | `MeasureLayout` 可能被问多遍 |
| 一次提交 | 批数 + 文字 / 纹理边界数 | 每渲染帧 | 同纹理连续笔合并 |
| `GetAbsX()` / `GetAbsY()` | `O(深度)` | 每次调用 | 别在热循环里对大量节点反复问 |
| `OptionBar::SetItems` | `O(n²)` | 每次项列表变化 | `AddChild` 每次查重 |
| `ListView` 窗口化 | `O(可见行数)` | 每次同步可见窗口 | 与总项数无关 |

---

## 6.7 绘制 / 合成的成本与取舍

这一节回答"钱花在哪、什么时候该换方案"。条目编号是引用锚点，别改号。

1. **先分清三个"条目数"。** 绘制列表（`GetDrawItemCount()`）是**注册过的节点**，
   **包含隐藏节点**——隐藏只在提交时短路，不从列表里删（删了就再也加不回来）。
   命中表（`GetHitTableEntryCount()`）只登记**可见的静态节点**，并且**跳过所有
   `OwnsSubtreeHit()` 容器的子孙**；动态组件不占条目，只留一个"洞"。
   `GetOwnedChildCount()` 又只数**拥有型**子节点，借用型子节点照样出现在
   `GetChildren()` 里。三个数对不上是正常的，先问口径再下结论。

2. **绘制列表只在结构脏时重建。** 重建是稳定排序 `O(n log n)`，顺带重算所有
   静态节点的绝对坐标快照；没有脏时一次都不碰。一帧里连续 `Resize` 三次、
   `ChangeZIndex` 两次，也只重建一次——这就是"标脏"存在的意义。

3. **命中表重建正比于"条目数 × 覆盖格数"，不正比于画布面积。** 只有含 UI 的
   粗块才建簇，簇内 `cellSize` 默认 64；空白区没有簇。格子只缩小候选范围，
   最终是否命中仍由节点自己的 `HitTest` 回答，所以格子算粗了只会多几个候选，
   **不会误命中**。

4. **变换通道不改表。** 面板开合、按钮三态、列表滚动都只改变换：表里登记的是
   "所有可能变换的并集"，查询时把点反变换回本地再精判。于是抽屉来回拉、
   按钮悬停放大，`GetHitTableEntryCount()` 一条都不变，也不会重烘。这是本框架
   最值钱的一条取舍——代价是包络必须**保守**，给大了只是候选多几个。

5. **批提交按纹理分界，文字是边界。** 连续的同种提交合并成一次
   `SDL_RenderGeometry`；纹理一变就刷上一批，绝不重排、绝不跨笔合并，所以
   顺序永远与调用顺序一致。文字走 `SDL_RenderTextureRotated`，它本身就是批次
   边界。`BatchStats` 的计数全是累计值，自检取差值。

6. **`OptionBar` 的项是运行期重建的子节点：几十项无所谓，几百项请换成
   `ListView`。** `SetItems` 走 `ClearChildren()` + 逐项 `AddChild()`，而
   `AddChild` 每次都要在拥有表里查重，整体是 `O(n²)`（`OptionBar.h:84-86`）。
   更关键的是"项 = 一个静态按钮"：几百项就是几百个节点、几百条命中表条目、
   几百次绘制提交。`ListView` 的窗口化只建可见行，1000 项也只有十几二十个节点，
   是这一档问题的正解。

7. **顶点缓存有键，键不对就重算。** 缓存键包含形状、尺寸、展示倍率、变换与
   **颜色**（顶点里是逐顶点颜色，换色不复用就会"状态切了、画面没变"）。
   描边与纹理**不进顶点缓存**：一个节点只有一个缓存槽，缓存键要再带描边宽度 /
   颜色或纹理句柄 + 适配模式 + 图尺寸，会互相顶掉。这是"正确性优先"的取舍。

8. **容器递归命中的成本在容器内的子节点数。** 子孙不进表，查询落到容器后要
   `HitTestNode` 遍历子树；`GetAbsX()` 是 `O(深度)`，容器套容器时别在热路径里
   反复问。裁剪同理：每个子节点提交时都要过一遍祖先裁剪矩形，层级越深越贵。

**什么时候该换方案**：

| 现象 | 先看 | 处理 |
| --- | --- | --- |
| 每帧都在重建绘制列表 | `GetStructureGeneration()` 是否每帧涨 | 找"每帧标结构脏"的调用点 |
| 悬停 / 动画时命中表条目数抖动 | `GetHitTableEntryCount()` | 变换不该动表；检查是否误标结构脏 |
| 列表项多时卡在 `SetItems` | 项数 × 项数 | 换 `ListView` 窗口化 |
| 单格候选数很大 | `GetHitTableMaxCellCandidates()` | 包络太保守或格子太粗 |
| 静止时 `vertexRebuilds` 还在涨 | 缓存键里的颜色 / 变换 | 是否每帧改了颜色或变换 |

---

## 7.4 变换作用域（T-7 之前）†

**结论：这是一条已关闭的历史限制，不是当前缺陷。** 原始报告 §7.4 的正文与
数据不可考，这里只复原它记录的行为差异。

| | T-7 之前 | `0.4.0`（T-7 之后） |
| --- | --- | --- |
| 组件自己的变换 | 只作用于自己那一笔 | 作用于自己 + 整棵子树 |
| 子节点 | 各自不知道"父级在动"，会留在原地 | 提交层复合祖先变换，一个字不用改 |
| 抽屉 / 下拉 | 要做"整块移动"只能逐个改子节点 | `GetSubtreeTransform()` 一处就够 |
| 命中 | 子节点包络会随容器变换过期 | 子孙不进表，容器递归解答 |

现在的实现见 `InkingWidget::GetSubtreeTransform` 与
`InkingStaticPanel::GetSubtreeTransform`：返回当前显示变换，提交层把它复合进
每一笔绘制；滚动则是在同一份变换上再平移一点。

---

## 7.5 预制件：C1–C5 的量级与限制

预制件是"用现成组件组装"，**不新增底层绘制 / 命中 / 布局代码**。下表是各自的
典型规模与硬边界。

| 预制件 | 就是 | 典型规模 | 硬边界 / 限制 |
| --- | --- | --- | --- |
| **C1 `OptionBar`** | 布局排列的按钮组 | 几项到几十项 | 每项一个静态按钮；`SetItems` 是 `O(n²)`；几百项换 C3 |
| **C2 `MenuBar`** | 顶部横向容器（可当标题栏） | 几个到十几个项 | 弹簧占位 + 右上角按钮；必须重写 `HandlesSubtreeWheel()` |
| **C3 `ListView`** | 滚动容器 + 布局 + 只建可见项 | 1000 项仍只建十几二十行 | 必须 `SyncVisibleWindow()`；`rowHeight > 0`；不做拖拽重排 |
| **C4 `Select`** | 收起按钮 + 就地展开的 `OptionBar` | 几项到几十项 | 用全局 z（默认 200）换层级；不做"点外部关闭" |
| **C5 `FilePicker`** | 路径框 + 快捷位置 + 列表 + 按钮栏 | 一个目录的条目数 | 走自绘，不走原生对话框；路径全程 `std::filesystem::path` |

### C1 `OptionBar`

- 项是**运行期重建**的子节点：`SetItems` 内容没变就一个节点都不重建（幂等），
  变了就 `ClearChildren()` + 逐项 `AddChild()`。
- `enabled = false` 落成两件事：回调不发、文字调暗（借用 `SetTextColor`）。
  **背景仍会随悬停变化**——这是"用现有配件组装"的边界，写在明处。
- 静态按钮重写了 `GetWidth()` / `GetHeight()`，布局写进 `_width` 的那份对按钮
  **不生效**；所以建项时按容器宽兜底，布局只负责"排在哪"。

### C2 `MenuBar`

- 内容项可以是任意 `InkingWidget`；"内容偏左 + 窗口按钮贴右上角"靠内部一根
  `fillMain` 弹簧实现，`justify` 仍然只管弹簧之前的内容项。
- `hideInFullscreen` 只记"被这条规则藏起来了"；退出全屏只恢复那一次，
  **不覆盖业务自己调过的 `SetVisible(false)`**。

### C3 `ListView`

- 窗口化：只建 `[first, last]` 这几行，两头各放一个**撑位置的占位块**。
  前面那个把首行顶到 `first × 行高`，后面那个把总高补到 `项数 × 行高`。
- 硬判据：**1000 项时 `GetBuiltRowCount()` 仍然只有十几二十个，
  `GetItemCount()` 才是 1000**（`ListView.h:18-19`）。
- 滚动 / 裁剪由预制件强制打开；`rowHeight <= 0` 会直接坏掉窗口化计算。
- 第一版不做：拖拽重排、行内编辑、多列 / 排序、树形缩进。

### C4 `Select`

- 展开 = 就地显示一个 `OptionBar`，**只改可见性**；`SetVisible` 是骨架变化，
  所以"展开的那一列点得到、收起之后点不到"是白拿的。
- 层级靠**全局 z**：展开时从 `Select` 自己开始提到 `expandedZIndex`（默认 200），
  内部件依次 +1。这是"用全局 z 换来的展开在内容之上"——真做局部层级（A8b）
  之前，不改框架就只有这一条路（`SelectData.h:85-93`）。
- 不做：点外部自动关、浮出式展开、多选、首字母定位。过滤框是**每渲染帧比一次
  文本**（`InkingTextInput` 没有"文本变了"事件槽），只在展开时做。

### C5 `FilePicker`

- **路线是组装自绘，不是 `IFileOpenDialog`**。理由：原生路线要 COM 初始化 +
  全程宽字符，而且**自检拿不到确定性判据**（弹窗要人点）——与本项目"能变成
  数值判据的自己验"直接冲突（`FilePickerData.h:9-13`）。它真正多给的只有
  "最近位置"这类便利。
- 路径全程 `std::filesystem::path`；窄串构造在中文目录会抛异常，`error_code`
  重载救不了。枚举必须带 `error_code`。
- "确定"要分三种模式：开文件 / 存文件要选中文件；选目录要**当前目录**。
- 不做：原生对话框、磁盘驱动器列表、书签 / 最近位置、文件名与目录分开输入。

---

## 7.6 B7：单行文本框

先给结论：

| 能力 | 状态 | 依据 |
| --- | --- | --- |
| 光标左右移动 | **已落地** | `TextEditState::MoveLeft` / `MoveRight` |
| 选区（Shift + 左右、全选、拖选） | **已落地** | `MoveLeft(true)` / `SelectAll` / `PressAt` + `DragTo` |
| 插入 / 退格 / Delete / 剪贴板 / IME 提交 | **已落地** | `InsertText` / `Backspace` / `DeleteForward` / `onTextInput` |
| **光标上下移动 + 跟随滚动** | **未做（另一件事）** | `TextEditState.h:32` |
| 多行输入 | **未做** | `InkingTextInput.h:24-25` 强制 `NoWrap` |
| 撤销 / 重做、双向文本、字素簇 | **未做** | `TextEditState.h:34-36` |
| Tab 焦点遍历、候选窗位置 | **未做** | `InkingTextInput.h:62-63` |

**为什么上下移动不是"多按两个键"**：单行输入框里"上 / 下"没有语义；到了多行，
它要求先有**行盒测量**（每个视觉行的起点 / 高度）、**目标列记忆**（从长行移到
短行再移回来，光标要回到原来的列）、以及**跟随滚动**（光标移出视口时容器要滚）。
这三件事分别属于排版层、编辑状态机与滚动容器，不是 `MoveLeft/MoveRight` 的
同类扩展。所以这一版明确把它留在"另一件事"，而不是塞一个半成品。

其余边界：

- 位置一律是**字节偏移里的码点边界**，点击算出的偏移会向前吸附；中文退格是
  整码点删，不会把汉字切成两半。
- 粘贴多行时 `\r` / `\n` 被丢掉，Enter 留给"提交"。
- 编辑状态是独立的一份 `TextEditState`（纯逻辑、可单测）；每次改动后同步给标签
  基类，触发重排 + 重绘。

---

## 8. "不成立"（做不到 / 这版不做）

| 项 | 分类 | 依据 | 说明 |
| --- | --- | --- | --- |
| **柔边阴影 / 真模糊** | 做不到（当前渲染路径） | `InkingDecoration.h:36-40` | 阴影是**偏移的实心副本**；真模糊要离屏目标 + 卷积（或 SDF 软化），是另一条渲染路径 |
| **局部层级 A8b** | 这版不做 | `InkingStaticPanel.h:158`；`Select.h:21` | 全局只有一条 z 序；容器里的东西盖不住容器外的兄弟。`Select` 因此用 `expandedZIndex = 200` 换层级 |
| **多指针** | 这版不做 | `MouseInput.h:99-107`；`InputDesign.md` §15 | 只认第一根按下的手指；后来的手指**明确忽略**，否则两根手指会互相抢一个指针状态 |
| **跨平台（Linux / macOS）** | 未验证 | `API.md` §2；`CHANGELOG.md` | 只在 Windows + MSYS2 UCRT64 构建过；不承诺行为一致 |
| **`ring` / `arc` 的描边** | 做不到（当前实现） | `InkingDraw.h:122-125` | 它们本身就是一条细带，沿边界描边会变成四条线；现在**直接不画**，配置写了也不报错 |
| **原生文件对话框** | 这版不做（已裁定） | `FilePickerData.h:9-13` | COM + 宽字符 + 无法自检；走自绘 `FilePicker` |
| **多行输入 / 光标上下 / 跟随滚动** | 这版不做 | `TextEditState.h:30-32` | 见 §7.6；要排版 + 编辑 + 滚动三处一起改 |
| **撤销 / 重做、双向文本、字素簇** | 这版不做 | `TextEditState.h:34-36` | emoji 组合、国旗按码点删；纯逻辑层尚未建模 |
| **Tab 焦点遍历** | 这版不做 | `InkingTextInput.h:62-63` | 焦点是"点击获得"；键盘那层的遍历还没做 |
| **图片纹理进顶点缓存** | 这版不做 | `InkingDraw.h:146-150` | 缓存键要带纹理句柄 + 适配模式 + 图尺寸，一个节点只有一个缓存槽 |
| **动态组件的遮挡仲裁** | 这版不做 | `InkingDynamicButton.h:33`；`InputDesign.md` §15 第 1 条 | 动态组件自管自，z 序不由 `JudgePointer` 负责 |
| **ABI / API 稳定** | 不承诺 | `API.md` 顶部横幅 | 0.x 发布版，升级按 `CHANGELOG.md` 逐条对照 |

---

## 9. 未验证与已知缺口

1. **真窗口渲染、文字栅格化、视频解码**：沙箱内 `SDL_IOFromFile` 可能失败，
   字体打不开；这三项按"可用但未逐项验收"看待。
2. **手感的验收**：动画曲线、拖拽跟手、滚动惯性这类东西没有数值判据，必须人看。
3. **`TaskQueue` 退出收尾**：函数内静态量在 DLL 卸载时析构并 join 管家线程，
   某些环境下进程退出像"挂住"；与业务无关，属线程收尾语义。
4. **文档缺口**：`InputDesign.md`、`TaskGuide.md`、`CXXCSS.md`、`AGENTS.md`
   仍不完整（见 `SKILL.md` §0），注释里的条号无法逐条核对。
5. **未验证平台**：Linux / macOS 没有构建记录。

---

## 10. 复现方法

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cd build && PATH=.:$PATH ./bin/inking_demo_selftest.exe
```

读 **stdout 末行"失败项：N"**，不要看退出码：自检退出时会因 `TaskQueue` 的
静态量析构而"挂住"，与失败项无关（`API.md` §7）。

要复现本报告里的计数：

1. 在同一场景、同一帧序列里取 `BatchStatsSnapshot()` 前后差值；
2. 开合面板前后各读一次 `GetHitTableEntryCount()`，应相等；
3. 列表灌 1000 项后读 `GetBuiltRowCount()` 与 `GetItemCount()`，前者应是
   十几二十，后者是 1000；
4. 动画停下后等 5 帧，`GetRepaintingNodeCount()` 应回 0；等 3 帧，
   `GetHitDirtyNodeCount()` 应回 0。

---

> 本文档对应版本：0.4.0（0.x 发布版）。
