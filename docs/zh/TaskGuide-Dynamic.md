# 任务书：动态档补全的方案设计（元任务）

> 状态：**已完成**（2026-11-02，`MD-01`~`MD-05` 全部收口；产物是 `docs/zh/TaskGuide-G.md`）
> 日期：2026-11-01 初稿；2026-11-02 修订（编号、验收、范围、缺口）
> 来源：PvzAdventure 接入 inking 0.8.5，实体层需要「几何可变 + 自带动画」的组件，现成件拼不出来
> 交付物：**`docs/zh/TaskGuide-G.md`**（另一份文件）
> 本文件：方案设计任务书，是**输入**，不是交付物
> **本任务不写任何 `.h` / `.cpp`**

> ⚠ **两条防错提示（初稿踩过）**
>
> 1. **交付物不得与本文同名。** 初稿写的是 `交付物：docs/zh/TaskGuide-Dynamic.md`，那就是本文件；
>    而 `MD-04` 的动作是"写一份任务书"——照字面执行会把指令本身覆盖掉。
> 2. **本文的 `MD-01`…`MD-05` 不是 `TaskGuide.md` 的任务编号**，只是这份方案的执行步骤。
>    交付物里的编号是 `G` 系列，见 §3 第 2 条。

---

## 0 结论

一句话：**本任务只产出一份文档，产出的那份文档才去指导重构。**

覆盖范围到四种组件为止，不多不少：

| 组件 | 要补的动态档 | 连带（本轮一并规划） |
|---|---|---|
| Image | `InkingDynamicImage` | — |
| Sprite | `InkingDynamicSprite` | `InkingSpriteGroup`（**本轮不设计，但必须保持可编译**，见 §2.3） |
| Label | `InkingDynamicLabel` | `InkingTextInput`（`InkingStaticLabel` 的子类） |
| Panel | `InkingDynamicPanel` | FilePicker / ListView / MenuBar / OptionBar / Select；**`InkingFixedPopup` / `MenuPopup`**（见 §2.3） |

**明确不覆盖**：

- `Canvas`、`Video`、`Scene`、`Window`——它们的几何在设计期就定了，没有运行期移动的需求。
- **`InkingSpriteGroup` 的双档设计**——它自己需要更详细的设计（子项坐标跟随、组内几何传播、成组动画），另开一份任务书。本轮只要求它在 Sprite 分档之后**仍然能编译且行为不变**。

> **前置（`MD-01` 之后加）**：「要补动态档」这句话已被附录 A.3 拆成三种诉求——**偶发拖拽**（A13 已够）、**每帧走位**（必须动态档，用 A13 会每帧重烘命中表）、**运行期改占位**（必须动态档）。`MD-02` 开工前每个组件都要能说出自己属于哪一种。

## 1 为什么先派一个「写文档」的任务

因为四个组件的静态实现结构各不相同，抽 `Like` 的边界在哪、兼容怎么保、拆几步，**都得先读代码才能定**。

直接开工的风险很具体：Image 的绘制逻辑在 `onRender` 里、Sprite 的帧管理在 `onAnimationTick` 里、Label 还有文本测量要动、Panel 有一堆预制件继承它——按同一个模子硬套，做到第三个组件会发现前两个的边界划错了。

先写方案后动手，是这份任务书唯一的目的。

## 2 依据核对

核实对象是 `include/` 下的实际代码与 `docs/zh/` 下已有的文档，不是结论。

| 条目 | 判定 | 证据 |
|---|---|---|
| 四个组件都只有静态档 | 成立 | `include/` 下 `*Dynamic*` 仅两个文件：`widget/InkingDynamicWidget.h`、`button/InkingDynamicButton.h` |
| Button 的两档共用接口，不是继承 | 成立 | `InkingStaticButton : InkingStaticWidget, InkingButtonLike`（`button/InkingStaticButton.h:82`）；`InkingDynamicButton : InkingDynamicWidget, InkingButtonLike`（`button/InkingDynamicButton.h:71`） |
| 已有第三个动态档先例 | 成立，且**就是 Panel 的那一份** | `class InkingFixedPopup : public InkingDynamicWidget`（`panel/InkingFixedPopup.h:68`），见 §2.3 |
| Sprite 的动画不依赖几何 | 成立 | `InkingSprite::onAnimationTick` + `Play/Pause/SeekToFrame`，都在内容层 |
| 静态档的「几何定型」是类型保证 | 成立 | `widget/InkingStaticWidget.h:8`：「这是**类型上的硬保证**，不靠约定。」 |
| 动态档不进命中表 | 成立 | `widget/InkingDynamicWidget.h:3`：「几何会自己变，所以不进命中表、自己维护命中状态」 |
| **两档的差异不止几何** | **成立（初稿漏了）** | `docs/zh/ComponentGuide.md:265-271` 的五栏对照；`InkingDynamicButton.h:21-56` 整节讲输入；`detail/InkingPointerProbe.h` |
| 静态档「不能动」是错的说法 | 成立 | 它不能改的是**几何**；颜色 / 透明度 / 变换过渡照做（`ComponentGuide.md:280-282`，`src/inking.cpp:78-94` 用 `static_assert` 钉住四条） |

**一处修正**：一开始以为静态组件"不能动"，实际是**不能改几何**。帧内容照样动，`onAnimationTick` 在基类上。写方案时别把这条搞混。

**一处补充**：静态 / 动态的取舍**只有一条判据**——几何会不会在构造之后自己变（`ComponentGuide.md:263`）。这条是本轮所有方案的上位规则，任何"两档都继承一点"的写法都违规（同文档 §5 第 3 条：**不要两者都继承**）。

### 2.1 初稿已修正的七处表述

留在这里而不是悄悄改掉，是因为这七条都是**读了文档就会被误导**的类型，后来者需要知道它们为什么变成现在这样。

