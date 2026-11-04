# 设计记录：CXXCSS 的位置错了——它被锁在构建期

> 状态：**已否决**（2026-11-03 同一轮裁定：P-1 不采用，不改架构；后续路线见 §8）
> 日期：2026-11-03
> 来源：Shyi 2026-11-03「要写 `cxxcss` 啊什么的，感觉用起来不如直接定义新类顺手，
> 但是要用到 json 解析器，**似乎就是这个样子**」
> 范围：`Cxxcss.cpp` 这一层的**位置**，不是它的内容；生成规则不动
> 相关：`CXXCSS.md` §1 / §6、`AGENTS.md` §6 第 3 条 / 第 82 条 / §3 第 6 条、0.6.5 T-15

---

## 0. 结论（TL;DR）

**你那句「似乎就是这个样子」多半可以推翻。**

「写 CXXCSS 不如直接定义新类顺手」和「直接定义新类就拿不到 json 配置」——
这两件事看着互为代价，其实是**同一个架构位置问题**造成的假二选一：

| 路线 | 定义配置的地方 | 外部可编辑 | 编译期类型 | 现在有吗 |
| - | - | - | - | - |
| **A** | `CXXCSS/*.json` + `inkgen` | ✅ | ✅ | 有 |
| **B** | 手写 `InkingButtonData` | ❌ | ✅ | 有（`examples/demo`） |
| **C** | C++ 写默认值 + json 覆盖 | ✅ | ✅ | **没有** |

缺的是 C。而 C 不存在的**不是技术障碍，是位置错了**：

> `tools/inkgen/Cxxcss.cpp`（246 KB，json → 运行期数据结构）只 include
> 库的公开数据头 + `inking_json`。**它已经是一份运行期代码，只是住在构建期工具里。**

`Cxxcss.h:22-28` 的 include 全表：

```cpp
#include <button/ButtonData.h>
#include <image/ImageData.h>
#include <image/InkingAtlas.h>
#include <image/SpriteData.h>
#include <panel/PanelData.h>
#include <text/InkingTextTable.h>
#include <text/LabelData.h>
```

没有 SDL、没有 `widget/`、没有 `Emit`。连 json 解析器本身都早在 0.6.5 T-15 搬进库了
（`include/util/InkingJson.h`），`tools/inkgen/Json.h` 只剩 **34 行薄壳**。

**建议（§3）**：把加载层下沉进库，让「C++ 主路径 + json 覆盖层」成立。
生成器随之从「必须的工序」降级成「可选的烘死优化」——这是架构上的一次**降级**，好事。

---

## 1. 为什么「二选一」是假的

现在要 json 配置，唯一的路是 A，而 A 的完整工序是：

```text
写 json → 配 CMake 的 ink_add_generated → 构建期生成 → include 生成的类 → 用
```

走这条路，改一个圆角要重编。想绕开重编，就只剩 B——手写 C++，而 B 没有外部配置。

于是「要外部配置」和「写着顺手」被绑成了互斥。**绑住它们的不是技术，是
「Cxxcss.cpp 住在 tools/ 里」这一件事**：运行期的库里没有「json → Data」这一层，
所以库的使用者一旦想要外部配置，就得自己抄 246 KB 的活。

这就是你说的「但是要用到 json 解析器」——**解析器有，装配置的那一层没有**。

---

## 2. 三条路线各自的真实代价

| | 谁来写配置 | 改一个数值要做什么 | 类型安全 | 适合 |
| - | - | - | - | - |
| A 现在 | 会写 json 的人 | 改 json → 重编 → 重启 | 生成后才有 | 外观定稿、数量多 |
| B 现在 | 必须会 C++ | 改 C++ → 重编 | ✅ 全程 | 少量、要逻辑 |
| C 缺的 | 先用 C++ 写默认值，json 只写差异 | 改 json → 重启（热调） | ✅ 全程 | **界面调试期** |

C 的关键是：**json 不是源，是覆盖层**。默认值在 C++ 里（顺手、可断言的），
json 只带差异（换肤 / 调试 / mod）。发布时把 json 剔掉，一个字节都不读。

这与 `AGENTS.md` §6 第 3 条并不冲突——那条说的是「构造时不查表」，
而 C 的覆盖是**显式的、构造之后的**动作。不调它，行为与现在完全一致。

---

## 3. 方案

