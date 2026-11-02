# 任务书 N 系列：命名空间收口（去 `inking` 外壳）

> ## 本文对应版本 0.9.x（开发中；代码 `kVersion` → `InkingVersion` 仍是 0.8.5，发布时一并改）
>
> - **系列语义**：`N` = **Namespace**。这一系列只做一件事——把库从"`namespace inking` 外壳 +
>   混合命名"改成"**全局命名空间 + 统一 `Inking` 前缀**"。
> - **动机**（用户原话的意思展开）：类名早就叫 `InkingWidget` / `InkingScene`，命名空间是
>   第二份前缀；但"去掉外壳"能安全成立的前提是**全局里每个名字都自己带前缀**——
>   而改造前的实情是：顶层 147 个类型里 126 个不带前缀、119 个自由函数几乎全不带、
>   33 个常量全是 `k*`。所以本系列的主语是**补前缀**，删命名空间只是最后一步。
> - **优先顺序是硬约束**：先补前缀（每一步都能编过、可逐步验收），后删外壳。
>   反过来做，中间态会在**别人的头文件里**报一堆撞名错误，没法定位。

---

## 1 改造目标（改造前的量化底数）

| 项 | 改造前 | 改造后 |
| --- | --- | --- |
| `namespace inking` 块 | 294 处（含 `detail` 32 / `prefab` 30 / `json` 5） | **0** |
| `inking::` 限定符 | 约 2256 处 | **0**（CMake 目标名 `inking::inking` 保留，那是另一套命名空间） |
| 顶层类型名 | 126 个不带前缀 | 全部 `Inking*`（`ICanvasPainter` 特例 → `InkingCanvasPainter`） |
| 顶层自由函数 | 119 个几乎全不带前缀 | 全部 `Inking*`（`sdl3_version` → `InkingSdl3Version`） |
| 顶层常量 | 33 个 `k*` | 全部 `Inking*`（`kVersion` → `InkingVersion`） |
| 子命名空间 | `inking::detail` / `inking::prefab` / `inking::json` | **`inking_detail` / `inking_prefab`（内嵌 `theme`）/ `inking_json`** |
| 生成代码命名空间 | `inking::cxxcss`（`--namespace` 默认值） | **`cxxcss`**（这是消费方自己组件的命名空间，不是库符号，可不带前缀） |

三条**不改**的边界（防扩散）：

1. **头文件路径不动**：`#include <button/ButtonData.h>` 照旧，虽然里面的类型现在叫
   `InkingButtonData`。文件改名是第二套破坏面（docs/打包/消费方 include 全要跟），留给
   1.0 的公开面盘点单独做。
2. **子命名空间内的符号不补前缀**：`inking_detail::fillShape`、`inking_prefab::MenuBar`
   的名字不变——命名空间本身就是它们的前缀。这一条把补前缀的工作量从 ~160 类型砍到 126。
3. **CMake 层不动**：目标 `inking` / 别名 `inking::inking` / `find_package(inking)` /
   `libinking.dll` 都是构建系统的事，与 C++ 命名空间无关。

---

## 2 批次

### N-01 补齐公开符号的 `Inking` 前缀

- 状态：**已完成**（2026-11-02）。
- 落点：`include/`（104 个头全中）、`src/`（74 个文件）、`examples/`、`tools/inkgen/`。
- 做法：脚本化改名映射（278 条，冲突检查 0），词边界正则一次替换 **5396 处 / 180 文件**。
  映射的来源是"解析头文件的命名空间栈、只收 `namespace inking` 这一层"的清单
  （`build/_ns/inventory.json` / `map.json`，构建产物，不入库）。
- 两个手工修正：
  - **`#include` 路径复原**（126 处）：词边界正则会把 `#include <image/SpriteData.h>`
    改成 `<image/InkingSpriteData.h>`，而文件名这次不动——按"去掉 `Inking` 前缀后真实
    存在"复原。注意 emit 的 include 是**字符串字面量**（`out << "#include <…>"`），
    不在行首，第一版复原脚本漏了它们。
  - **`InkingStaticVideo::AssetExists` 复原**：它是成员观测口，不是自由函数，被映射
    误扫成 `InkingAssetExists`，与全局自由函数撞名（签名不同所以只是难看，不是递归）。