| # | 初稿说法 | 实际 | 依据 | 改成 |
|---|---|---|---|---|
| 1 | 交付物 = `docs/zh/TaskGuide-Dynamic.md` | 与本文同路径，`MD-04` 会覆盖指令本身 | 头两行 vs `MD-04` 落点 | 交付物 = `docs/zh/TaskGuide-G.md` |
| 2 | 元任务编号 `T-01`…`T-05` | `T` 在既有任务书里是**语义系列**「纹理 / 文字层与命中」，`T-1`…`T-4` 一一对应具体能力 | `TaskGuide.md:45`；`:59`「'T-1 的坑'指一类具体的漏命中」 | 本文改用 `MD-01`…`MD-05`，并声明它不是任务编号 |
| 3 | 「`A`–`T`、`r2` 已被占用」 | 实际只有 `A / B / C / D / T / r2` 六个系列，**`E`–`S` 全空** | `TaskGuide.md:39-46` | 改为"六个系列已占用，`G` 取 Geometry 之意且必须登记进 §1.1"（**已完成**：当前是七个系列） |
| 4 | 两档差异「恰好只有四个几何入口」 | **至少六项**：4 个几何入口、`Tick`/`onTick`、命中表登记、输入通道（推 → 拉）；另两档**各写了一份**默认 `onRender` | `ComponentGuide.md:265-271`；`InkingDynamicWidget.h:22-36`；`InkingDynamicButton.h:21-56` | `MD-02` 按六项验，不再按四项 |
| 5 | 校验脚本对 `G` 系列「会误报」 | 反向：**全篇漏检**，恒返 0 个错误 | 实验见 `MD-05` | 验收不再依赖脚本，改成人工清单 |
| 6 | 「抽 `Like` 是纯重构，所以先抽」 | 仓库自己的 `InkingButtonLike` 是**先有第二份复制品、再抽出来**的（它就是为了收拾 `StaticImage` 抄了三份方法的烂摊子）⇒ 边界要靠第二份实现逼出来，猜不出来 | `InkingButtonLike.h:13-16`；`MD-02` 附录 B.0 | 顺序改成"**先复制、再抽取**"，两笔相邻、各自验收（附录 B.4） |
| 7 | 「抽了 `Like` 之后旧类名要选 A / B / C 三方案之一」 | 这是伪问题：`InkingStaticImage` / `InkingStaticLabel` / `InkingStaticPanel` 的名字里**本来就带 `Static`**，多继承一个 `Like` 不动公开面 ⇒ 零改动。唯一没前缀的 `InkingSprite` 保留指静态档即可 | 附录 C.3、C.4 | **零改名**；A / B / C 三选一与 `[[deprecated]]` 全部作废 |

第 4 条值得单独说一句：`InkingDynamicWidget.h:6` 那句「与静态档的唯一区别就是下面这四个几何写入口」**本身就是错的**——同一文件 `:27-36` 就有 `Tick` / `onTick` / `onRender`。`InkingDynamicButton.h:12-14` 又抄了一遍同样的说法，然后在 `:21` 开始用整节讲输入为什么不同。初稿照抄了这句注释，于是把验收判据也定错了。**注释是所有下游判断的地基，错一句会连锁三处。**

### 2.2 既有缺陷登记簿（谁修了、谁还没修）

| # | 缺陷 | 证据 | 为什么现在不改 |
|---|---|---|---|
| 1 | 「唯一区别就是四个几何入口」与同文件自相矛盾 | `InkingDynamicWidget.h:6` vs `:27-36` | **已修**（2026-11-02，`G-00`） |
| 2 | 同一句病在按钮头里再说一次 | `InkingDynamicButton.h:12-14` vs `:21-56` | **已修**（同上） |
| 3 | `docs/zh/TaskGuide.md` 头部仍标「本文对应版本 0.5.2」 | `TaskGuide.md:3`（当时 0.8.5） | **已处理**（2026-11-02）：头部与页脚统一成「0.9.x 开发中」 |
| 4 | 洞漏登"不会点错"（**方向写反了**） | `HitTable.h:96-108`、`InkingScene.cpp:1155-1163`；短路点在 `InkingScene.cpp:1234-1236` | **已修**（2026-11-02，`G-00`） |
| 5 | **`ctest` 下 `selftest` / `selftest_windows` 恒红**（5/7） | `add_test` 没设 `PATH`（`CMakeLists.txt:446,462`）；SDL3 的 dll 在 `C:/msys64/ucrt64/bin`，`build/bin` 里只有 `libinking.dll` ⇒ 字体打不开（自检打印 `[skip] 没有可读字体`）⇒ `glyph_atlas_full` 的静默失效自检被跳过 ⇒ 同步检查判红。带 PATH 手动跑是**失败项 0** | **已修**（2026-11-02）：新增 `tools/stage_runtime_deps.sh` + `inking` 的 `POST_BUILD`，把 **ldd 闭包（19 个 DLL）** 拷进构建输出目录；`ctest` 裸环境 **7/7**，`build/bin/*.exe` 也能直接双击跑。与 `package.sh` 那段等价、两处互相指了指针 |

顺带登记一条**与本任务无关**的：`CMakeLists.txt:4` 的 `project(... VERSION 0.7.1)` 从未跟上 `kVersion`，导致已发布的 `lib/cmake/inking/inkingConfigVersion.cmake` 写的是 `PACKAGE_VERSION "0.7.1"`，下游 `find_package(inking 0.8.5)` 会直接失败。这条与动态档无关，**不要顺手塞进本次重构**（会把一个版本号问题和一次大重构混在一个提交里）。

### 2.3 初稿漏掉的两个结构问题

**(a) `InkingFixedPopup` 不是"先例"，是 Panel 这一档唯一的动态样本。**

初稿把 FilePicker / ListView / MenuBar / OptionBar / Select 列进连带，却漏了 `InkingFixedPopup`——而它才是唯一的动态档面板，且 `MenuPopup : InkingFixedPopup`。`ComponentGuide.md:284-300` 专门拿它当"动态档的对照样板"。

~~于是抽 `PanelLike` 有一条现成的验收：抽完之后 `InkingFixedPopup` 应能改成 `: InkingDynamicWidget, PanelLike`，并删掉它自己那份 `onRender` / `onRenderDecoration`。~~ **这条假设已被 `MD-01` 推翻**（证据见附录 A.2）：两个类的 `onRender` / `onRenderDecoration` 都只是 8 行 / 3 行的薄包装，真正的绘制早在 `src/panel/PanelPaint.cpp:48` 共用；**删不掉是正常的**，删不掉不证明抽歪。

结论：**`PanelLike` 不抽**。两档的差异不在绘制，而在状态机（`PanelLook`，314 行）、滚动、容器命中三组，而这三组 `FixedPopup` 一组都没有。`InkingDynamicPanel` 按独立实现做。`InkingStaticPanel.h:289-290` 那句"`PanelLook` 是静态 / 动态 Panel 共用的一份"是对的——**正因为它在，`PanelLike` 没有生存空间**。

