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
| `detail/InkLog.h` | 日志（写 `Log.html`，编译期可整体去掉） |
| `detail/MessageQueue.h` | 跨线程消息队列（生产者任意线程，消费者 UI 线程） |
| `detail/InkingGeometry.h` | 几何工具（祖先仿射、包络、裁剪） |
| `detail/InkingHit.h` | 命中工具（形状 / 矩形） |

## 一、版本与入口

```C++
// inking.h
inline constexpr const char* kProjectName = "InkingFrontendDeveloper";
inline constexpr const char* kVersion     = "0.1.0";
inline constexpr const char* kVersionStage = "test";   // 测试版本
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

```C++
class InkLog {
public:
    static void info(const std::string& moduleName, const std::string& message);
    static void pass / warn / error / debug(...);
    static std::filesystem::path logFilePath();
};

#define INK_LOG_INFO(module, msg)
#define INK_LOG_PASS(module, msg)
#define INK_LOG_WARN(module, msg)
#define INK_LOG_ERROR(module, msg)
#define INK_LOG_DEBUG(module, msg)     // 只在 INK_ISDEBUG 下存在
#define INK_DEBUG_CHECK(cond, msg)     // debug 下断言 + 记日志
```

- 输出是**可执行文件同级目录下的 `Log.html`**，按「天」和「程序启动会话」分组，写入加锁。
- **编译期开关**（CMake → 宏）：

  | 选项 | 宏 | 效果 |
  | --- | --- | --- |
  | `-DISLOG=ON` | `INK_ISLOG` | 日志参与编译，输出 `Log.html` |
  | `-DISDEBUG=ON` | `INK_ISDEBUG` | 调试级日志与调试断言生效 |

  关闭时 `INK_LOG_*` 展开成 `((void)0)`：**参数不求值、字符串不拼接**、日志代码完全不进
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

> 本文档对应版本：**0.1.0-test（测试版本）**。
