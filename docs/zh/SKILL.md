# 编码与解耦规范

> **⚠ 本文对应的版本是 `0.1.0-test`（测试版本）。**
> 规范本身在测试期内有效，但**具体条款可能随接口调整而改号**
> （本仓库注释按「§N 第 M 条」引用，改号会让历史注释失准——改之前先 grep 一遍）。

适用范围：`InkingFrontendDeveloper` 与 `InkingBackendFramework` 的全部 C++ 代码。
来源：对后端框架（除 `net` 模块）实际写法的提炼，**优先照抄现成的正确写法**。

规范性用语：**必须**（违反就是 bug 或必然的返工）、**应该**（默认这么做，有理由可以例外）、**可以**（可选）。

## 0. 文档现状（测试版本）

这一节存在的理由是：本仓库的注释里大量按「`docs/AGENTS.md` §6 第 N 条」「`TaskGuide.md` T-x」
这样的形式引用文档。**第一轮搬迁时缺了五份，第二轮已按代码与引用上下文重建**
（2026-10-17）。读注释时遇到对不上的引用，先在这里对一眼，不要把"文档缺失"误判成
"代码有问题"。

| 被引用的文档 | 状态 | 说明 |
| --- | --- | --- |
| `docs/AGENTS.md` | **已重建**（§1–§6，§6 共 112 条） | 41 个被引用条号全部覆盖；`●` = 按引用处语义回填，`†` = 编号未被引用、按仓库约定补入 |
| `InputDesign.md` | **已重建**（§1–§16） | 命中三态、标脏生命周期、变换通道、开放问题（多指针 / 局部层级） |
| `TaskGuide.md` | **已重建**（A / B / C / D / T 系列 + r2） | 每个编号五段：目标 / 落点 / API / 验收 / 偏离 |
| `CXXCSS/CXXCSS.md` | **已存在**（821 行） | 目录与命名、公共骨架、形状层词汇表、Button 字段全表 |
| `UsageReport.md` | **已重建**（§6.7 等） | 成本与量级、"不成立"表、预制件限制、B7 文本框现状 |
| `ComponentGuide.md` | **已重建** | 以 `InkingStaticPanel` 为样板的组件写法 + 新增组件 checklist |

> **重建 ≠ 原文**：`InputDesign.md` / `TaskGuide.md` / `UsageReport.md` 的原始设计稿与
> 实测数据没有随搬家带入。结论按**代码行为 + 引用上下文**复原；标 `†` 的小节是推断内容，
> 引用具体数值时以自己实测为准。

**当前文档全貌**：`docs/zh/API.md`（索引）→ `docs/zh/api/*.md`（16 个专页，含
`InkingCxxcss.md` 讲配置生成出来的那些类）、
`docs/zh/SKILL.md`（本文件，规范）、`docs/AGENTS.md`（经验与条号）、
三份设计文档、`docs/zh/CHANGELOG.md`、`docs/zh/plans/`（已落地方案）。
**遇到注释与文档冲突，以头文件注释为准**（它是与代码一起改的）。


## 1. 分层与依赖方向

**必须**保持单向依赖：**上层可以依赖下层，下层绝不能反过来认识上层**（避免循环引用）。

## 2. 头文件纪律

细节见 `AGENTS.md` §3 第 2 条（公开头只放声明 / 前向声明优先 / `union SDL_Event;`）
与 §6 第 14 条（include 相对 `include/` 根，多写一段就找不到）。
前端特有补充：

- 只做前向声明的类型**不该**在头文件里调它的方法（那要求完整类型），实现搬到 `.cpp`；
  只有一行的方法（例如重写 `ModuleName`）可留在头文件里。
- **新文件统一用 `#pragma once`**，不再新增 `#ifndef` 宏 guard；改旧文件时顺手统一。

## 3. 类的写法

### 3.1 数据与逻辑分开

**必须**把纯数据抽成 `struct`（`dataStruct/`，组件放 `include/<组件>/<组件>Data.h`）。
判据：**这个类型有没有需要维护的不变量？** 有 → 类；没有 → `struct`。

### 3.2 单例

需要单例时（前端 `TaskQueue`）**必须**用**函数内静态量**，不用类的静态数据成员
（静态初始化 / 析构顺序问题：会出现"场景活了库还没起来"甚至"对象在库已析构之后才注销"，
`AGENTS.md` §6 第 12 条）。拷贝 / 移动四个全禁：

```cpp
static TaskQueue &instance() { static TaskQueue inst; return inst; }   // ← 函数内静态量
TaskQueue(const TaskQueue &) = delete;        TaskQueue &operator=(const TaskQueue &) = delete;
TaskQueue(TaskQueue &&) = delete;             TaskQueue &operator=(TaskQueue &&) = delete;
```

### 3.3 静态工具类

纯功能型（前端 `MessageQueue`）用**静态类**：全 `static` 方法 + 私有构造 / 析构
（私有构造 = 禁止实例化）。有状态、有生命周期的按 §3.2 走。

### 3.4 接口 + 实现分离

有"多种实现"预期的抽象**必须**抽成纯虚接口，头部只依赖接口；**虚析构必须有**：

```cpp
class IDatabase {                       // 纯接口，不知道 MySQL 存在
public:
    virtual QueryResult connect(...) = 0;
    virtual QueryResult execute(const std::string &sql) = 0;
    virtual ~IDatabase() = default;     // ← 虚析构必须有
};
```

前端的对应：`SceneLibrary` 是接口层，具体场景由调用方派生——**框架层不该认识任何具体业务类型**。

### 3.5 CMake 里的可选依赖

可选依赖**必须**做成编译期开关 + **能读懂的报错**：说清"怎么办"（推荐版本、
关掉开关的命令、路径覆盖文件怎么写），而不是只丢一句"找不到 XXX"：