**(b) 四个组件是继承链，不是兄弟；`InkingSpriteGroup` 会被悬空。**

Button 那套模子能成立，前提是 `StaticButton` 与 `DynamicButton` 是**兄弟**。四个目标组件的现状不是：

```
InkingStaticImage ← InkingSprite ← InkingSpriteGroup
InkingStaticLabel ← InkingTextInput
InkingStaticPanel ← FilePicker / ListView / MenuBar / OptionBar / Select
InkingDynamicWidget ← InkingFixedPopup ← MenuPopup
```

`InkingSprite` 今天是 `InkingStaticImage` 的**子类**。一旦 Image 分出静态 / 动态两档，Sprite 必须选定跟哪一档；Sprite 选定之后，`InkingSpriteGroup`（本轮不设计）**必须至少还能编译**。（**已解**：零改名方案下 `InkingSprite` 仍是静态档，`SpriteGroup` 什么都不用改——见附录 C.4。）最小处置是让它挂静态那一档（行为不变），真正的双档设计留给后续任务书——**但这条"最小处置"必须写进交付物**，否则抽完 `SpriteLike` 当天就编不过。

同一张图还有个后果：`InkingDynamicSprite` 与 `InkingStaticSprite` 之间**没有任何公共基类**（Button 也是这样）。所以任何持有 `InkingSprite*` 的现有代码，都装不下一个动态精灵。这直接推翻 §`MD-03` 里"老代码零改动"的收益——见 `MD-03`。

## 3 交付物的写法约定

产出的 `docs/zh/TaskGuide-G.md` 必须遵守这五条，否则它自己就不是一份合格任务书：

1. **格式对齐现有 `TaskGuide.md`**：五段式（目标 / 落点 / API / 验收 / 偏离），不要另创一种。两个系列的任务书并排放，格式不同会让后来者以为走错了地方。
2. **编号新开一个系列**，用 `G`（Geometry）。已占用的是 `A / B / C / D / T / r2` 六个（`TaskGuide.md:39-46`），`E`–`S` 其实都空着；选 `G` 是因为它有意义，不是因为没有别的字母。编号只增不改，废止的保留并注明"未落地"。
3. **每个任务必须有可断言的验收**。禁止"看起来对"。能写成 `ctest` 名的就写 `ctest` 名，写不成的写"手动核对的判据是什么"。
4. **必须挂进既有文档体系**（初稿整节缺失）：
   - `TaskGuide.md` §1.1 的系列表**加一行 `G`**。不加的话 `grep TaskGuide` 索引不到它，`G` 就是第七个没人知道的系列。
   - `ComponentGuide.md` §5 的 13 条「新增组件 checklist」逐条对照，尤其第 3 条（**不要两者都继承**）、第 12 条（改了静态 / 动态边界就补 `src/inking.cpp` 的编译期断言）、第 13 条（给可断言的观测口）。
   - `docs/zh/api/` 新增对应条目（`InkingFixedPopup.md` 是现成的唯一动态档样例，照它写）。
   - `CHANGELOG.md` 一条。
5. **不得用 `check_task_book.py` 的"通过"当验收**（理由见 `MD-05`）。G 系列的验收必须是人能读懂的断言，不是脚本退出码。

## 4 任务

### MD-01 摸清四个静态实现的可抽边界

- 状态：**已完成**（2026-11-02）。五列现状表见**附录 A.1**；`PanelLike` 判定**不抽**（A.2，理由：绘制早已在 `src/panel/PanelPaint.cpp` 共用，能抽的只剩 11 行胶水）；`InkingFixedPopup` 是 Panel 唯一的动态样本，但它连 `PanelLook` 都没用。追加发现见 **A.3**：`SetDisplayTransform` 标的是结构脏，**每帧走位不能用 A13**——这反过来证明动态档对实体层是必需的。

---

### MD-02 确认 `Like` 抽取模式能复制

- 状态：**已完成**（2026-11-02）。六项差异、可复制/需改造/复制不过去、`PointerProbe` 归属见**附录 B**。两条要紧的：① `Like` 能复制但**顺序要反过来**——先复制第二份实现、再抽（仓库自己的 `InkingButtonLike` 就是这么来的）；② 真正的工作量在**输入通道**（每组件 30–56 行），不在几何；Image 的拖/缩与 Panel 的滚动归属是两处必须重写的硬骨头。

---

### MD-03 定兼容与迁移策略

- 状态：**已完成**（2026-11-02）。结论：**零改名**——四个类里三个名字里本来就带 `Static`，加动态档只是多继承一个 `Like`，公开面不动；唯一没前缀的 `InkingSprite` 也保留指静态档。原稿 A / B / C 三选一与 `[[deprecated]]` 整条作废。证据、引用点盘点、三条真约束见**附录 C**。决定性发现：**代码生成器把基类名写死在生成的代码里**（`tools/inkgen/Emit.cpp:1260,1296,1321`），下游已生成的 `.h` 不会重新生成 ⇒ 旧名必须继续是同一个东西，这一点初稿完全没提。

---

### MD-04 写重构任务书

- 状态：**已完成**（2026-11-02）。交付物 **`docs/zh/TaskGuide-G.md`**（304 行，9 个任务 `G-00`…`G-08`）：`G-00` 修错注释 → Image / Sprite / Label 各"复制 + 抽取"相邻两笔 → `G-07` 动态 Panel（独立实现）→ `G-08` 生成器支持 `dynamic`。`TaskGuide.md` §1.1 已从"六个系列"改为"七个系列"并登记 `G` 行。范围按用户 2026-11-02 的确认取"四个都补"。

---

### MD-05 自检结构

- 状态：**已完成**（2026-11-02）。8 条人工核对全部通过，记录与脚本基线见 `TaskGuide-G.md` **附录 A**。执行中查出并修掉一处不合格：`G-03` / `G-05` 的落点写了"补断言"、验收里却漏了对应条目。脚本对 `G-` 系列仍返回 `0 个错误`——按 `MD-05` 的预期，这个 0 被当作"没查"的证据写进了附录，不是通过依据。

---

## 5 批次门槛与进度

### 5.1 门槛

| 批次 | 发布前必须满足 |
|---|---|
| 全量 | `docs/zh/TaskGuide-G.md` 存在且**不等于**本文件；五段式齐全；编号 `G` 系列连续、首项 `G-00`；兼容策略写明"零改名"且与附录 C.4 的对照表一致；覆盖 Image / Sprite / Label / Panel 四组件，显式排除 `Canvas` / `Video` / `Scene` / `Window`，并写明 `InkingSpriteGroup` 本轮不动且零改动；`TaskGuide.md` §1.1 已加 `G` 行 |

