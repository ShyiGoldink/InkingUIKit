# 运行支撑

**回到API目录：**
[API index](../API.md)

这一页是"不是 UI、但 UI 离不开"的那一层：

| 头文件 | 内容 |
| --- | --- |
| `inking.h` | 公共入口头：版本、日志、消息队列、任务队列 |
| `event/InkingEvent.h` | 通用事件槽（令牌订阅 / 派发时快照） |
| `dataStruct/TaskStruct.h` | `Task<T>`：依赖 + 链式后继 |
| `thread/ThreadPool.h` | 受限线程池（注入式：谓词 / 执行 / 等待提示） |
| `thread/TaskQueue.h` | 单例任务队列（含延迟任务） |
| `timeline/Timeline.h` | **整个 UI 的唯一时间源**（逻辑步 / 每帧 / 自定义频率） |
| `asset/InkingAsset.h` | 资源根 + 轻量混淆 + 加载 |
| `detail/InkLog.h` | 日志（纯文本 `.log` + 按需 HTML 报告，编译期可整体去掉） |
| `detail/MessageQueue.h` | 跨线程消息队列（生产者任意线程，消费者 UI 线程） |
| `detail/InkingGeometry.h` | 几何工具（祖先仿射、包络、裁剪） |
| `detail/InkingHit.h` | 命中工具（形状 / 矩形） |

## 一、版本与入口

```C++
// inking.h
inline constexpr const char* kProjectName = "InkingFrontendDeveloper";
inline constexpr const char* kVersion     = "0.5.3";
inline constexpr const char* kVersionStage = "test";   // 0.x 发布版
std::string sdl3_version();                            // 例如 "3.4.16"
```

`inking.h` **不含组件**：组件头各自带自己的依赖，全塞进来会让"只是想用日志"的翻译单元
被迫拖上一整套锚点树。

## 二、InkingEvent：令牌订阅 + 派发时快照

```C++
template <typename... Args>
class InkingEvent {
public:
    using Handler = std::function<void(Args...)>;
    using Token = std::uint64_t;          // 0 = 无效

    Token Subscribe(Handler handler);
    bool Unsubscribe(Token token) noexcept;
    bool HasSubscribers() const noexcept;
    std::size_t SubscriberCount() const noexcept;
    void Clear() noexcept;
    void Emit(Args... args);
};
```

**`Emit` 派发的是快照**：它先把当前全部 handler 拷一份再依次调，所以**回调里退订
（甚至订阅）是安全的**——这是这类槽最容易踩的迭代器失效。

**谁挂的回调谁负责摘**：注册者（例如 `MenuBar` 订阅窗口的 `fullscreenChanged`）必须在
析构时 `Unsubscribe`，并把令牌存成成员（0 = 没订）。表比注册者活得久，挂着已死的 `this`
就是悬垂。

用法：

```C++
InkingEvent<const std::string&, int> selectionChanged;
Token tok = selectionChanged.Subscribe([] (const std::string& id, int index) { /* ... */ });
selectionChanged.Unsubscribe(tok);
```

## 三、Task 与 TaskQueue

>**Task&lt;T&gt;**

```C++
template <typename T>
struct Task {
    using Action = std::function<T(const std::vector<T>&)>;

    Task& dependOn(Action fn);              // 加一个"先跑的"依赖
    Task& dependOn(std::vector<Action> fns);
    Task& then(Action fn);                  // 加一个后继（链式）
    Task& then(std::vector<Action> fns);

    T execute(const T& input);              // 依赖先跑，结果按序装进 params
};
```

`execute` 的语义：有依赖就逐个 `dep->execute(input)` 收集返回值，把整组结果作为
`params` 交给本任务；然后沿 `next` 链继续跑。

>**TaskQueue**

```C++
class TaskQueue {
public:
    static TaskQueue& instance();                              // 必须走 .cpp 定义

    void addTask(Task<std::any> task);                         // 线程安全
    void addTask(Task<std::any> task, std::chrono::milliseconds delay);

    bool isEmpty() const;
    bool hasDueTask() const;
    std::chrono::milliseconds timeUntilNextDue() const;
    void executeTask();                                        // 线程池的执行函数
};
```

- **单例的 `instance()` 定义在 `src/thread/TaskQueue.cpp`**：头内 inline 函数的函数内静态量
  在每个翻译单元各有一份，动态库下 exe 与 DLL 会**各起一个队列、各起一组管家线程**
  ——两个队列互不可见，任务落进哪一个取决于调用点（而且不报错）。搬进 `.cpp` 之后只有一个。
