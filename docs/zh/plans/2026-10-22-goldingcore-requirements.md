# 任务书：GoldingCore 内置能力需求（A/B/C/D 四组）

> 状态：**A / B / D 组已落地，C 组暂缓**（2026-10-22，本轮）  
> 来源：`D:/GoldingCore/docs/inking-requirements.md`（提出方：GoldingCore core 层）  
> 目标版本：`0.4.0`（上一批 0.3.0 是字体回退链与静默失效巡检）  
> 涉及层：`scene/`（主）、`widget/`、`button/`、`layout/`、`window/`、`prefab/`、`examples/demo/selftest`

写法沿用 `docs/zh/plans/2026-10-22-feedback-upgrade-tasks.md` 的三段式与"完成即压缩"
（完成的条目只留一行，细目交给 commit 与代码注释）。本轮**全部条目已在本轮内完成，
因此 §2 全是压缩行**——设计取舍另放 §3"定案"，那一节是给下一个人看的。

---

## 0. 结论（TL;DR）

18 条里 **15 条成立并已修、3 条暂缓（C 组全部）**。核实过程中发现**三处比反馈判断更深一层**
的根因，写在这里，因为它们决定"改哪里"：

| 反馈判断 | 实测根因 | 结论 |
| --- | --- | --- |
| A2/A3「树变了收集表不跟上」 | 收集表是**增量登记**的：构造时沿父链登记一次；`AddChild` 只登记**被挂的那一个**，它的子孙不进 | 修在"按树重建"，不是逐个补登记 |
| A4「`fillMain` 只在首次收敛」 | 不只是布局不重跑——**按钮的 `GetWidth()` 读的是 `ButtonLook` 里那份冻结的基准尺寸**，布局写进 `_width` 之后观测口还是旧值 | 两条都修：布局触发点 + `SyncBaseSize()` |
| A1「动态组件不进命中表」 | 动态组件的**子孙**是进表的（表里只有它自己是"洞"）；断的是 `HitTestNode` 的递归——基类只返回自己，只有 `InkingStaticPanel` 重写了一份 | 把递归放进基类，而不是给动态档再抄一遍 |

三条"报告里没有、但同源"的发现：

1. **`RenderToTexture` 的落盘路径本身不可靠**：`SDL_SavePNG(surface, path)` 在受限环境里
   会把文件名截断（写出 `probe_o`、返回 true）。这是"成功但没产出"的典型，改成"先编码到
   内存、再 `fopen`（失败才退回 SDL 文件层）"。顺带修好了既有的截图路径。
2. **`PumpOnce()` 会静默丢帧回调**：时间线的两条通道原来只挂在 `runLoop` 里，非阻塞入口
   漏挂 ⇒ "帧在跑、场景一帧都没收到"。已移到 `stepFrame` 的初始化里（两条路共用）。
3. **`MenuBar` 的 B3 其实已有观测口**（`IsVisibleInTree()` + `GetHeight()`），缺的是
   "当前占多少"这一个便捷口与一句文档。补 `GetCurrentHeight()`（隐藏时为 0）。

---

## 1. 逐条裁定

| 编号 | 反馈内容 | 裁定 | 落点 |
| --- | --- | --- | --- |
| A1 | 动态组件不进命中表 | **成立**（断在递归，不是建表） | `InkingWidget::HitTestNode` |
| A2 | 事后 `AddChild` 的子树不进渲染列表 | **成立**（子树只登记一层） | `InkingScene::collectDrawList` |
| A3 | 创建时隐藏、之后显示也不渲染 | **成立**（与 A2 同源） | 同上 |
| A4 | `fillMain` 只在首次收敛 | **成立**（两条根因） | `AddChild`/`Resize` + `ButtonLook::SyncBaseSize` |
| A5 | 要显式 `RebuildRenderList()` | **成立** | `InkingScene::RebuildRenderList` |
| B1 | 交叉轴拉伸 | **部分成立**：`Stretch` 早就实现了，**是布局没跑** | 同 A4 |
| B2 | 动态组件缺名字入口 | **成立** | `InkingWidget::SetName` + `FindByName` |
| B3 | 顶栏当前高度可查 | **成立**（缺便捷口与文档） | `MenuBar::GetCurrentHeight` |
| C1 | `0x00000000` 渲染成黑块 | **成立，暂缓** | 见 §4 |
| C2 | 按钮文字对齐不可控 | **成立，暂缓** | 见 §4 |
| C3 | 缺字形（`☰` 画方框） | **成立，暂缓**（资产问题） | 见 §4 |
| D1 | 非阻塞跑帧 | **成立** | `Open`/`PumpOnce`/`RunFrames` |
| D2 | 离屏渲染 | **成立** | `RenderToTexture` |
| D3 | 帧内调度 | **成立** | `PostFrameTask`/`RunAfterFrames` |
| D4 | 确定性时间步 | **成立** | `SetFixedDelta` |
| D5 | `RequestClose(code)` | **成立** | `RequestClose(int)` + `Show()` 返回 int |
| D6 | 命中表可强制构建 | **成立**（`QueryHit` 其实已能提前用） | `RebuildHitTable` |
| D7 | 点击能力的基类抽象 | **成立** | `InkingWidget::Activate` + `InkingScene::Activate(名字)` |

