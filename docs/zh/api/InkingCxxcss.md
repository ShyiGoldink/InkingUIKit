# CXXCSS 生成物（配置驱动的组件类）

**回到API目录：**
[API index](../API.md)

**必读的前置**：配置怎么写（目录、字段、形状表、`$token`、各组件段的完整字段）
见 [`CXXCSS/CXXCSS.md`](../../../CXXCSS/CXXCSS.md)（826 行，**权威**）。本页只回答另一个问题：
**生成出来的东西长什么样、怎么用、边界在哪。**

一页说清核心：

> **CXXCSS 不是"运行期查表"，是"每个名字生成一个独立的 C++ 类"。**
> 颜色 / 尺寸 / 形状 / 锚点全部烘成字面量，构造时不查任何表。
> 所以拼错名字是**编译错误**，而不是运行期 `Find()` 返回 `nullptr`。

## 一、一句话定位：生成类只是"把 Data() 递上去"

```cpp
namespace inking::cxxcss {
class OptionItem : public inking::InkingStaticButton {   // ← 基类由 json 的 "dynamic" 决定
public:
    static constexpr const char* kName = "optionItem";
    explicit OptionItem(inking::InkingWidget* parent)
        : inking::InkingStaticButton(parent, Data()) {}
    static const inking::ButtonData& Data();     // 全字面量，烘在构建产物里
    static void Register();                      // 仅按钮有
};
}
```

三条推论，读的时候记住：

1. **行为一点都不在生成类里**：它是薄壳，真正的实现全在
   `InkingStaticButton` / `InkingStaticPanel` / `InkingStaticLabel` / `InkingStaticImage`。
   所以"这个类能干什么"要查 [InkingComponents.md](InkingComponents.md)，不在本页。
2. **`Data()` 是函数内静态量**：首次使用时才初始化，避开静态初始化 / 析构顺序问题；
   内容全是常量，多线程读安全。
3. **两条路等价**：生成类 vs 手写 `ButtonData` + `InkingStaticButton(parent, data)`
   最终是同一个构造。配置驱动只是省掉手写字面量、并拿到名字的编译期保险。

## 二、产物：两个文件

`inkgen` 固定产出两个文件，名字由头文件名推出来：

| 文件 | 内容 |
| --- | --- |
| `<header>`（默认 `inkgen/button_service.h`） | 常量与各类声明（`inline constexpr` 放这里，业务代码可直接 include） |
| `<header 主体>_register.cpp` | 各自的 `Data()` 定义 + 启动期就位的静态对象 |

实际位置（本项目）：`build/inkgen/all/button_service.h` 与
`button_service_register.cpp`。**产物只落 build 目录**，源码树保持干净。

头注释第一行就写了"本文件由 inkgen 生成，不要手改"，并且会记下**来源清单**与**主题**
（`$token` 已换成具体值）。

## 三、五类组件生成什么

| 目录 | 生成物 | 基类 | `Register()` |
| --- | --- | --- | --- |
| `Button/` | 一个按钮类 | `InkingStaticButton`（默认）/ `InkingDynamicButton`（`"dynamic": true`） | **有** |
| `Panel/` | 一个面板类 | `InkingStaticPanel` | 无 |
| `Label/` | 一个标签类 | `InkingStaticLabel` | 无 |
| `Image/` | 一个图片类 | `InkingStaticImage` | 无 |
| `Text/` | **语言表静态数据 + 安装函数**（不是类） | — | — |
| `Theme/` | **什么都不生成**（它是生成器的输入） | — | — |

**只有按钮有 `Register()`**：`ButtonLibrary` 是"运行期才知道名字"那条路的落点
（自检、工具、将来换皮肤）。面板 / 标签没有按名字查的库——它们只有"数据构造"一条路，
所以它们的 `name` **必须**是合法标识符（生成期会拦下不合法的）。

> `Text` 是**语言表**、不是"文字组件"（文字组件叫 `Label`）；
> `Theme` 是**生成器的输入**、不是组件——组件配置里写 `"$token"`，主题在**生成期**
> 就被换成具体颜色，所以**换主题要重新生成**，运行期改不了。

本项目当前的生成结果（`CXXCSS/` 16 个 json，去掉 3 个 `.example.json`）：

| 类 | 来自 | 类型 |
| --- | --- | --- |
| `OptionItem` | `Button/optionItem.json` | 静态按钮 |
| `ClipWindow` / `ColumnPanel` / `DrawerPanel` / `LabelBoard` / `ScrollPanel` | `Panel/*.json` | 静态面板 |
| `EllipsisNote` / `HintBody` / `LangBadge` / `MenuTitle` | `Label/*.json` | 静态标签 |
| `CardArt` | `Image/cardArt.json` | 静态图片 |
| （语言表） | `Text/ui.json` | 2 门语言 / 4 条占位符 |
| （不生成） | `Theme/dark.json` | 主题输入 |

## 四、命名：文件名 = `name` 字段 → PascalCase 类名

```text
CXXCSS/Button/optionItem.json   →   inking::cxxcss::OptionItem   （kName = "optionItem"）
```

1. **目录名 = 组件类型**（大小写不敏感、单复数都认）。认不出的目录名**报错**，不会猜。
2. **文件名 = `name` 字段**，两者不一致时**报错**（类名与查找键都取自这个名字）。
3. **`.example.json` 结尾只作参考，不参与生成**。
4. **`name` 必须是合法的 C++ 标识符**：`normalButton` 行，`button.example` 不行。
   名字里有 `.` 或 `-` 时**只是没有类**——数据照样进 `RegisterAllButtons()`，
   只能按字符串名字用。