### P-1 把加载层下沉进库（推荐）

`Cxxcss.cpp` 一分为二：

| 现在（`tools/inkgen/Cxxcss.cpp`，约 5200 行） | 搬到 `src/cxxcss/` | 留在 `tools/inkgen/` |
| - | - | - |
| `LoadConfigInto` / 各 Loader / 字段白名单 / 校验 | ✅ | |
| `ApplyExtends`（`$extends` 浅合并） | ✅ | |
| `TryComponentKindFromPath`（目录名 = 类型） | ✅ | |
| SVG 子集离散（`:844-1076`，`ParseSvgSubset` / `SvgToShape`） | | ✅ 生成期专属 |
| `ReadImageBytes`（把图烘成字节数组） | | ✅ 生成期专属 |
| 主题 token 替换成字面量 | | ✅ 生成期专属（运行期换肤走 `InkingTheme`） |
| `Emit` 那一侧的一切 | | ✅ |

公开面（名字待定）：

```cpp
namespace inking_cxxcss {
    struct LoadResult { /* 与 inkgen 那份同形 */ };
    LoadResult LoadButton(const std::string& path);      // 或 LoadButtonFrom(text, 源名)
    LoadResult LoadPanel(const std::string& path);
    LoadResult LoadLabel(const std::string& path);
    LoadResult LoadImage(const std::string& path);
    LoadResult LoadTextTable(const std::string& path);
}
```

于是 C 成了：

```cpp
// 主路径：C++ 里定义，顺手、类型安全
struct MenuButton : InkingStaticButton {
    explicit MenuButton(InkingWidget* parent)
        : InkingStaticButton(parent, Data()) {}
    static InkingButtonData Data();
};

// 可选：运行期覆盖（调试 / 换肤 / mod），不重编
auto r = inking_cxxcss::LoadButton(assetRoot / "Button/optionItem.json");
if (r.ok) { menuButton.ApplyData(r.button); }   // 或构造时传合并后的 Data
```

**收益不是「多一条路」，是三条路共用同一份解析与校验**——
`AGENTS.md` §6 第 94 条那条「同一件事写两遍就会分家」在这里正好适用：
现在「json 认什么字段」只有构建期知道，运行期想知道得抄一遍。

### P-2 只在库侧补一个覆盖入口（局部，不动加载层）

`InkingButtonData` 加一个 `ApplyOverrides(const InkingJsonValue&)`，只认**少量**
可覆盖字段（颜色 / 圆角 / 偏移 / 时长）。json 全文件读、只挑这几项。

- 好处：改动小，不进 246 KB
- 坏处：**两套字段白名单**，违反上面第 94 条；迟早和 `Fields.h` 分家
- 结论：只有当 P-1 被否时才考虑

### P-3 维持现状

承认「要外部配置就得走生成器」，把这条写进文档，不再假装有三条路。
代价是下游每一个想要热调的人都得自己抄解析。

---

## 4. 代价与风险（P-1）

| 项 | 评估 |
| - | - |
| DDR/DLL 体积 | `Cxxcss.cpp` 246 KB **源码**，编进 DLL 大概一二百 KB 量级；可选编译（`INK_HAS_CXXCSS_RUNTIME`）就能让不需要的人零成本 |
| 编译时间 | 一个 5000 行的翻译单元。现在它也已经编进 `inkgen_lib` 了，只是换了个目标 |
| 分拣风险 | SVG 离散 / 图片字节 / 主题替换三块要剥出来，**剥错的表现是"库开始依赖文件系统语义"**。这三块的边界必须先写清单再动手 |
| 路径语义 | `LoadJsonFile` 是**相对 CWD**（工具的输入），库里的 `ParseAsset` 是**相对资源根**。下沉时必须明确选一个，不能两个都留（`AGENTS.md` §6 第 30 条：不做静默兜底） |
| 契约 | 加载失败要返回 `LoadResult` + 计数，不抛、不静默退默认值——照 `InkingStaticButton::UnknownNameCount()` 那个形状（`src/button/InkingStaticButton.cpp:50-66`） |

---

## 5. 这一改动**不会**动到什么

- 生成器仍然能生成类是字面量的代码（路径 A 不变，一个字节都不变）
- 所有现有自检、`inkgen --validate`、字段总表、`CXXCSS.md` 全部照旧
- 运行期性能：不调覆盖入口 = 零成本；调了 = 启动期一次解析
- 公开数据结构（`InkingButtonData` 等）一个字段都不改