### 5.2 进度表

| 编号 | 任务 | 状态 |
| - | - | - |
| MD-01 | 摸清四个静态实现的可抽边界 | 已完成（附录 A） |
| MD-02 | 确认 Like 抽取模式能复制 | 已完成（附录 B） |
| MD-03 | 定兼容与迁移策略 | 已完成（附录 C） |
| MD-04 | 写重构任务书 | 已完成（`TaskGuide-G.md`） |
| MD-05 | 自检结构 | 已完成（附录 A 在 G 文件里） |

完成任务后，本节状态改 `已完成`，正文对应段落按压缩规则收成一行——两处必须同时改。

## 6 明确不做

| 不做的事 | 原因 |
|---|---|
| 写任何 `.h` / `.cpp` | 本任务只产出文档；重构是下一份任务书的事 |
| **`InkingSpriteGroup` 的双档设计** | 它需要更详细的设计（子项坐标跟随、组内几何传播、成组动画），另开任务书；本轮只保它编译 |
| 评估工期、排版本号 | 那是 Shyi 排期时的事，调研阶段给的估时只会误导 |
| 覆盖 `Canvas` / `Video` / `Scene` / `Window` | 它们几何不变，补了也没人用 |
| 顺手补 `InkingTextInput` / 五个预制件的动态档 | 它们继承 Panel / Label，父类补完再决定要不要派生；本轮只在兼容表里登记 |
| 改现有 `TaskGuide.md` 正文 | 只在 §1.1 加 `G` 一行；动正文会让历史编号全部失准 |
| 修 `InkingDynamicWidget.h:6` 等错注释 | 本任务不写 `.h`；登记在 §2.2，由 `G-00` 第一笔修 |
| 修 `CMakeLists.txt` 的 `VERSION 0.7.1` | 与动态档无关，混进来会让一次大重构和一个版本号 bug 变成同一个提交（§2.2 已登记） |
| 为了过校验脚本把编号改成 `T-` | 会和「纹理与命中」系列撞号，见 `MD-05` |

## 附录 A：MD-01 执行结果（2026-11-02）

核实对象是代码，不是推断。所有行号对应当前的 `D:/InkingUIKit` 工作区。

### A.1 五列现状表

| 组件 | 绘制位置 | 几何依赖 | 派生影响 | 输入通道 |
|---|---|---|---|---|
| `InkingStaticImage`<br>`image/InkingStaticImage.h:47` | `onRender`（`src/image/InkingStaticImage.cpp:399`）**薄包装**，整笔委托 `detail::PaintButtonAppearance`（`:404`，与 Button 同一份） | **只读**：`GetShape()`（`InkingStaticImage.h:62`）→ `GetWidth/GetHeight`；**不写**几何。拖 / 缩走 A13 `SetDisplayTransform`（`InkingWidget.h:581`），不动 `_width/_height` | `InkingSprite`（`:61`）→ `InkingSpriteGroup`（`InkingSpriteGroup.h:76`） | **推**：`onPointerPress`（`src/image/InkingStaticImage.cpp:312`）、`onPointerDrag`（`:332`）；无 `onPointerStateChanged` |
| `InkingSprite`<br>`image/InkingSprite.h:61` | **没有自己的 `onRender`**（`include/` 与 `src/` 均 0 处）。换帧只在内容层：`applyFrame`（`:239`）→ `SetImageBytes` / `SetRenderSourceRect`（`InkingStaticImage.h:146,164`） | 同 Image。且**刻意不动尺寸**：`SetSpriteData` 不改 `width/height`（`:145-147` 明写"改尺寸属布局层"） | `InkingSpriteGroup`（`InkingSpriteGroup.h:76`）——**它已经在改自己尺寸**（`src/image/InkingSpriteGroup.cpp:130` → `ApplySelfSize`，靠 `InkingWidget.h:907` 的 friend） | **继承 Image 的推**（自身 0 处输入重写） |
| `InkingStaticLabel`<br>`text/InkingStaticLabel.h:30` | `onRender`（`src/text/InkingStaticLabel.cpp:222`）：按**当前** `_width/_height` 排版（`:230`）再逐行提交文字层 | **只读** `_width/_height`。写尺寸的两条通道都不归业务代码：布局 `ApplyLayoutRect`（`InkingWidget.h:860`，仅 `detail::layout::ArrangeSubtree` 是 friend，调用点 `src/layout/InkingLayoutBinding.cpp:48`）、内容自测量 `ApplySelfSize`（`:931`，private，仅 `InkingSpriteGroup` friend） | `InkingTextInput`（`text/InkingTextInput.h:65`） | **无**：默认不进命中、不吃点击（`InkingStaticLabel.h:27-28`）。派生类 `InkingTextInput` 是推（`:179,180`） |
| `InkingStaticPanel`<br>`panel/InkingStaticPanel.h:33` | **两趟**：`onRender`（`src/panel/InkingStaticPanel.cpp:286`，填充 + 阴影 + 滚动条）、`onRenderDecoration`（`:355`，描边在子孙之上）。都委托 `src/panel/PanelPaint.cpp:48` 那三个 helper | **只读** `GetWidth/GetHeight`（→ `ContentViewportWidth/Height` `:219-220`、`GetSubtreeClipBounds` `:213`）；不写几何。**但每帧改子树变换**：滚动走 `_scrollX/_scrollY` → `GetSubtreeTransform`（`:182`），不是几何 | FilePicker `prefab/FilePicker.h:69`、ListView `:62`、MenuBar `:68`、OptionBar `:63`、Select `:53`（全 `: public InkingStaticPanel`） | **推**：`onPointerWheel`（`:249`）、`onPointerPress`（`:254`）、`onPointerDrag`（`:257`） |
| `InkingFixedPopup`<br>`panel/InkingFixedPopup.h:68` | 两趟，**8 行 + 3 行薄包装**：`onRender`（`src/panel/InkingFixedPopup.cpp:234`）、`onRenderDecoration`（`:246`），同样走 `PanelPaint` 三个 helper | **既读也写**：落位走 `ChangeSelfAnchor` + `ChangeOffset`（`applyPlacement` `:180`）——唯一已在用动态几何入口的面板类 | `MenuPopup`（`prefab/MenuPopup.h:61`） | **拉**：`onPointerStateChanged`（`:158`）+ `detail::PointerProbe _probe`（`:204`） |