- 队列用 `std::multimap<time_point, task>`：按到期时刻排序，同时刻按插入顺序（立即任务之间
  仍是先进先出）。
- **回调跑在管家线程上**：碰 UI 状态的逻辑不能放进去。时间可以在别处算，**交付必须在
  UI 线程**（`SceneLibrary::DrainPreloadCompletions` 就是那条回程）。

> ⚠ 已知缺口：`TaskQueue` 的函数内静态量会在 **DLL 卸载（`DllMain` 持加载器锁）** 时析构
> 并 join 管家线程，某些环境下会让进程退出看起来"挂住"。与业务逻辑无关，
> 属于线程收尾语义，尚未修（见 `README.md` §已知限制）。

## 四、ThreadPool

**注入式**：线程池本身不认识任务是什么，认识任务的是 `TaskQueue`。

```C++
inline constexpr std::size_t kDefaultThreadCount = 4;

class ThreadPool {
public:
    ThreadPool() = default;
    ~ThreadPool();                      // 析构里 join

    void init(std::function<bool()> predicate,                            // 有活吗
              std::function<void()> execute,                              // 有活干什么
              std::function<std::chrono::milliseconds()> waitHint = {},   // 最多睡多久
              std::size_t threadCount = kDefaultThreadCount);

    void quit();       // 请求退出并唤醒全部（真正 join 在析构里）
    void wake();       // 唤醒一个（一条任务只需一个执行者）
    void wakeAll();
    bool isRunning() const noexcept;
};
```

`waitHint` 是**延迟任务的唯一实现手段**：到点时没有人会来 `notify`，只能靠超时醒来。
所以它返回的必须是"距离最近一个任务到期还有多久"（`TaskQueue::timeUntilNextDue`）。

`init` **重复调用会被忽略**：避免覆盖已经在跑的线程对象导致无人 join。

## 五、Timeline：唯一时间源

三重频率互不整除（逻辑 50Hz / 动画 24Hz / 显示器 60~144Hz），所以**时间不能按"多少次
tick"来数，只能按真实流逝时间累加**——错拍只影响"这一帧算几次"，不影响时间本身。

```C++
using Handle = std::uint64_t;
using SampleCallback = std::function<void(double stepSeconds)>;
using LogicCallback  = std::function<void(double fixedStepSeconds)>;
using FrameCallback  = std::function<void(double deltaSeconds)>;

explicit Timeline(double logicIntervalSeconds = 1.0 / 50.0,
                  double maxCatchUpSeconds = 0.25);

void Advance(double deltaSeconds);                         // 每帧调一次

Handle Subscribe(double intervalSeconds, SampleCallback);  // 自定义频率
bool Unsubscribe(Handle handle);
std::size_t SubscriberCount() const noexcept;

void SetLogicCallback(LogicCallback);
double GetLogicInterval() const noexcept;
void SetFrameCallback(FrameCallback);

std::uint64_t GetLogicStepCount() const noexcept;   // 累计派发过几次逻辑步
int  GetLastLogicSteps() const noexcept;            // 上一帧派发了几次（0 / 1 / 更多）
double GetLastDelta() const noexcept;
double GetLogicAccumulator() const noexcept;
std::uint64_t GetCatchUpDropCount() const noexcept; // 丢掉过几次"追不上"
```

三条通道：

1. **逻辑步**（固定 interval）：累加器追赶，一帧可能跑 0 次、1 次或多次。
   回调收到的是**固定的 interval**，不是真实 delta——物理和状态机要的就是确定的步长。
2. **每帧**：每个渲染帧恰好一次，回调收到**真实 delta**。插值、和画面同步的放这里。
3. **自定义频率**：自己声明"我多久要一次"，同样走累加器追赶，回调收到的是**实际步长**
   （= 声明的间隔），所以慢帧也不会让动画走偏。

**追上限制**：机器休眠 / 切后台回来 delta 可能是几秒，不设上限就会一次补几百帧逻辑。
超过 `maxCatchUpSeconds` 的部分**直接丢掉**（丢的是"欠的帧"，不是时间基准——
累加器清零而不是减少）。

