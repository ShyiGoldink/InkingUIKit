# 变更记录（CHANGELOG）

格式：每个版本一节，分 **新增 / 变更 / 修复 / 文档 / 已知限制** 五类。
测试版本期间的条目**不承诺**跨版本兼容——升级前请逐条对照。

---

## 未发布 —— 局部层级（A8b）

### 变更

- **`zIndex` 的语义从"全场景一条"改为"兄弟之间"**（局部层级）。`InkingWidget::IsAbove`
  现在按"树序 + 兄弟内 z"比较：① 父容器先于自己的子孙画；② `zIndex` 只在同一父下生效；
  ③ 同 z 仍看注册序号。
- **修复一类静默失效**：以前"面板 `zIndex:20`、里面的标签 `zIndex:1`"会让面板底色
  盖住整块内容，且不报任何错（外部使用方实测：面板区最大亮度 38，调完 z 才回到 255）。
  现在子的 z 再小也在父之上。
- 仍是**一个严格全序**，所以绘制、命中表、面板子树仲裁、动静合并四处共用同一个比较器，
  "画的是 A、点到的是 B"这条不变量没变。
- **日志改为纯文本事实源**（与 InkingBackendFramework 的 `ShineLog` 同步）：
  - 运行期只写 `LOG/<日期>/run-<时分秒>-<pid>.log`，一次运行一个文件，`LOG/latest.txt`
    指向最近一次；**不再写 `Log.html`**，热路径上不再有 CSS/JS 模板。
  - 每行 7 段自带定位：`时间 | 级别 | 模块:阶段 | 序号 | 线程 | 文件:行 | 消息`；
    第 3 段固定为 `模块:--`（前端无阶段概念，保留该列以便与框架端共用解析/报告工具）。
  - 连续相同记录折叠成一行 `FOLD`（带重复次数与起止时间）；同一个键每秒最多 5 条；
    `ERROR` 不折叠，一条一行。
  - 浏览器视图改由 `InkLog::report()` 按需把 `.log` 渲染成自包含 HTML（双击即开）。
  - `InkLog::logFilePath()` 现在返回**当前 `.log` 路径**（首次调用会创建文件与目录）；
    新增 `logDirectory()` / `tail()` / `grep()` / `statistics()` / `setLevel()` /
    `parseLevel()` / `mark()`，以及 `INK_LOG_MARK` 宏（伪级别 MARK，不受阈值影响）。
  - `INK_LOG_*` 宏改为把 `std::source_location::current()` 传下去，日志里的"文件:行"
    指向业务代码而不是 `InkLog.cpp`。

### 新增

- **运行期主题（颜色）**：`theme/InkingTheme.h`（`InstallThemeTable` / `SetActiveTheme` /
  `ThemeVersion` / `ResolveThemeColor` / `ThemeStatsSnapshot` / `ClearThemeTables`）。
  生成期仍按**烘焙主题**把 `$token` 换成字面量（= 默认色），同时把 token 名留在
  `ButtonAppearance::themeColor` / `PanelAppearance::themeColor` / `LabelData::colorToken`
  里，运行期按当前主题重查 ⇒ `SetActiveTheme("light")` 一句话换肤。
- **多份主题**：`CXXCSS/Theme/*.json` 可以有多份，`"default": true` 指定烘焙主题
  （都没标则按文件名排序取第一份，并打一条警告）；生成物多出
  `InstallGeneratedThemes()` / `kDefaultThemeName` / `kThemeCount`，启动期自动安装。
- 生成期新增**跨表校验**：每份主题都必须覆盖组件用到的全部颜色 token——少一个的表现是
  "切过去那一处还是旧色"，在构建期就报错。
- 自检新增「运行期主题」18 项：装两张表 / 切表 / 切失败不动版本 / **真渲染像素**在
  蓝→红→蓝之间切换 / 缺 token 用兜底色并记 miss / 清空回到字面量。
- `inking_demo_selftest` 的「层级」段（19 项）：父子 / 兄弟 / 跨子树三种关系、绘制列表
  顺序与 `IsAbove` 同源、中心点命中子面板、**真渲染一帧后读回中心像素**。
- **截图能力（P2-6）**：`InkingWindow::RequestCapture(path)` / `Capture(path)` /
  `HasPendingCapture()`。`.png` 走 `SDL_SavePNG`（SDL ≥ 3.4）、其它走 `SDL_SaveBMP`，
  **不新增 SDL3_image 依赖**。`RequestCapture` 排队到本帧末（`Present` 前）执行，
  所以在按钮回调里也能用；`Capture` 立即读当前渲染目标。