### A.2 「`PanelLike` 抽不抽」→ **不抽**

原假设（§2.3a）是"`FixedPopup` 与 `StaticPanel` 重复了同一套面板绘制，抽完 `PanelLike` 就能删掉它自己那份"。**实测推翻了这个假设**：

1. **绘制早就抽完了。** 填充 / 阴影 / 描边三笔都在 `src/panel/PanelPaint.cpp:48`（`PaintPanelAppearance` / `PaintSurfaceShadow` / `PaintSurfaceStroke`）。两个类的 `onRender` / `onRenderDecoration` 分别是 **8 行 / 3 行**的薄包装（`InkingStaticPanel.cpp:286-299`、`:355-362` 对 `InkingFixedPopup.cpp:234-249`），差异只有两处：取哪份外观（`_look.display*` vs `_appearance.*`）、传哪份变换（`_look.displayTransform` vs `TransformSpec{}`）。
2. **能抽的只剩那 11 行胶水。** 代价是给绘制加一层虚调用（`CurrentAppearance()` / `RenderTransform()`），并且要把 `PanelLook` 的两态语义压进同一个虚口。省 22 行，换每帧两次间接调用 + 一处新抽象。
3. **两档真正的差异不在绘制。** 是这三组：`PanelLook` 的状态机与过渡（`PanelLook.h`，314 行）、滚动（`_scrollX/_scrollY` + `UpdateScrollRange` + 裁剪/视口那 5 个 override）、容器命中（`OwnsSubtreeHit` `:191` / `HitTest` 递归 / `GetHitEnvelope`）。**`FixedPopup` 三组一个都没有**——它连 `PanelLook` 都没用，只持有 `PanelAppearance _appearance`（`:189`，单态）。
4. 因此 `InkingDynamicPanel` 应当是「`InkingDynamicWidget` + `PanelData` / `PanelLook` + 滚动/裁剪/命中三组接口」的**独立实现**，绘制照抄那 11 行。**不造 `PanelLike`。**

这与 Button 的处境不可类比：`InkingButtonLike` 承载 418 行（`include/button/InkingButtonLike.h`）带状态的状态机，抽出来收益巨大；Panel 这边没有等价物——**它的"状态机"已经在 `PanelLook` 里了，而这东西跟档位无关。**

### A.3 追加发现："要动态档"其实是三种不同的诉求，不能混成一句

MD-01 本来只回答"结构长什么样"。但读的过程中撞到一件事，它直接决定 `MD-02` / `MD-04` 该拆多少任务，必须先说清楚。

四个组件现在**都已经能"动"**，只是动的方式和代价不同：

| 组件 | 运行期"动"的现状 | 谁在付代价 |
|---|---|---|
| Image | 能拖、能缩（`SetDraggable` `:109` / `SetResizable` `:117`），走 A13 外部显示变换，不动 `_width/_height` | 见下面那条——**结构脏** |
| Label | 文字变了能改高度（`SetText` → `RelayoutParent`；无布局容器走 `RelayoutSelfIfContentSized` `InkingWidget.h:881`，有布局走 `ApplyLayoutRect`） | 布局重排 |
| Panel | 滚动每帧改子树变换（`GetSubtreeTransform :182`）；开合过渡走 `onAnimationTick :267` | 重绘 |
| Sprite | 帧动画在跑（`onAnimationTick :193`），换帧不改几何 | 重绘 + 纹理 |
| SpriteGroup | 换动作改自己尺寸（friend + `ApplySelfSize`，`InkingWidget.h:907`） | 结构 |

**关键在这里：A13 的每帧代价是结构脏。** `src/widget/InkingWidget.cpp:915-929` 写着，`SetDisplayTransform` 标的是**结构脏**——理由原文「任意变换是**无界**的，包络并集算不出来 ⇒ 只能让表跟着重烘一次」，代价原文「拖动期间**每一帧**都会重排绘制列表 + 重烘命中表」。

于是三种诉求必须分开答：

| 诉求 | 正确解 | 为什么 |
|---|---|---|
| **偶发**拖动 / 缩放（人手拖一张图） | A13 外部显示变换 | `ComponentGuide.md:277` 的判决说的正是这一种："不要为此选动态档" |
| **每帧**移动的实体（僵尸、子弹、粒子） | **动态档** | ~~用 A13 等于每帧重排绘制列表 + 重烘命中表；动态档不进表就是为这个准备的~~ **这句是错的，见下面的更正** |
| 尺寸 / 占位由业务在运行期改（撑开兄弟、被布局管） | **动态档** | 静态档那两条写尺寸的通道（`ApplyLayoutRect` / `ApplySelfSize`）都不是业务代码能调的 |

#### A.3 更正（2026-11-02 复核）：动态档**不省**每帧重建

上表第 2 行原来把"动态档"当成了"避免每帧重烘"的手段。**复核代码后这句话被推翻了**：

- 四个几何写入口（`Resize` / `ChangeSelfAnchor` / `ChangeTraceAnchor` / `ChangeOffset`）**全部**调 `MarkStructureChanged()`（`src/widget/InkingDynamicWidget.cpp:47,64,76,88`），而它做三件事：`MarkDirty()` + `MarkRepaintDirty()` + `NotifySceneStructureChanged()`（`src/widget/InkingWidget.cpp:505-516`）。
- `NotifySceneStructureChanged()` → `InkingScene::NotifyStructureChanged()` 把 `_contentDirty` 与 `_orderDirty` **一起**置真（`src/scene/InkingScene.cpp:611-616`）。
- 下一帧 `sortDrawList()` 见脏就 `_drawList.clear()` + 全量 DFS 重建 + `stable_sort` + `rebuildHitTable()`（`:787-826`）。

所以：**用 A13 移动**和**用几何入口移动**，代价完全一样——每帧一次整表重建。动态档"不进表"省掉的是**表项**与**查询路径**（它自己在洞里判），**不是**那次重建。

那动态档到底买什么？两条，都与性能无关：

1. **占位**：几何参与锚点 / 布局 / 兄弟让位，A13 只动画面不动占位（`ComponentGuide.md:277` 的"不改变布局占位"）。
2. **命中语义**：动态组件自判命中、占地记"洞"，可以让"移动中的东西"被点到而不用把任意变换塞进静态表。