**为什么驱动源是主循环而不是任务队列**：任务队列的到点靠 `wait_for` 撑，Windows 上定时器
粒度 1~15.6ms，50Hz（20ms）的抖动占比太大；更关键的是**回调线程**——任务队列在管家线程上
执行，而 UI 状态只能在 UI 线程动。时间可以在别处算，**交付必须在 UI 线程**。

**单线程使用**：所有方法只在 UI 线程调，所以不加锁。

## 六、资源：Asset

```C++
struct AssetConfig {
    std::string root;   // 空 = exe 目录下的 assets/
    std::string key;    // 空 = 不解密
};

void AssetConfigure(const AssetConfig& config);   // 进程级，幂等，线程安全
AssetConfig AssetConfigSnapshot();
void AssetResetConfig();

std::string AssetRoot();                          // 绝对路径（不保证目录存在）
std::string AssetResolve(const std::string& relative);
bool AssetExists(const std::string& relative);
std::vector<std::uint8_t> AssetLoad(const std::string& relative);
std::string AssetLoadText(const std::string& relative);
bool AssetIsObfuscated(const std::uint8_t* data, std::size_t size) noexcept;

struct AssetStats { /* hits / misses / bytes */ };
AssetStats AssetStatsSnapshot();
void AssetResetStats();
```

解决的核心问题只有一个：**别把所有图片都烘进 exe**。配置里写**相对路径**，文件运行期读进来
——换图不用重编，exe 也不随图片数量膨胀。

**`key` 是"轻量混淆"，不是加密**：可逆的异或流（+ 一个文件头），挡的是"把资源包的字节
直接拖出来用 / 一眼看出是什么格式"，**挡不住有意逆向的人**（密钥在 exe 里，谁都读得到）。
要真安全得走平台加密 / 服务端下发，不在本模块范围内。

`root` 为空时走 `getModuleFileNameW` 那条**宽字符**路——中文目录里窄串构造 `path` 会抛。

## 七、日志：InkLog

与 InkingBackendFramework 的 `ShineLog` 是**同一套设计取向，不是逐字同构**：**纯文本是唯一
事实源，HTML 是按需派生产物**这一条两边一样；但前端没有搬 `init/shutdown/snapshot/command`
那几样，级别集合也不同（前端少 `Trace`/`Fatal`、多 `Pass`），另外 `0.1.1` 起还有两处前端特有
（DEBUG 独立折叠槽、`CountOf`）。完整对照在 `include/detail/InkLog.h` 头顶那段注释里——
**要判断"改动要不要同步另一个仓库"，看那段**。

```C++
class InkLog {
public:
    static void info / pass / warn / error / debug(module, msg, site = current());
    static void mark(text, site = current());       // 伪级别 MARK，不受阈值影响
    static std::filesystem::path logFilePath();     // LOG/<日期>/run-<时分秒>-<pid>.log
    static std::filesystem::path logDirectory();    // LOG/
    static void setLevel(LogLevel);  static LogLevel level();
    static bool parseLevel(std::string_view, LogLevel&);
    static std::vector<std::string> tail(std::size_t count);
    static std::vector<std::string> grep(pattern, module);
    static std::string statistics();
    static std::uint64_t CountOf(LogLevel);         // 按级别数"真的发生了几次"
    static void ResetStats();                       // 只给自检用：清计数与折叠槽
    static std::filesystem::path report();          // .log -> 自包含 HTML 报告
};

#define INK_LOG_INFO / PASS / WARN / ERROR(module, msg)
#define INK_LOG_DEBUG(module, msg)     // 只在 INK_ISDEBUG 下存在
#define INK_LOG_MARK(text)             // 复现起点锚点
#define INK_DEBUG_CHECK(cond, msg)     // debug 下断言 + 记日志
```

**输出位置**

- `LOG/<YYYY-MM-DD>/run-<HHMMSS>-<pid>.log`，可执行文件同级；`LOG/latest.txt` 指向最近一次运行。
- 一次运行一个文件；跨零点继续写同一个文件，只插一行日期标记。

**每行 7 段，自带定位**

```text
20:11:51.978 | WARN  | Scene:-- | #000003 | T-01 | InkingScene.cpp:144 | 场景未登记
   时间         级别    模块:阶段   序号     线程    文件:行              消息
```

第 3 段固定写成 `模块:--`：前端没有阶段编号，保留这一列是为了和框架端共用同一套解析与报告工具。
第 6 段由宏把 `std::source_location::current()` 传下来，所以它指向**业务代码**，不是 `InkLog.cpp` 这个转发点。