---

## 2. 已落地（按 §1.2 规则压缩，每项一行）

- A1 动态容器子树可命中-已完成（2026-10-22，`HitTestNode` 基类改成子树优先的递归；"是容器"只需 `OwnsSubtreeHit`）
- A2 事后 `AddChild` 的子树进列表-已完成（2026-10-22，收集表在内容脏时**按树先序 DFS 重建**，逐层都进）
- A3 隐藏过再显示能渲染-已完成（2026-10-22，与 A2 同一处修好：可见性仍在提交时判，列表不再丢条目）
- A4 `fillMain` 随尺寸重收敛-已完成（2026-10-22，`AddChild`/`RemoveChild`/`ClearChildren`/`Resize` 触发重排；按钮另补 `SyncBaseSize`）
- A5 显式重建入口-已完成（2026-10-22，`RebuildRenderList()` 返回项数；正常路径不需要它）
- B1 交叉轴拉伸-已完成（2026-10-22，`Stretch` 实现本来就在，缺的是"挂了孩子就跑一趟布局"）
- B2 `SetName`-已完成（2026-10-22，不标脏；配 `FindByName` 深度优先先序）
- B3 顶栏当前高度-已完成（2026-10-22，`MenuBar::GetCurrentHeight()`：全屏隐藏时给 0）
- D1 非阻塞跑帧-已完成（2026-10-22，`stepFrame` 从 `runLoop` 拆出，`Show`/`PumpOnce` 共用）
- D2 离屏渲染-已完成（2026-10-22，surface+软渲染器走同一条 `RenderScene`；落盘改内存编码）
- D3 帧内调度-已完成（2026-10-22，帧末任务排在脏消费之后、场景析构之前）
- D4 确定性时间步-已完成（2026-10-22，固定 delta 下不节流，CI 跑 100 帧不用等 1.6 秒）
- D5 `RequestClose(code)`-已完成（2026-10-22，`Show()` 返回退出码；被闸门否决时不改码）
- D6 命中表强制构建-已完成（2026-10-22，`RebuildHitTable()`；窗口没开也能建）
- D7 点击基类化-已完成（2026-10-22，`Activate()` 虚函数 + `InkingScene::Activate(名字)`）

---

## 3. 定案（别翻案）

1. **收集表以树为准，不再维护增量登记**。`_scene` 由 DFS 认领，`AddDrawNode` 退化成"标脏"。
   理由是 A2/A3 的洞补不完：任何"漏登记"的路径都会复现同一个症状，而树是现成的。
   代价是每次结构脏 O(N) 遍历——与"重算所有静态坐标快照"同一量级，原来就要付。
2. **"是容器"只需要 `OwnsSubtreeHit()`**。`HitTestNode` 的递归进基类，派生类**不再需要重写**；
   重写仍合法（`InkingStaticPanel` 保留了自己那份，因为它还要判裁剪与 `_look` 变换）。
3. **骨架变化触发重排，内容变化不触发**。`InkingWidget.h` 原来那句"框架不做隐式重排"收窄为
   "框架不管**内容**变化"——文字改长仍要自己喊 `RelayoutParent()`，但"挂了个孩子"必然
   伴随一次布局，否则 `fillMain` / `Stretch` 全是静默 0。
4. **按钮显示尺寸必须在 `onSizeChanged()` 里同步**。`GetWidth()` 读 `ButtonLook::DisplayWidth()`，
   而布局写的是 `_width`：两份数据、一个观测口。同步点只能放在钩子里（那是"几何真的变了"
   的唯一落点），别在每个写入口各写一遍。
5. **`stepFrame` 是唯一的一帧定义**。`Show()` / `PumpOnce()` 都只允许调它，不许各自抄一份顺序
   ——抄一份就会"自检过了、真机不过"。
6. **时间线通道在 `stepFrame` 的初始化里挂**，不在 `runLoop` 里。`runLoop` 只是循环。
7. **图片落盘 = 编码到内存 + 自己写文件**。SDL 的文件层在受限环境里不可靠（见 §0 第 1 条），
   而 `fopen` 在 Windows 上又不认 UTF-8 路径——两条路互补，缺一条就有一类环境存不出图。

---

## 4. 暂缓（C 组，附前置条件）