**这是本仓库当前的成本上限**：只要有节点每帧动，场景就每帧重建一次列表与表。（2026-11-02 已立项为 `TaskGuide.md` 的 **`T-19`**：叶子只刷洞；实测数据见 `TaskGuide-G.md` §2.1.2。）PvzAdventure 若有几百个移动实体，这条会变成主瓶颈。`T-19` 只处理**叶子**那条（动态容器带静态子孙仍全量重建）；更往下的"增量重建 / 移动节点专用批"仍是场景架构级的事，要另立项。`TaskGuide-G.md` 的 §2.1 记了这条，`§7` 写明 G 系列不碰。

**对 PvzAdventure 的预判**：它是**实体层**（僵尸、植物、子弹每帧走位），几乎肯定落在第 2 种 ⇒ 动态档**确实需要**，初稿的方向没错。但"四个组件都要"仍需逐个确认——Image / Sprite 是每帧走位（需要），Label / Panel 更像第 3 种（要，但理由不同：它们要的是"内容/尺寸驱动本体"，不是"每帧位移"）。

所以 A.3 的结论不是"砍任务"，而是**每拆一个任务都要能说出它属于上表哪一行**。说不出来的任务，`MD-04` 里就该删掉。**这是我在 MD-01 里最想让下一个人看到的东西。**

## 附录 B：MD-02 执行结果（2026-11-02）

### B.0 一句话结论

**`Like` 模式能复制，但顺序要反过来：先复制第二个实现，再抽 `Like`。**

依据是仓库自己的历史。`InkingButtonLike.h:13-16` 写着它为什么存在：

> 这三样原先长在 `InkingStaticButton` 上，想复用只有继承那个**具体类**一条路……`InkingStaticImage` 就是受害者：它在自己的头里把 `GetAppearance` / `GetDisplayColor` / `GetDisplayTransform` 又抄了一遍。

也就是说 —— **Button 的 `Like` 是"先有第二份复制品、再抽出来"的产物，不是"先设计接口、再写两份实现"。** 而我现在没有第二份 Image / Label 实现，抽出来的 `ImageLike` 边界只能靠猜；猜错的代价是成员要在新旧基类之间搬两次。

这直接推翻 `MD-04` 原来的"先抽 Like、再加动态档"（见 §B.4）。

### B.1 六项差异逐项判定（修订版）

| # | 差异 | 静态档 | 动态档 | 谁承担 | 复制成本 |
|---|---|---|---|---|---|
| 1 | 几何写入口 | 无 | 4 个 | **档位**（`InkingDynamicWidget`） | 零：继承即得 |
| 2 | 命中 | 进表 | 不进表，占地记"洞" | **档位** | 零：登记时按档分流 |
| 3 | 输入 | 场景**推** `onPointerPress/Drag/Wheel` | 场景推通知 `onPointerStateChanged`，自己拉 | **组件**（`PointerProbe` 已抽好） | **30–56 行 / 组件**，见 B.2 |
| 4 | 每逻辑步 Tick | 不收 | 收 `onTick` | **档位**（`InkingScene.cpp:262-279` 推，带结构代次保护） | 零 |
| 5 | 渲染帧 `onAnimationTick` | 收 | 收（**两档相同**） | 各组件自己接自己的过渡 | 低：一行推进 + 两行标脏 |
| 6 | 默认 `onRender` | 各写一份实心矩形（`InkingStaticWidget.h:30` / `InkingDynamicWidget.h:36`） | 同左 | 与抽取无关 | **不用动**：两份实现都只是一行 `detail::FillDefaultRect`（`src/widget/InkingStaticWidget.cpp:22-25` / `InkingDynamicWidget.cpp:105-110`），真正重复的那件事早就在 helper 里。两个 override 合不了——两档是兄弟，没有共同的可借用父实现（`InkingDynamicWidget.cpp:107-108` 的原注释就在说这个） |

真正的工作量全在第 3 项。初稿以为"差异只有几何"，第 3 项被完全漏掉——而它恰好是四个组件里工作量最大的一条。

### B.2 可复制 / 需改造 / 复制不过去

**可直接复制（已有 ≥2 个消费者，等于已被验证）**

| 东西 | 现状证据 |
|---|---|
| 4 个几何入口，类型无关 | 全部调用方只有三处：`src/button/InkingDynamicButton.cpp:97-99`（装配置）、`src/panel/InkingFixedPopup.cpp:138,159`（落位）、`examples/demo/main.cpp:149`（自走）。都只要求"是个动态组件" |
| `detail::PointerProbe` | **已有两个消费者**：`src/button/InkingDynamicButton.cpp:204` 的 `JudgePointer`、`src/panel/InkingFixedPopup.cpp:255` 的 `onPointerStateChanged`。同一份边沿判定被两个不同组件用过 = 它已经过一次验证 |
| 每逻辑步 Tick 的派发 | `src/scene/InkingScene.cpp:262-279`：复制动态节点列表 → 逐个 `Tick(fixedStepSeconds)` → 结构代次一变就停。新组件不用管 |
| 指针变化的通知时机 | `src/scene/InkingScene.cpp:1321, 1443`：只在 `_pointerDirty && !_pointerNotified` 时推，且只推给 `IsDynamic() && IsVisibleInTree()` 的节点 |

**需要改造（每个组件一份，同类不同形）**

1. **`JudgePointer()` 不能直接复用。** `src/button/InkingDynamicButton.cpp:204-259`（56 行）里调的是 `MouseHover` / `MousePress` / `TriggerClick` / `TriggerContextClick` —— 四个成员全来自 `InkingButtonLike`。Image / Label / Panel 没有三态，这四个调用必须换成本组件的动作（Image 换"开始拖/结束拖"，Panel 换"拖滑块/切滚动轴"）。
   - 但其中 `scene == nullptr` 那段（`:206-215`）**必须照抄**：退回"没被指到" + `_probe.Reset()`。不抄的症状是"组件移出场景再移回来，带着一份过期的悬停状态"——不报错。
   - `_probe.Update(hit, down)` 那段（约 20 行）是通用的，可照抄。