**降噪**

| 机制 | 行为 |
| --- | --- |
| 折叠（DEBUG 独立槽） | 同一个键再来一条就**不落第二行**，结算时补一行 `FOLD`，带次数与起止时间。别的级别的记录插进来**不打断**它——真实噪声正是"每帧一条 DEBUG、中间夹着别的日志"，共用槽对这种交错无效 |
| 折叠（其它级别） | 只有**连续**相同才合并：DEBUG / INFO / PASS 走这条，WARN 及以上不走 |
| 限流 | 同一个键每秒最多 5 条，其余只计数（`statistics()` 与报告页脚可见）；`ERROR` 不限流 |
| 失败 | `WARN` / `ERROR` **不折叠**，一条一行——它们是诊断凭据，合并会丢掉"发生了几次" |
| 阈值 | 运行时 `INK_LOG_LEVEL` 或 `setLevel()`；`MARK` 永远落盘 |

`InkLog::CountOf(Level)` 数的是**真的调用了几次**（被折叠、限流、阈值挡掉都不算），
`statistics()` 数的是**落盘了几行**。要断言"这里应当恰好出现一条 WARN"，用前者。

**查看**

- 直接读 `.log`（`grep` / `tail` / 编辑器）；或 `tail()` / `grep()` / `statistics()` 读内存环形缓冲（最近 2000 行）。
- 需要浏览器视图：`InkLog::report()` 生成 `report-<run>.html`，自包含单文件、双击即开。
- 运行期**不写 HTML**，热路径上没有 CSS/JS 模板。

**编译期开关**（CMake → 宏）：

| 选项 | 宏 | 效果 |
| --- | --- | --- |
| 恒开 | `INK_ISLOG` | 日志参与编译，输出 `.log` |
| `INKING_DEBUG_LOG`（**默认 `OFF`**） | `INK_ISDEBUG` | 调试级日志与调试断言生效 |

两个宏都是 `target_compile_definitions(inking PUBLIC ...)`——**必须是 PUBLIC**，因为公开头里
`kDefaultLogLevel` / `kDebugEnabled` 是 `inline constexpr`，库与消费者取值不同就是 ODR 违例。
但 `INK_ISDEBUG` 由选项控制、**默认关**：`0.1.1` 之前它被无条件写成 `PUBLIC INK_ISDEBUG=1`，
于是发布包的消费方**关不掉**调试流（实测 948 条日志里有一条重复 528 次）。
关闭 `INK_ISLOG` 时 `INK_LOG_*` 展开成 `((void)0)`：**参数不求值、字符串不拼接**、日志代码完全不进
二进制。"关掉日志"是真的把代码去掉，不是运行时判断一下再跳过。

> 库本身**不往 stderr 打调试输出**：`InkingBatch.cpp` 里那些 `fprintf` 已经清掉了。

## 八、消息队列

```C++
enum class MessageType { Normal = 0, Error = 1, Pass = 2, Warn = 3 };

struct Message {
    MessageType type = MessageType::Normal;
    float delayTime = 0.0f;   // 延迟多少秒之后才输出；0 = 立即
    std::string message;
};

class MessageQueue {
public:
    static void addMessage(MessageType type, float delaySeconds, const std::string& text);
    static void quickMessage(bool success, float delaySeconds, const std::string& text);
    static std::vector<Message> drainMessages();
    static void waitForMessage(std::chrono::milliseconds timeout);
    static std::size_t pendingCount();
};
```

并发模型（唯一一个**加锁**的成员）：**生产者任意线程，消费者 UI 线程**。
`waitForMessage` 让 UI 线程在没有消息时阻塞（或做超时），有新消息时 `condition_variable`
唤醒。

`delayTime > 0` 时消息会变成 `TaskQueue` 里的一个延迟任务，到点后按"立即"重新入队
——延迟本身不在这里等。

> 编译期开关 `ISMESSAGE`（宏 `INK_ISMESSAGE`）关闭时整个类退化成空实现，
> 其实现 TU 变成空翻译单元。

## 九、几何与命中的内部工具

`detail/InkingGeometry.h`（不含 SDL）：