| 编号 | 为什么暂缓 | 落地前置 |
| --- | --- | --- |
| C1 `0x00000000` 要"真透明" | 现在的品红兜底是 **T-03 的护栏**（防"颜色串写漏 → 整块看不见"）。要真透明必须给一条**显式**通道，不能靠 alpha==0 猜意图 | `PanelData`/`ButtonData` 加 `fillBackground`（默认 true）+ `inkgen` 白名单 + `CXXCSS.md` §9 字段表重生成（`ctest field_table_in_sync` 会红） |
| C2 按钮文字对齐 | 文本层**已有** `TextAlignH`/`TextAlignV` 枚举与 `MeasureText` 对齐起点，但按钮只走"居中画"这一条，`ButtonData` 没暴露对齐字段 | 把已有的对齐枚举接到按钮的排版路径上，`ButtonData`/`LabelData` 暴露两个对齐字段 |
| C3 `☰` 画方框 | **字体资产问题**，不是代码问题：默认字体没有 U+2630 | 由应用侧配 `text.paths`（0.3.0 已支持回退链）挂一个含该字形的字体；或换 `≡`（U+2261） |

三条都**不影响 A/D 组收益**：核的 A 组绕路代码（`requestRebuild` 满天飞）现在可以删掉。

---

## 5. 与改动前对比

| 维度 | 改动前 | 改动后 | 改动后**仍不变**的边界 |
| --- | --- | --- | --- |
| 收集表来源 | 构造期增量登记 | 结构脏时按树 DFS 重建 | 提交顺序仍是 `IsAbove` 反序（树序 + 兄弟 z）；隐藏条目**仍留在表里**，只是提交时跳过 |
| 动态容器里的静态控件 | 收不到任何输入 | 与静态容器里的控件同权（hover/点击/焦点） | 动态组件**自己**仍不进表（仍是"洞"），仍由 `topmostDynamicHit` 现问 |
| 容器尺寸变化 | 只能重建整棵树 | `Resize` 后孩子自动重排 | 静态组件的"几何构造即定型"没变——`Resize` 仍只在动态档 |
| 布局触发 | 只有构造期 / 手工 `Relayout()` | 骨架变化（增删孩子）+ 动态 `Resize` 自动触发 | **内容**变化（文字改长、列表项换文案）仍要自己喊 `RelayoutParent()` |
| 按钮 `GetWidth()` | 可能长期是配置值 | 跟随布局结果 | 有"每态尺寸"动画时仍以动画插值为准（`SyncBaseSize` 不覆盖它） |
| 运行模型 | `Show()` 阻塞到底 | `Open` + `PumpOnce`/`RunFrames` 可选 | `Show()` 仍是"打开 + 一直跑到关窗 + 释放"，语义未拆 |
| 截图落盘 | 直接交给 SDL 文件层 | 编码到内存再写（fopen → SDL 兜底） | 读的仍是整个帧缓冲、`.png` 仍需 SDL ≥ 3.4、其它扩展名写 BMP |
| 自检顺序 | 全部在裸窗口生命周期内 | D 组移到裸窗口销毁**之后** | 前面的检查一条没动（217 项 ok，失败 0） |

### 验收

- `inking_demo_selftest`：**217 项 ok / 失败项 0**（新增 26 项：A 组 15、D 组 11）。
- `ctest`：**4/4 通过**（`inkgen_validate_cxxcss` / `field_table_in_sync` /
  `inkgen_compile_check` / `silent_failure_list_in_sync`）。
- 探针复核（库外链接发布包）：A1~A5/B1 五组断言全绿，见 commit message 里的 `probe*.cpp` 输出。

---

## 6. 给 GoldingCore 的迁移提示（删绕路代码的顺序）

1. **先删 `requestRebuild` 的调用点**（抽屉展开 / 收起、覆盖层显隐、换页），改成
   `SetVisible()` / `AddChild()` / `Resize()`。A2/A3/A4 之后这三条都不需要重建整棵树。
2. **把 `kTopBarHeight = 40` 换成 `menuBar->GetCurrentHeight()`**，内容区高度
   `窗口高 − 该值` —— `architecture.md` §8 的全屏留白就此消失。
3. **容器的宽度魔数（1224 / 1080 / 200）可以删**：给容器写 `layout`，孩子不写宽度即可
   （`Stretch` 是默认的交叉轴对齐）。
4. **自检从 `dynamic_cast<InkingStaticButton*>` + `TriggerClick()` 改成
   `scene.Activate("名字")`**；退出码走 `RequestClose(failures)` + `Show()` 的返回值。
5. **SelfTest 做成插件**（需求单 §5）的前置条件现在齐了：D1（跑帧）+ D3（帧末钩子）+
   D5（退出码）+ D7（不认类型就能点）。剩下的是 core 侧的两阶段装配与 `onReady`，不在本库。