5. **同名跨类型也是错**：一个按钮和一个面板同名，生成期就报"已经被 xxx 用了"
   （名字是运行期的全局查找键）。
6. 生成类的命名空间由 **构建脚本指定**（`--namespace`），本项目用 `inking::cxxcss`。

## 五、全局入口（生成头里那几个符号）

```cpp
// —— 语言表 ——
inline constexpr std::size_t kTextLanguageCount = 2;
inline constexpr std::size_t kTextEntryCount    = 4;
const inking::TextTable& GeneratedTextTable();     // 那张表的本体（进程内静态量）
void InstallGeneratedTextTable();                  // 整表替换；启动期已自动调过一次

// —— 计数（自检 / 工具用）——
inline constexpr std::size_t kButtonCount = 1;
inline constexpr std::size_t kPanelCount  = 5;
inline constexpr std::size_t kLabelCount  = 4;

// —— 把本次生成的全部按钮配置登记进 ButtonLibrary ——
void RegisterAllButtons();                         // 幂等：先撤下上一批再登记
```

- `InstallGeneratedTextTable()` 由生成的 `.cpp` 里那个**启动期静态对象**自动调一次；
  手动再调就是"重新装一遍"。
- `RegisterAllButtons()` **不是按钮能用的前提**——每个按钮类自己带着数据。
  它服务的是"按名字查配置"的用法。幂等，重复调用不会叠加。

## 六、生成期校验（这些错在构建期就红）

| 情形 | 行为 |
| --- | --- |
| 未知字段（拼错的键） | **报错**（拼错的字段比缺字段更难查） |
| 文件名 ≠ `name` | 报错 |
| 同名（含跨组件类型） | 生成期报"已经被 xxx 用了" |
| `name` 不是合法标识符 | 面板 / 标签 / 图片生成期拦下；按钮退化成"只有数据、没有类" |
| 形状参数不合法 | 见 `CXXCSS.md` §3.6（一部分断言、一部分夹取） |
| 认不出的目录名 | 报错，不猜 |

`inkgen --validate <json>...` 只校验不写文件；`INK_BUILD_TESTS=ON` 时会把 `CXXCSS/`
里的全部配置过一遍（"配置写错了"于是构建与 ctest 两条路上都红）。
另外还有一条**编译自检**：把全部配置真实生成一份并编进一个最小 exe
（`inkgen_compile_check`）——它证明"生成出来的 .h/.cpp 能编过"，那与"配置读得懂"是两件事。

## 七、inkgen 命令行

```text
inkgen --out-dir <目录> [--namespace <ns>] [--header <名>] <file.json>...
inkgen --validate <file.json>...

  --out-dir <目录>   产物目录（约定指向 build/，不存在会创建）
  --namespace <ns>   生成代码的命名空间，默认 inking::cxxcss
  --header <名>      生成的头文件名，默认 button_service.h
  --validate         只校验，不写文件
  -h, --help         显示帮助
```

CMake 侧不用手写这些参数：`ink_add_generated(<目标> OUT_DIR ... SOURCES ...)`
（见根 `CMakeLists.txt`）负责"配置变了就重新生成 → 编进 OBJECT 库 → 挂到目标上"，
调用方只需要 `#include <inkgen/button_service.h>`。

**生成器只依赖标准库**（连 SDL 都不碰），所以它能在任何环境下先编出来，
再拿它去生成组件代码。

## 八、边界（做到哪一步了）

| 项 | 状态 |
| --- | --- |
| 每个名字一个独立类 | ✅ 已实现 |
| 形状展开成 SDF + `contains` + AABB | ✅ 已实现（三份同源） |
| 锚点与尺寸变成编译期常量 | ✅ 已实现（运行期不再解析配置） |
| 语言表 / 主题表 | ✅ 已实现（`$token` 生成期替换） |
| **按布局类型生成专用查找**（O(1) 命中，不走烘焙表） | ❌ **未做**（查询仍走烘焙表） |
| "专属命中结构"这类特化 | ❌ 未做 |
| **场景生成** | ❌ **有意不做**——场景是"编排"不是数据，手写就够（`CXXCSS.md` §7） |
| `AppearanceType::Image` / `Svg` 的生成链 | ⚠ 数据面与纹理路已接，生成器那条链仍在落地 |

## 九、什么时候**不要**用生成器

- 组件的尺寸 / 颜色要**运行期算**（例如按内容自适应、按数据上色）：手写 `ButtonData`
  传进 `InkingStaticButton(parent, data)` 更直接。
- 一次性小工具 / 自检：手写数据，不必为了三个按钮建一套 json。
- 场景：永远手写（见上）。

## 常见坑

1. **改了 json 没生效**：产物在 `build/`，看 `build/inkgen/all/button_service.h` 的头部
   来源清单确认这次真的重新生成了。
2. **换主题只改 `Theme/*.json` 就以为运行期会变**：`$token` 是**生成期**替换，必须重新生成。
3. **给面板 / 标签起名 `my.panel`**：那不是合法标识符 ⇒ 没有类可用（按钮还有数据兜底，
   面板直接什么都不剩）。
4. **以为生成类里能改行为**：它是薄壳，改行为要改基类或手写组件——**别改生成文件**。
5. **一个按钮和一个面板同名**：跨类型也算冲突，生成期就红。

---

> 本文档对应版本：**0.1.0-test（测试版本）**。配置字段的权威说明在
> [`CXXCSS/CXXCSS.md`](../../../CXXCSS/CXXCSS.md)。
