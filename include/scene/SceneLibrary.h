#pragma once

// 场景库：登记与查找场景的静态类，对应后端 InkingBackendFramework 的
// basic/ShineStatusChecker（静态类 + moduleName → 指针）。
//
// 结构对应关系：
//   ShineStatusChecker   → inking::SceneLibrary          （静态注册表）
//   ShineBasicModule     → inking::InkingScene           （被登记的基类）
//   StatusRegisterToken  → inking::SceneRegisterToken    （RAII 令牌）
//
// 与后端不同的地方，都是本项目自己的规矩：
//
//   1. **同名同期只能有一个**。名字被占了，后来者注册**当场驳回**——对象还是
//      构造出来了，但它永远不进库、永远不会被播放。想重新用这个名字，得先把
//      占用者关掉（CloseScene）。这条规矩让"按名字找场景"永远只有一个答案，
//      窗口层和生成器都不用再处理"同名多个怎么办"。
//
//   2. **场景有活跃态**。同一时刻最多一个场景是"正在渲染"的（SetActiveScene），
//      切场景时旧的自动进入静默。静默不等于销毁：数据还在、能读，
//      只是不渲染、跟着渲染的那组逻辑也不动。
//
//   3. **存储用函数内静态量，不用类的静态数据成员**。类的静态数据成员在 main
//      之前按翻译单元顺序初始化，而静态场景对象可能先构造，于是出现"场景在库
//      已经析构之后才注销"的静态初始化 / 析构顺序问题。函数内静态量首次使用时
//      才初始化，谁先登记就把库先拉起来，库一定活得比登记者久。
//
//   4. **不加锁，只允许 UI 线程登记与注销**。后端有锁是因为模块会被线程池里的
//      多个管家线程读写；本项目的场景层只在 UI 线程构造 / 析构（窗口主循环那
//      一条线程），加锁既挡不住误用，又白付一次原子操作。
//
// 详见 docs/API.md「SceneLibrary」章节。

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

// 只做前向声明：公开头里不拖 SDL 进来。
struct SDL_Renderer;

namespace inking {

class InkingScene;

/**
 * @brief 场景库：登记与查找场景、调度预加载、并（只对显式接管的场景）负责销毁。
 *
 * 三件事分得很清楚，别混：
 *   - **登记**：名字 → 场景，一对一。注册是"抢占式"的，谁先注册谁占住这个名字；
 *   - **活跃态**：同一时刻最多一个场景在渲染（`SetActiveScene`）；
 *   - **所有权**：默认**不拥有**任何场景（谁 new 的谁管，而实际上栈上 / 成员
 *     变量最常见）。只有 `AdoptScene` 显式接管过的，`DestroyScene` 才敢销毁。
 *
 * 预加载（`PreloadScene`）调度也归它：异步那条路需要一个**统一的受理入口**，
 * 否则"谁受理过"就会出现两个答案。
 */
class SceneLibrary {
public:
    /** 注册的结果。失败原因分得清，才能给出有用的日志。 */
    enum class RegisterResult {
        Ok,         /**< 注册成功 */
        NameTaken,  /**< 同名场景已在库中——驳回，后来者永不播放 */
        Invalid,    /**< 名字为空或指针为空 */
    };

    /**
     * 登记一个场景。**同名已存在时直接驳回**，不覆盖、不排队。
     *
     * @return 注册结果；只有 Ok 表示真的进库了。
     */
    static RegisterResult RegisterScene(const std::string& sceneName,
                                        InkingScene* scene);

    /**
     * 注销一个场景。
     *
     * 只在该名字当前正指向这个场景时才真的摘掉（指针不匹配就什么都不做），
     * 所以"库里已经换人了"这种情况不会误删别人的登记。
     */
    static void UnregisterScene(const std::string& sceneName, InkingScene* scene);

    /** 按名字找场景；没注册过返回 nullptr。 */
    static InkingScene* FindScene(const std::string& sceneName);

    /**
     * 关掉一个场景：从库里摘掉，并把它标记为已关闭。
     *
     * 场景对象本身不归库所有，**不会被销毁**——调用方自己管生命周期。
     * 关掉之后这个名字空出来，可以再建一个新的同名场景。
     * 这也是"避免野指针"的正路：库先放手，调用方再决定什么时候析构。
     *
     * **拒绝当前活跃的场景**：关掉它在表现上是"窗口静默黑屏"——没有场景可渲染、
     * `Tick*` 也没人收，而且不报错。库里没有"下一个是谁"的概念（没有场景栈、
     * 没有切换历史），所以这里不替你挑替代者：先 `SetActiveScene(nullptr)`
     * 或切到别的场景，再回来关。
     *
     * **也拒绝预加载在飞的场景**：收尾那条路只发给**在库**的场景，先摘了登记
     * 那次收尾就永远等不到了（规则和 `DestroyScene` 一致：在飞时既不关也不销毁）。
     *
     * @return 真的关掉了一个才返回 true；上面两种会被拒绝（返回 false + 日志）。
     */
    static bool CloseScene(const std::string& sceneName);