2. **Image 的拖 / 缩是最大的改写点。** 现版本在**推**通道里拿现成增量：`onPointerPress`（`src/image/InkingStaticImage.cpp:312`）、`onPointerDrag(designX, designY, deltaX, deltaY)`（`:332`），并且按下点靠"首次拖动的 `designX - deltaX`"反推（`InkingStaticImage.h:222-227`）。动态档没有推 ⇒ **增量要自己算**（存上一次 `GetPointerX/Y`），"首次反推按下点"那段逻辑作废重写。这不是复制，是重写。
3. **Panel 的滚动三件套全部失效，且有一个真坑。**
   - `onPointerWheel`（`InkingStaticPanel.h:249`）在动态档收不到 ⇒ 改 `onTick` 里读 `GetPointerWheelX/Y`（`InkingScene.h:522-523` 已备好这个口，`TaskGuide.md:116-117` 的 A4 偏离写的正是这条路）。
   - **坑**：滚轮推给静态目标后**不清零**，原文见 `src/scene/InkingScene.cpp:1641-1642`「派发之后**不清零**：动态组件要在自己的 `Tick` 里读它」。而推给动态目标时，`dispatchTarget` 直接返回 nullptr（`src/scene/InkingScene.cpp:1341`：`if (node == nullptr || node->IsDynamic()) return nullptr;`）。两边合起来的后果是：**动态面板拉到的滚轮没有归属仲裁**——它必须自己判"指针在不在我身上"，否则会和被推出的静态滚动容器**同时滚动**。
   - 更硬的一条：`HandlesSubtreeWheel` / `FindSubtreeWheelOwner`（`InkingStaticPanel.h:195`）是**推通道**的机制（沿父链找最近的声明者，然后在 `dispatchTarget` 之后 `PushPointerWheel`）。动态档不进派发 ⇒ 这两条对动态面板完全无效，滚动归属要在动态面板里重做。

**复制不过去**

- **`InkingButtonLike` 本身搬不过去，也不该搬。** 它承载三态 + 过渡状态机（`_look`，`:406`）+ 三个事件槽（`clicked :123` / `contextClicked :140` / `stateChanged :163`）+ 六个观测口 + 两个 `[√]` 钩子。而 Image / Label / Panel 都没有三态：Image "只认 `normal` 一态"（`InkingStaticImage.h:23-24`），`FixedPopup` 只认 `closed` 那一份（`InkingFixedPopup.h:189` 持单态 `PanelAppearance`）。照抄它的形状，只会造出四个**没有状态的状态机**。
- 所以 `ImageLike` / `SpriteLike` / `LabelLike` 是**另一种形状**的 `Like`：装数据 + 观测口 + 绘制入口（+ 拖拽/文字测量这类自有逻辑），**不装状态机**。能照抄的是"不带状态的基类 + 多继承"这条规矩，以及"Like 自己一个字都不标脏、只调钩子把变化报给组件"（`InkingButtonLike.h:64-72`）这条纪律。

### B.3 `detail::PointerProbe` 归属 → **保持 `detail/`**

四条理由：

1. **它承诺的是时机，不是接口。** 正确用法绑死在"指针真的变了的那一帧、由 `onPointerStateChanged` 唤醒"（`InkingPointerProbe.h:40`）。升进 `util/` 就要把这条时机也冻进承诺，而时机是场景派发策略（`InkingScene.cpp:1321, 1443` 的 `_pointerDirty` 那套）的一部分——场景优化一次就要改承诺。
2. **组件是库的一部分。** `PointerProbe` 的消费者是库内组件，不是业务代码。`util/` 的价值是"对外承诺"，这里没有对应的对外消费者。
3. **外部作者不是没有路。** 自定义动态组件的公开面已经够：`onPointerStateChanged` + `GetPointerX/Y` + `GetPointerDown` + `HitTest`，自己写 30 行即可；`InkingPointerProbe.h:77-85` 的注释本身就是一份可照抄的样例。
4. **代价不对称。** 收益是外部作者省 30 行；代价是冻结一个时序敏感的类 + 承担它的 ABI。不划算。

**复查触发条件**（写下来免得以后反复争）：出现**第二个**外部项目（PvzAdventure 之外）抱怨"自定义动态组件要自己重写输入"，再考虑升 `util/`；只一个不算。

### B.4 对 `MD-04` 的影响：执行顺序要反过来

原规则（`MD-04` 第 4 步）："先抽 `Like`、再加动态档——顺序不能反"。

按 B.0 的证据，**这条要改成"复制 → 抽取"，且两笔必须相邻、各自单独验收**：

| 笔 | 内容 | 性质 |
|---|---|---|
| 第 1 笔 | 写 `InkingDynamicImage`，**直接复制**静态版的数据与绘制调用（不改行为，静态版一个字不动） | 新增行为，但只新增 |
| 第 2 笔 | 把两份共用的部分抽成 `ImageLike`，两个类都改成继承它 | **纯重构**，行为零变化，可用"两版截图 + 自检计数不变"验收 |

好处与原来的规矩一致（重构与行为变更不混在一个提交里），而且多了一条原规矩给不了的：**第 2 笔的边界是被第 1 笔的真实需求逼出来的，不是猜的。**

`MD-04` 的验收相应加一条：每个组件的任务必须是相邻的两笔，第 2 笔的提交说明里必须写"纯重构"。

## 附录 C：MD-03 执行结果（2026-11-02）

### C.0 一句话结论

**兼容策略 = 零改名。** 原稿的 A / B / C 三选一在回答一个不该问的问题——四个类里三个的名字里**早就带着 `Static`**，加动态档根本不需要动它们。唯一没有前缀的 `InkingSprite` 也建议保留（理由见 C.3）。

### C.1 引用点盘点

| 类名 | 引用处 | 文件数 | 其中 `examples/demo/selftest.cpp` |
|---|---|---|---|
| `InkingStaticImage` | 95 | 18 | 24 |
| `InkingSprite` | 176 | 21 | 58 |
| `InkingStaticLabel` | 71 | 14 | — |
| `InkingStaticPanel` | 120 | 19 | — |

按"谁改得动"分五层：

| 层 | 落点 | 兼容负担 |
|---|---|---|
| A 库内部 | `include/`、`src/`（含 `src/inking.cpp:148-158` 的静态档断言、`src/image/InkingSpriteGroup.cpp`） | **无**：自己改 |
| B **代码生成器** | `tools/inkgen/Emit.cpp`（初稿完全没提这一层） | **硬约束**，见 C.2 |
| C 示例 | 6 个工程：`demo/main.cpp`、`demo/selftest.cpp`、`minimal/`、`sharp/`、`atlas_frames/`、`pet/` | 跟着改，但零改名的话一处都不用动 |
| D 外部 | PvzAdventure；**以及下游项目里已经生成并提交的 `.h` / `.cpp`** | 改不动，只能不破坏 |
| E 文档 | `docs/zh/api/*.md`、`ComponentGuide.md`、`CXXCSS.md` | 要同步 |