```C++
Affine AncestorAffine(const InkingWidget& node) noexcept;        // 祖先的复合仿射
Affine SubtreeChainAffine(const InkingWidget& node) noexcept;    // 含自己的子树链
Affine ClipChainAffine(const InkingWidget& node) noexcept;       // 裁剪链
bool   AncestorClipInDevice(const InkingWidget& node, ShapeBounds& out) noexcept;
ShapeBounds SubtreeHitEnvelope(const InkingWidget& node) noexcept;
ShapeBounds IntersectBounds(const ShapeBounds& a, const ShapeBounds& b);
ShapeBounds TransformBounds(const ShapeBounds& local, const TransformSpec&);
```

它们住在 `detail/` 而不是 `InkingWidget` 里，是为了**依赖单向**：几何层包含 Widget 头，
Widget 头不包含几何层，不会循环包含。

`detail/InkingHit.h`：

```C++
bool HitTestShape(const InkingWidget& node, const ShapeSpec& shape,
                  const TransformSpec& transform, float worldX, float worldY);

bool HitTestLocalRect(const InkingWidget& node, const ShapeBounds& bounds,
                      const TransformSpec& transform, float worldX, float worldY);
```

两者走同一条三步：**撤祖先变换 → 减绝对位置 → 过自身变换的逆**；只有最后一步不同
（形状 vs 矩形）。边界算**在内**（与 SDF 的 `f <= 0` 一致）。

> 渲染侧的矩形会按包含式取整成整数像素，所以"边界那一像素"两边可能差一格——
> 自检别贴着边界写断言。

## 十、主题：运行期换颜色（`theme/InkingTheme.h`）

颜色在生成期被换成字面量（**烘焙主题**），token 名同时留在组件数据里；运行期按
名字重查，所以能换肤。**只覆盖颜色**——字号 token 改的是几何，仍在生成期替换。

```c++
bool InstallThemeTable(ThemeTable table);       // 装一张（同名替换；不切换）
bool SetActiveTheme(const std::string& name);   // 切（没装过 → false，版本不动）
const std::string& ActiveThemeName() noexcept;
std::uint64_t ThemeVersion() noexcept;          // 装 / 切 / 清空都 +1
std::uint32_t ResolveThemeColor(const char* token, std::uint32_t fallback);
ThemeStats ThemeStatsSnapshot();                // hits / misses / version
void ClearThemeTables() noexcept;               // 回到"全用字面量"
```

>**ThemeTable**

```c++
struct ThemeColorEntry { std::string token; std::uint32_t color; };
struct ThemeTable { std::string name; std::vector<ThemeColorEntry> colors; };
```

三个要点：

1. **不装表 = 零成本**。`ResolveThemeColor` 先判空指针（这个颜色位不是语义色），
   再判"一张表都没装"，两条都在进表之前返回 `fallback`（生成期烘的字面量）。
2. **版本号是"切换时机"**（和语言表同一套）。组件把版本放进缓存：`ButtonLook` /
   `PanelLook` 在 `Advance` 里比一次 `ThemeVersion()`，标签在 `EffectiveTextColor`
   里比一次。**不要"遍历场景通知所有组件"**——漏一个就是"有的组件还是旧配色"。
3. **查不到不报错、不空白**：用兜底色，并记一次 miss（`ThemeStatsSnapshot` 看得见）。

生成物里的入口是 `InstallGeneratedThemes()`（启动期已自动调过一次），
`kDefaultThemeName` 是烘焙主题名。切主题只需要：

```c++
inking::SetActiveTheme("light");
```

`CXXCSS.md` §3.9 讲配置侧（多份主题、`"default": true`、跨表 token 覆盖校验）。

## 常见坑

1. **在 `TaskQueue` 回调里碰 UI 状态**：那是管家线程。交付回 UI 只有"帧首消费"那条路。
2. **用 `Timeline` 之外的东西当节拍器**（例如 `sleep` 或 `TaskQueue` 的延迟任务）：
   定时器粒度与线程归属都不对。
3. **`Subclass` 里存了 `InkingEvent::Token` 却不退订**：注册者先死，表后死 ⇒ 悬垂。
4. **把 `AssetConfigure(...).key` 当加密**：那是可逆混淆，挡不住逆向。
5. **指望 `INK_LOG_*` 在关了 `ISLOG` 时还有副作用**：宏展开成 `((void)0)`，参数根本不求值。
6. **多线程调 `SceneLibrary` / `Timeline` / 渲染**：它们明确"只在 UI 线程"，
   不加锁不是遗漏，是前提。

---

> 本文档对应版本：**0.5.3（0.x 发布版）**。
