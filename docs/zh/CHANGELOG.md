# 变更记录（CHANGELOG）

格式：每个版本一节，分 **新增 / 变更 / 修复 / 文档 / 已知限制** 五类。
测试版本期间的条目**不承诺**跨版本兼容——升级前请逐条对照。

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