```cmake
message(FATAL_ERROR
    "MySQL client library not found.\n"
    "If you do not need MySQL, configure with:\n"
    "  cmake -S . -B build -DINKING_ENABLE_MYSQL=OFF")
```

前端的 `cmake/SDL3.cmake` 已经照这个思路写了。

## 4. 解耦的四种常用手法

按优先级从高到低，**能用位置靠前的就别用靠后的**。

### 4.1 反向注册（谁想知道谁去查）

**不要**让底层认识上层：上层把自己登记到一张表，底层只认表、不认类型——底层不必为后来者改动。
前端已移植成 `SceneLibrary` + `RegisterToken`：

```cpp
_sceneRegisterToken.emplace(_sceneName, this);      // 上层登记自己
std::optional<SceneToken> _sceneRegisterToken;      // 延迟构造（InkingScene.h）
```

**关键点**（前端 `RegisterToken` 已经踩过并修正）：

- **令牌必须记住"我到底进没进库"**（`_active`）：注册可能被驳回，驳回的令牌是空转的，
  析构时**绝不能**去注销——那会把正主从库里摘掉（`AGENTS.md` §6 第 15 / 22 条）。
- **用 `std::optional` 延迟构造**，不在初始化列表里。
- **令牌的成员声明顺序要在被登记的信息之后**，析构时 key 才全程有效。

### 4.2 依赖注入（传指针，不 new）

**应该**把协作者以指针 / 引用传进来，而不是自己 `new`、也不直接调单例。
**必须**在命名和注释里写清**拥有关系**：`InkingAnchor::_parent`（定位链）与
`InkingWidget::_scene`（所属场景）都标了"非拥有"——**标了之后才知道该不该 delete，
以及对象比协作者活得久时会不会变成野指针**。

### 4.3 门面（把 N 个子部件收成一个入口）

**应该**用一个瘦门面把多个协作部件收成单一入口，外部只认识门面——内部怎么拆、怎么重构，
调用方都不用动，这正是解耦的实际收益。

### 4.4 静态分发点（把"依赖"换成"登记"）

需要"某个模块提供的能力让别处用"时，**不要**让使用方 include 提供方：能力包成数据
（含 `std::function`），按类别拆成多个编译单元，汇总文件只负责把几类拼起来——
**新增一类不用动它以外的任何文件**。

## 5. 命名

细节（类 `PascalCase` / 方法 `camelCase` / 成员 `_camelCase` / 常量 `kPascalCase` /
`enum class` / **不要** `m_` 前缀）见 `AGENTS.md` §3 第 3 条。

**前端补充约定**（必须遵守）：写入口 `PascalCase`（`SetVisible` / `Resize`）且 `[final]`
（不可重写、内部统一标脏）；钩子 `on` 开头（`onTick` / `onDirty`）且 `[√]`（可重写、不标脏）——
**名字上就该能一眼分开**，因为两种契约完全不同。

## 6. 注释与文档

**应该**用 `/** @brief ... */` 描述类，`/** 一句话 */` 描述成员，行尾对齐：
**变量**的注释写在其**前方**，**方法**的注释写在其**后方**：

```cpp
    /**这是一个xx变量*/
    int xx;

    void function();/**this is a kind of function*/
```

## 7. 错误处理

**必须**给"可能失败的边界"一个可表达的返回值，而不是靠抛异常穿过层边界
（后端 `JsonTool`：`std::optional<Json> loadFromFile(...)` / `bool saveToFile(...)`）。
**应该**沿用这个判据：**能纯本地判断的就别做 I/O**，把代价不同的两件事拆成两个接口
（`ping()` 探活 vs `isBroken()` 只看本地状态）。

**必须**在长驻线程（管家线程、渲染循环）的入口兜住异常：异常一路逃逸出去，
这条线程会被 `std::terminate` 干掉，线程池静默少一个工人，之后处理能力永久下降。

## 8. 并发

**必须**明确"哪些状态归哪条线程"，并在注释和文档里写死：

| 子系统 | 线程归属 |
| --- | --- |
| 场景 / 渲染 / 输入 / 时间线 | **只在 UI 线程**（主循环那一条），因此**不加锁** |
| 任务队列 / 线程池 | 管家线程，跨线程 |
| 消息队列 | 生产者任意线程，消费者 UI 线程，**加锁** |

**必须**：跨线程的状态才加锁，并在头文件里说明"为什么加锁 / 为什么不加锁"。
前端 `SceneLibrary` 写了"有意不加锁，因为场景只在 UI 线程构造与析构"——
它比锁本身更有价值，因为标出了前提；前提变了就该加锁。

**必须**记住：**任务队列的回调跑在管家线程上**，碰 UI 状态的逻辑不能放进去；
时间可以在别处算，**交付必须在 UI 线程**。

## 9. 提交前的自检

见 `AGENTS.md` §4：构建与 ctest 全过；**能变成数值判据的自己验**（像素 / 坐标 / 数量 /
顺序 / 计数，渲染类走离屏读回）；**需要"看 / 摸"的停下来交给用户**；改了渲染路径先存
逐字节基线再比对；同步与实现不一致的文档；别提交 `build/`。

## 10. 文件夹和命名规则

每个组件**应该**占有**单一文件夹**（`include/<组件>/` + `src/<组件>/`），只放自己的逻辑。
**静态组件以 `InkingStatic` 开头**（`InkingStaticButton`）、**动态组件以 `InkingDynamic` 开头**
（`InkingDynamicButton`），通常两样都要。通用工具（如 Json 解析）以**名词**结尾：`JsonTool`。
细节见 `AGENTS.md` §3 第 10 条与 `docs/ComponentGuide.md`。