- **两个同名遮蔽点**（去命名空间后 `inking::f()` 变 `f()`，落进同名静态成员）：
  `InkingButtonAppearance::InkingFromUnitRgba` 与 `InkingPanelAppearance::…` 的工厂函数，
  内部对自由函数的调用改为 `::InkingFromUnitRgba(...)`。**这一类坑编译器只抓得到签名
  不匹配的**；同名同签名的会静默无限递归，所以 N-04 里有一条全量审计。

### N-02 删除 `inking` 外壳、子命名空间改名

- 状态：**已完成**（2026-11-02）。
- 做法：行级手术（开/闭行都是整行，CRLF 文件要按 `rstrip('\r')` 比较——第一版漏了 98 处）
  + 限定符改写（`inking::detail::` → `inking_detail::` → 再把残余裸 `detail::` 补全，
  用 `(?<!inking_)` 防二次前缀）。共删 292 行外壳、改写 2352 处限定符。
- 已知残留：`include/render/InkingGlyphAtlas.h` 与 `src/asset/InkingAsset.cpp` 各有一条
  讲"命名空间嵌套"的旧注释，已随手改写。

### N-03 生成器与生成产物

- 状态：**已完成**（2026-11-02）。
- `--namespace` 默认值 `inking::cxxcss` → `cxxcss`（`Emit.h` / `CMakeLists.txt` /
  `cmake/inking-inkgen.cmake` 三处同改）；`compile_check.cpp` 的 `static_assert` 判据
  改成 `cxxcss::DynamicWalker` 等新名字，全部照常通过。

### N-04 文档同步与审计

- 状态：**已完成**（2026-11-02）。
- 活文档按同一份映射同步（约 1010 行）：`README.md`、`docs/AGENTS.md`、`docs/zh/API.md`、
  `docs/zh/api/*`、`docs/zh/UsageGuide.md`、`docs/zh/UsageReport.md`、`CXXCSS/CXXCSS.md`
  （§9 字段表重新 dump 重贴）与带 `$comment` 的样例 json。
- **历史文档不改**：`TaskGuide*.md`、`docs/zh/plans/*`、`CHANGELOG.md` 旧条目是记录，
  改了就是伪造历史；只在本文件与 CHANGELOG 新条目里写新名字。
- **顺手修掉的文档 bug**：活文档里有 30 处写 `ink::InkingStaticButton` 这类名字，而仓库
  从来没有 `ink` 这个别名（也没有 `namespace ink = inking;`）——照文档抄的代码编不过。
  现在统一为无命名空间的新写法。
- **静默递归审计**（编译器抓不到的那类）：全库扫"成员声明与新自由函数同名"的点——
  类外定义 8 处全是构造函数（无害），类内 1 处真遮蔽（video 的 `AssetExists`，已复原）。
  加上 N-01 手工修的两处工厂函数，遮蔽点清零。

---

## 3 验收判据（全部已跑）

1. `cmake --build build` **0 error**；`ctest` **7/7**（`build` 与 `build-release` 两套，
   含 `strict_warnings -Werror`、两档 `selftest`、`inkgen_compile_check`、
   `field_table_in_sync`、`silent_failure_list_in_sync`）。
2. `grep -rn "namespace inking" include src examples tools` 除 `inking_*` 外 **0 命中**；
   `grep -rn "inking::"` 只剩 CMake 目标引用（`inking::inking`，属于构建系统命名空间）。
3. 活文档 `grep "inking::\|ink::"` **0 命中**；样例 json 全部可解析。
4. `selftest` 失败项 0——像素断言 / 命中断言 / 动态档断言在改名后行为不变。

## 4 不覆盖什么

- 头文件**文件名**（见 §1 边界 1）。
- `dist/` 里已发布的 0.8.5 包：那是历史产物，不回写。
- **二进制兼容**：本次改造使全部导出符号的修饰名变化，0.8.5 及更早的 DLL 消费方
  **必须整包重编**。这在 0.x 的承诺范围内（`InkingVersion` 的注释写了"不承诺跨版本兼容"），
  但发版时 CHANGELOG 必须置顶说明。