### C.2 决定性发现：生成器把基类名写死在生成的代码里

`tools/inkgen/Emit.cpp` 输出的类定义里，基类是**字面量**：

| 类型 | 行 | 生成的文本 |
|---|---|---|
| Panel | `:1260`、`:1267` | `: public inking::InkingStaticPanel` |
| Label | `:1296`、`:1303` | `: public inking::InkingStaticLabel` |
| Image | `:1321-1322`、`:1331-1332` | `inking::InkingSprite` / `inking::InkingStaticImage`（按 `isSprite` 二选一） |

后果很具体：**下游项目里已经生成并提交的 `.h` / `.cpp` 不会跟着重新生成。** 所以旧类名必须继续存在、且是同一个东西——这不是客气，是硬约束。

好消息是：**零改名方案自动满足它**，生成器一行都不用改，重新生成的产物和旧的逐字相同。反过来，任何改名方案（B / C）都会让下游"重新生成 vs 不重新生成"两条路都难受。

生成器里已经有一条档位分支可以照抄形状：`Emit.cpp:1214` 的 `button.dynamic` 决定基类（`CXXCSS.md:881` 有文档）。给 Image / Sprite / Label / Panel 加 `dynamic` 是**新功能**，不是兼容问题，别混着做。

### C.3 零改名可行吗：三个本来就带 `Static`，一个不带

- **`InkingStaticImage` / `InkingStaticLabel` / `InkingStaticPanel`**：名字已经是档位。抽 `Like` 只是**多一个基类**，构造函数、观测口、事件槽全不动 ⇒ 公开面完全不变 ⇒ 调用方**零改动**。
- **`InkingSprite`**：没有前缀。三条路：

| 选项 | 做法 | 代价 | 判定 |
|---|---|---|---|
| X | 保持 `InkingSprite` = 静态档，另加 `InkingDynamicSprite` | 命名不对称（其余三个都带 `Static`） | **选它** |
| Y | 改名 `InkingStaticSprite` + `using InkingSprite [[deprecated]] = InkingStaticSprite;` | 176 处引用（selftest 58 处）都要碰一遍，下游生成代码每次编译都报警告 | 收益只是命名整齐 |
| Z | 改名，旧名消失 | 下游直接编不过 | 否 |

选 X 的三条理由：

1. **它今天就是静态档**——`InkingSprite : InkingStaticImage`（`InkingSprite.h:61`），`src/inking.cpp` 的断言把它钉在静态那一档上，"不带动态几何入口"是它的事实属性，不是我的猜测。
2. **改名要动 176 处，收益是整齐**——而整齐可以用一行注释买到（`InkingSprite.h` 顶部写清"本类 = 静态档；要几何可变用 `InkingDynamicSprite`"）。
3. **按 B.4 同一条纪律**："加动态档"和"重命名"不该混在一个改动里。真要整齐，留到 0.9 的破坏性窗口单独做一次，那时才付得起重新生成 + 全下游改名的账。

### C.4 对照表（旧类名 → 新结构）

| 旧名 | 新结构 | 调用方改什么 | 生成器改什么 |
|---|---|---|---|
| `InkingStaticImage` | `InkingStaticWidget` + `ImageLike`（公开面不变） | **零** | **零** |
| `InkingSprite` | 同上，走 `SpriteLike`，仍是静态档 | **零** | **零** |
| `InkingStaticLabel` | `InkingStaticWidget` + `LabelLike` | **零** | **零** |
| `InkingStaticPanel` | **不变**（A.2 判定不抽 `Like`；`InkingDynamicPanel` 独立实现） | **零** | **零** |
| `InkingSpriteGroup` | 不变（挂 `InkingSprite`，即静态档）——§2.3b 的"悬空"问题由此消失 | **零** | **零** |
| `InkingTextInput` / FilePicker / ListView / MenuBar / OptionBar / Select | 不变 | **零** | **零** |
| `InkingFixedPopup` / `MenuPopup` | 不变 | **零** | **零** |
| **新增** | `InkingDynamicImage` / `InkingDynamicSprite` / `InkingDynamicLabel` / `InkingDynamicPanel` | — | 加 `dynamic` 支持（新功能） |

所以原稿那张"兼容表"整张作废：不是"每条注明零改动 / 需要改"，而是**一条都不用改**。原稿的 `[[deprecated]]` 前置策略也随之取消——没有要废弃的名字。

### C.5 零改名不等于零风险：三条真约束

1. **`Like` 里不许有纯虚函数。** 生成类会多继承它；基类有纯虚 ⇒ 下游生成的类**变成抽象类**，报错落在"实例化"那一行，看不出是基类的问题。`InkingButtonLike` 的两个钩子都是**带空实现**的虚函数（`:384`、`:400`），新 `Like` 照抄（也符合"契约头必须声明即定义"那条老规矩，见 §2.2 同源的静默失效清单）。
2. **静态档不能顺手继承 `InkingDynamicWidget`。** 诱惑很大（"两档都继承就都有了"），但会同时破坏命中表登记和 `src/inking.cpp:148-158` 的断言。那条断言正是拦这个的——所以 `G` 系列每加一个动态类，要补**一对镜像断言**（`is_base_of_v<InkingDynamicWidget, InkingDynamicXxx>` + 静态版的反向断言）。这不是形式主义，是给"零改名"织安全网：名字没变的代价是**类型关系必须被钉死**。
3. **`CXXCSS.md` 有两句会过期**：`:1119`「Panel 只有静态档」、`:1450`「Image 没有 dynamic」。零改名不需要动生成器，但**要动这两句**，否则下一个人按文档写 `"dynamic": true` 会以为自己写错了。

### C.6 对 `MD-04` 的影响

- 「兼容」一节从"三选一 + 理由"缩成一句：**零改名，见附录 C**；`[[deprecated]]` 不再出现。
- 但 `G` 系列要多出三条验收，对应 C.5 的三条约束：新 `Like` 无纯虚函数、每个新增动态类有一对镜像 `static_assert`、`CXXCSS.md` 两句已更新。
- 原稿"给薄派生类标 `[[deprecated]]`"这个风险条目整条删掉——不是风险降低了，是**这个风险不存在了**。