---

## 6. 明确不做

| 不做的 | 理由 |
| - | - |
| 让运行期解析承担 SVG 离散 / 图片烘字节 | 那是「生成期把外部资源变成二进制」的事，运行期做等于每次启动都重算 |
| 在 JSON 里写回调 / 表达式 | 编排留在 C++。这条是 `CXXCSS.md` §7 的结论，不动 |
| 一套「通用配置格式」（yaml/toml/自定义） | `InkingJson` 已经在了，换格式只增加第二种真相 |

---

## 7. 待裁定

**Q1** 方向对不对？「C++ 主路径 + json 覆盖层」是不是你要的那种顺手？

**Q2** P-1 的范围：第一刀只下沉 **Button**（最小闭环，验完再铺 Panel / Label / Image），
还是五类一起下沉（一次到位，但分拣的工作量一次压上来）？

**Q3** 下沉后的加载层放哪、叫什么？
候选：`src/cxxcss/`（按子系统，`AGENTS.md` §3 第 10 条）＋ `inking_cxxcss` 命名空间；
或者挂在现有的 `util/` 下（不行，`util/` 定义是"纯标准库、任何子系统都能用"，
而它要认识 `InkingButtonData`）。

---

## 8. 裁定结果（2026-11-03，同一轮）

**P-1 / P-2 / P-3 全部不采用。不改架构，`Cxxcss.cpp` 留在 `tools/inkgen/`。**

用户裁定的后续路线：

> 之后用 **UI 编辑器**包装一层，相当于热更新；确定之后直接用代码生成器生成对应的类，
> 这样代码就能摆脱 `cxxcss::` 了。

拆开看是三件事，其中两件**现在的工具面已经支持**：

| 想要 | 现状 |
| - | - |
| 编辑期热更新 | 由 UI 编辑器自己提供（它内部渲染，不跑真程序）。与库无关 |
| 生成物换命名空间 | **已支持**：`inkgen --namespace <ns>` |
| 生成物落进源码树 | **已支持**：`inkgen --out-dir <目录>`。要改的只是「产物只落 `build/`」那条纪律（`AGENTS.md` §3 第 6 条） |

所以这条路**不需要动生成器一行代码**，要动的是约定：

1. **§3 第 6 条要开一个口子**：新增一个明确的生成物目录（例如 `src/generated/`），
   写清「由 `inkgen` 产出、不手改」，而不是把产物混进 `src/` 的其余部分。
   否则那条「源码树保持干净，才敢用通配符扫描」的前提会失效。
2. **生成物提交进仓库** ⇒ 改配置 = 重新导出 + 提交，代码评审里会出现字面量 diff。
   这是「摆脱生成器依赖」的必然代价，接受即可，但要写下来。
3. 生成器从此是**一次性脚手架**（导出期用一次），不再是构建期工序。
   `ink_add_generated()` 仍然是给「不想提交生成物」的人留的另一条路，两条并存。

**这条路线的影响面比 P-1 小得多，且不动任何运行期代码。** 记在这里，
将来接编辑器时按这三条走。

---

## 附录 A：上一轮的使用侧归因（仍然有效，但不是本轮主题）
上一版文档从「拿生成类写界面」这个角度归了五条，方向偏了半截，但其中两条是真问题，
留在这里不丢：

| 编号 | 问题 | 证据 |
| - | - | - |
| U-1 | **父子关系藏在成员声明顺序里，反了是 UB，不报不崩不诊断** | `src/widget/InkingWidget.cpp:78-82` 构造时 `GetParent()->_children.push_back(this)`；父成员声明在后 = 往未构造的 vector 里 push |
| U-2 | **实例数量在编译期焊死，而界面行数是运行期的** | 生成类推荐写法是成员（`CXXCSS.md:1317`）；运行期那条路要 `AddChild(unique_ptr)`，`parent` 传两次 |

这两条与 P-1 正交：P-1 解决「配置写在哪」，U-1/U-2 解决「实例怎么摆」。
真要动，U-1 该按 `AGENTS.md` §7 静默失效处理（补一条计数 + 进清单）。

---

> 本文只做归因与取舍，不含任何已落地的改动。方向裁定后再按
> `task-driven-project-build` 拆任务书。