- **环境变量 `INK_CAPTURE=<路径>`**：启动后第一帧零代码落一张图。排错时"先看一眼画面"
  比加日志快——外部使用方当初为此自写了 `SDL_RenderReadPixels` + 纯 Python 的 BMP→PNG。
- 自检新增「截图：失败路径」4 项：空路径 / 无渲染器都必须返回 false，
  且**不许留下永远不落盘的排队请求**。

### 修复

- **标签 `height` 写 `0` 被判"必须大于 0"**：`CXXCSS.md` 两处都写着"可省：省略 = 0 = 完全由
  内容撑高"，但生成器的 `ReadSize` 对 `< 1` 一刀切报错——照文档写 `0` 的人在构建期被拒
  （外部使用方正是这么踩的）。现在标签的 `height` 收 `0`（与省略完全等价，产物里都不写
  这一行）；**宽度仍然必填且 > 0**（它是换行的依据），负数的文案改成"不能是负数"。

### 文档

- `CXXCSS.md` 新增 §2.3「层级」：`zIndex` 只在同级生效、要压住别的内容就抬容器自己。
- `InputDesign.md` §1/§10.5、`TaskGuide.md` A8b/T-4、`UsageReport.md`、
  `README.md`「已知限制」、`api/InkingWidget.md` / `InkingScene.md` / `InkingAnchor.md`
  同步改口径。
- `api/InkingRuntime.md` §七「日志：InkLog」按新实现重写：输出位置、7 段行格式、
  折叠/限流规则、查看方式（`.log` / `tail` / `grep` / `report()`）、编译期开关的实际取值
  （原先写的 `-DISLOG=ON` 选项并不存在，实际情况是 `CMakeLists.txt` 里直接打开）。

### 已知限制

- **portal 未做**：子节点的 z 再大也盖不过**父级的兄弟**。"展开的下拉要压住同一容器
  之外的内容"目前靠抬高**容器自己**（`Select` 的 `expandedZIndex` 就是这么用的）。
  引入 portal 之后，`§10.5 规则 4` 的跨簇条目仲裁才需要补回来。
- **运行期换肤只覆盖颜色，且只覆盖三处**：按钮三态、面板两态、标签文字色。
  `image` / `canvas` / 滚动条的颜色，以及 `stroke` / `shadow` 的颜色 token，仍在生成期定。
- **字号 token 是编译期的**：那是几何（重排 + 重烘命中表 + 重烘字图集），改字号要重新生成。

---

## 0.1.0-test —— 2026-10-17（**当前：测试版本**）

首个"可构建、可运行、文档齐"的测试版本。功能面来自此前的分层重构与
T/A/B/C 系列任务；本次的主要工作是**文档补齐 + 最小发布准备**。

### 新增

- **完整 API 文档**（`docs/zh/api/` 新增 10 页）：
  `InkingWidget` / `InkingScene` / `InkingWindow` / `InkingInput` /
  `InkingRender` / `InkingComponents` / `InkingPrefab` / `InkingRuntime` /
  `InkingLayoutSolver` / **`InkingCxxcss`（CXXCSS 生成物：生成类形态、`Data()` /
  `Register()` / `RegisterAllButtons()`、语言表安装、`inkgen` 命令行、落地边界）**。
  原有 6 页（锚点 / 形状 / 变换 / 布局 / 指针 / 键盘）保留。
- `docs/zh/API.md` 重写为**索引 + 五分钟上手 + 模块地图 + 头文件全量索引**。
- 仓库根 `README.md`：构建、运行、产物、依赖、已知限制。
- `include/inking.h` 新增 `kVersionStage = "test"`（版本阶段单独成一个常量）。
- 本文件。

### 变更

- `docs/zh/API.md` 的"关键词"章节保留原文并补充**设计坐标 / 设备像素**一条，
  以及"重绘脏 vs 命中脏"的区分。
- 全文档标注**测试版本**（`0.1.0-test`）。
- **运行期也标出测试版本**：`inking_demo` 与 `inking_demo_selftest` 的启动横幅改成
  `版本 0.1.0-test（测试版本）`——别人抓到一份日志时，第一行就该知道它不是正式版。

### 修复

- 无（本次不改运行期行为）。

### 已验证

- Release 构建（Ninja + MinGW UCRT64）：`libinking.dll` 链接成功，导出符号 1943 个。
- 无头自检 `inking_demo_selftest`：**69 项通过 / 0 项失败**，本次运行退出码为 0。

### 文档（第二轮补齐，同日追加）

第一轮把 API 面补到 15 页，但**被注释引用的设计文档**仍然全缺，导致 141 行注释的
「§6 第 N 条」「T-3」「§11」这类引用无法核对。第二轮补的就是这一块：