    // ---------------- 预加载 ----------------

    /**
     * 按名字要求一次**异步**预加载：任务丢进 `TaskQueue`，场景的 `onPreload()`
     * 在**管家线程**上跑，跑完由主循环帧首回到 UI 线程收尾。
     *
     * 这就是"把 init 从构造函数里拆出来、而且不卡调用方"的那条路：在
     * `Show()` 之前调一次，启动时就不用等加载了。
     *
     * ⚠️ **场景必须活到预加载落地为止**（任务里捕的是场景的裸指针）。被库
     * 接管的场景由 `DestroyScene` 把关（它拒绝销毁在飞的场景）；**栈上 / 成员
     * 变量的场景**就得调用方自己保证——别在 `IsPreloadFinished()` 还是假的时候
     * 让它出作用域。
     *
     * @return 找到了场景、且这次真的受理了才返回 true。重复调用会被拒
     *         （返回 false），所以不用自己判重；场景不存在 / 已关闭也是 false。
     */
    static bool PreloadScene(const std::string& sceneName);

    /**
     * 预加载收尾：把"已经有结果、还没通知过"的场景挑出来，在**调用它的线程**
     * 上调它们的 `onPreloadFinished(ok)`。
     *
     * **主循环帧首调一次**（UI 线程）。它只做一次遍历，没有结果要收尾的场景
     * 一个字节都不动。
     */
    static void DrainPreloadCompletions();

    // ---------------- 所有权与销毁 ----------------

    /**
     * 把场景的**所有权**交给库：只有被接管的场景才会被 `DestroyScene` 销毁。
     *
     * 为什么"销毁"必须显式 opt-in：库里其余场景都是**非拥有**的（谁 new 的谁管，
     * 而实际上栈上 / 成员变量最常见），库对它们不能 delete——对栈对象 delete
     * 是 UB，而且库也看不见场景的**子组件**是值成员还是堆对象。所以库只敢销毁
     * 自己接管的。
     *
     * 场景在这之前已经构造完了（构造即登记），所以它一定已经进过库。
     * @return 指针非空、且没被接管过才返回 true（幂等）。
     */
    static bool AdoptScene(std::unique_ptr<InkingScene> scene);

    /**
     * 销毁一个**被库接管**的场景。
     *
     * 拒绝四种情况，都返回 false（前三种带日志）：
     *   - 找不到（名字错 / 已关闭 / 停放态的场景压根不在库里）；
     *   - 没被接管过（`AdoptScene`）——库对它没有所有权，硬删会踩到看不见的东西；
     *   - **正在活跃**——先 `SetActiveScene(nullptr)` 或切走；
     *   - **预加载在飞**——管家线程正拿着这个对象跑 `onPreload()`。
     *
     * 它**不当场 delete**：标 closed、摘登记、把所有权挪进待销毁队列，真正的
     * 析构交给 `CommitPendingDestructions()` 在**帧末**做（理由见那里）。
     */
    static bool DestroyScene(const std::string& sceneName);

    /**
     * 执行待销毁队列：**主循环帧末调一次**。
     *
     * 为什么必须推迟到帧末：销毁一个场景会连带拆掉它的整棵子树，而
     * `Render` / `TickLogic` / `TickFrame` / 指针派发都在遍历那份绘制列表。
     * 在遍历中途把容器拆了就是悬垂。攒到帧末再删——"这一刻没有人在看它"。
     *
     * @return 真的析构了几个。
     */
    static std::size_t CommitPendingDestructions();

    /** 把某个场景设为当前活跃（正在渲染）的那个；传 nullptr 表示谁都不活跃。 */
    static void SetActiveScene(InkingScene* scene);

    /** 当前活跃（正在渲染）的场景；没有时返回 nullptr。 */
    static InkingScene* GetActiveScene();

    /**
     * 渲染当前活跃场景。**窗口层每帧调这个**，不要直接去碰场景的 `Render`。
     *
     * `InkingScene::Render` 是 protected，只有框架该调；从库这里走一道，
     * "只有活跃场景会被渲染、每个场景一帧只渲染一次"这条规矩就有了唯一落点。
     * 将来上合批（DrawCall 合批 + 静态烘焙）时，提交层也正好落在这里。
     *
     * @return 真的画了才返回 true（没有活跃场景时为 false）。
     */
    static bool RenderScene(SDL_Renderer* renderer);

    /** 取当前登记过的所有场景名，主要用于自检与枚举。 */
    static std::vector<std::string> GetAllSceneNames();

    /** 当前库里有多少个场景（一个名字算一个）。 */
    static std::size_t SceneCount();

    /** 某个名字是否已经被占。 */
    static bool IsNameTaken(const std::string& sceneName);

private:
    SceneLibrary() = delete;
    ~SceneLibrary() = delete;
};

}  // namespace inking