- **`docs/AGENTS.md`**：12 行 → **重建为完整文档**（§1 阅读顺序 / §2 构建自检 /
  §3 头文件与类写法 10 条 / §4 验证 6 条 / §5 两种脏对照表 / §6 经验条目 **1–112**）。
  41 个被引用条号**全部覆盖**（含 `CMakeLists.txt` 与 `tools/inkgen/` 的引用共 46 处），
  编号连续无空洞；带 `●` 的是按引用处语义回填
  （可核对），带 `†` 的是编号未被引用、按仓库实际约定补入的。
- **`docs/zh/InputDesign.md`（新增，约 470 行）**：§1 每帧只判断一次与全局 z 序 /
  §2 三档与"档不迁移" / §4 四类标脏触发源 / §5 isDirty 生命周期
  （命中脏 3 帧、重绘脏 5 帧、指针脏单列）/ §6 命中三态 / §7 动态档自判 /
  §8 生命周期成对处理 / §9 包络分档 / §10 命中表（§10.2 格子 + CSR、§10.3 聚类、
  §10.5 未落地的两条规则）/ §11 变换通道 / §12–14 键盘、滚动、捕获 / §15 开放问题。
- **`docs/zh/TaskGuide.md`（新增，517 行）**：A / B / C / D / T 五个任务系列 + `r2`
  任务纪律，每个编号给「目标 / 落点 / API / 验收 / 偏离」五段，末尾附**编号与代码
  对照表**。
- **`docs/zh/ComponentGuide.md`（新增，397 行）**：以 `InkingStaticPanel` 为样板的
  八段式组件写法、静态 / 动态选型判据、`InkingButtonLike` 混入基类、
  "为什么没抽 `TransitionTrack`"、新增组件 13 项 checklist。
- **`docs/zh/UsageReport.md`（新增，348 行）**：观测口与读数口径、成本量级、`§6.7`
  绘制成本 8 条、预制件 C1–C5 的限制、B7 文本框现状、**"不成立"表**。
- `docs/zh/SKILL.md` §0 的"文档缺口"表改为**已完成**状态（保留历史对照）。
- `docs/zh/API.md` §1 撤掉"CXXCSS.md 尚未搬入"的错误提示（该文件实际存在，821 行）。

### 已验证（第二轮追加）

- `docs/AGENTS.md` 的条号覆盖：以脚本对 `include/` + `src/`（含 `CMakeLists.txt`、
  `tools/inkgen/`）全量抽取 `§6 第 N 条`，**46 个被引用条号 0 缺失**，编号 1–112 **无空洞**。
  `§3` 被引用条号 2 / 3 / 9 / 10、`§4` 的 4 也各自有对应条目。
- **Release 全量重建**（`--clean-first`，70 个构建步骤全过，0 error）：
  `libinking.dll` 1,265,899 字节，导入库 959,264 字节，导出 **1943** 个符号。
- **自检**：`inking_demo_selftest` **69 项 `[ok]` / 1 项 `[skip]` / 失败项 0**。
  `[skip]` 是"高清文字"路径（本机 `SDL_IOFromFile` 打不开字体，退化成占位画法）。
- **发布包**：`dist/inking-0.1.0-test-win64/`（129 个文件，4.3 MB）与同名 `.zip`
  （1.3 MB）。包内 `bin/inking_demo_selftest.exe` 单独跑过一遍，`失败项：0`。
  包根 `BUILD.txt` 记录工具链、依赖版本、SHA-256、验证记录、已知限制与许可状态。

### 已知限制

1. **`TaskQueue` 退出收尾**：函数内静态量在 DLL 卸载（`DllMain` 持加载器锁）时析构并
   join 管家线程，某些环境下进程退出会看起来"挂住"。与业务逻辑无关，属线程收尾语义。
2. **`docs/zh/TaskGuide.md` / `InputDesign.md` / `UsageReport.md` 是"重建版"**：
   原始设计稿与实测数据没有随搬家带入，结论按代码行为与引用上下文复原，
   标 `†` 的小节是推断内容——引用具体数值时以自己实测为准。
3. **未验证平台**：只在 Windows + MSYS2 UCRT64 构建过；Linux / macOS 未验证。
4. **ABI 不承诺稳定**：测试版本。

---

## 0.0.x —— 2026-10-15 ~ 10-17（历史，无独立记录）

分层重构与窗口能力落地（无边框 / 全屏 / 缩放档位 / 快捷键 / 文字高清）。
详细方案见 `docs/zh/plans/2026-10-17-window-menubar-zoom-hidpi.md`。
