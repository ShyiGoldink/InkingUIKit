//InkingScene是场景的基类
//类似于QWidget，用户创建场景需要通过继承的方式来具体实例化内部的场景组件
//由于InkingScene是静态的，所以InkingScene本身的大小不能自己改变，只能跟随Window的大小改变
//想使用能随意改变大小的组件请参考InkingDynamicWidget

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <scene/HitTable.h>
#include <scene/SceneLibrary.h>
#include <scene/SceneRegisterToken.h>
#include <widget/InkingStaticWidget.h>

#include <input/KeyboardInput.h>

// 只做前向声明：公开头里不拖 SDL 进来（见 docs/AGENTS.md §6 第 8 条）。
struct SDL_Renderer;

namespace inking {

/**
 * @brief 场景：window 之下最高的结点，继承**静态锚点**。
 *
 * 场景永远静态：没有位置与尺寸写入口，只负责"渲染 / 不渲染"，
 * 会动的东西压在 widget 层（见 docs/API.md「InkingScene」）。
 *
 * ---------------------------------------------------------------------------
 * 登记：同名同期只能有一个
 *
 * 构造时把自己登记进 SceneLibrary。**名字被占了就当场驳回**——对象照样构造
 * 出来了，但它没进库，所以既不会渲染也不会被 tick，是一块"停放"的荒地；
 * 想用这个名字，得先把占用者 `SceneLibrary::CloseScene(名字)` 关掉。
 *
 * 被驳回的场景 `IsRegistered()` 为 false。库不拥有场景对象，关掉也不析构它，
 * 生命周期始终归调用方——这样"从库里拿指针 → 关掉 → 再自己决定何时析构"
 * 就不会踩野指针。
 *
 * ---------------------------------------------------------------------------
 * 三组逻辑
 *
 * 场景和组件的回调分成两组，由场景按当前状态分别开关；再算上渲染本身，
 * 就是"三组"：
 *
 *   | 回调                | 谁驱动 / 什么时候跑                          |
 *   | ------------------- | -------------------------------------------- |
 *   | onRenderBoundTick   | 每个**渲染帧**一次，且只在**活跃**时跑       |
 *   | onTick              | 时间线的**固定逻辑步**（默认 50Hz），一直跑  |
 *
 * 于是三种状态就是这两组的组合：
 *
 *   - **活跃**：渲染 + 两组逻辑都跑；
 *   - **静默**（被别的场景顶掉）：不渲染，跟着渲染的那组停；
 *     固定逻辑步照跑——后台的事情不该因为切场景就停摆。数据能读，
 *     所以可以先把数据拿完再决定去留；
 *   - **停放**：注册被驳回的场景，什么都没开始，永远不会跑。
 *
 * "需要不渲染但逻辑动"的场景，用 onTick 就行——它不依赖活跃态；
 * 反过来"不渲染就彻底别动"，把逻辑放 onRenderBoundTick。
 * 更细的频率（例如 24fps 的动画）不走这两个钩子，去订阅 `inking::Timeline`
 * （include/timeline/Timeline.h），那里按需采样。
 *
 * ---------------------------------------------------------------------------
 * 渲染树（骨架 + 扁平绘制列表）
 *
 * 层级（`_parent`）是**唯一真相源**，场景持有的绘制列表只是它的派生索引：
 * 子节点在构造时沿父链找到自己所属的场景并登记进来，场景这边只是一份
 * 按 z 序排好的扁平数组。
 *
 * 为什么扁平而不是递归遍历：
 *   - 绝对坐标必须走父链，而 `GetAbsX()` 是 O(深度)。扁平列表里父指针是
 *     现成的，取绝对坐标不用额外结构；
 *   - 命中侧（docs/InputDesign.md §10.2）已经定了"压平成 HitTable + 平铺
 *     _drawList"，渲染跟着扁平，两边遍历才不会越走越远；
 *   - 线性遍历对缓存友好，也没有深递归的栈风险。
 * 「整块在动」那档（抽屉 / Toast）将来要的是"子树自带子表 + 变换通道"，
 * 那时再加子表，不走这条默认路径。
 *
 * 顺序的维护方式是**标脏 + 帧末惰性重建**：
 *   - 骨架变化（构造 / 析构 / SetParent / ChangeZIndex / SetVisible / 动态几何写入）
 *     只把场景标脏，不立刻排序。一帧里连改十个 zindex 也就重建一次；
 *   - 重建推迟到 `Render()` 开头做，重建完就是最新的；
 *   - 提交顺序是 **树序**（父先于子）+ 兄弟间 z 从小到大（先画底下的），
 *     正好是 `InkingWidget::IsAbove` 答案的反序——同一个比较器，两边规则
 *     必须一致，否则会出现"画的是 A、点到的是 B"。
 * ---------------------------------------------------------------------------
 */
class InkingScene : public InkingStaticWidget {
public:
    /**
     * 场景的 RAII 登记令牌：绑定"场景类 → SceneLibrary"。
     * 类型别名让派生类不用关心模板参数。
     */
    using SceneToken = inking::RegisterToken<InkingScene, SceneLibrary>;

    /**
     * 绘制列表里的一项：节点本身 + 提交时算绝对坐标用的父指针。
     *
     * `absX` / `absY` 是静态节点的绝对左上角，**建表时算一次就冻住**
     * （静态组件的几何构造即定型）。动态节点存在这里的是建表那一刻的
     * 快照，每帧提交时会重新算——所以别拿它当动态节点的当前坐标用。
     *
     * 把这一项暴露出去、让窗口层自己遍历提交，是**临时的**：
     * 等合批（DrawCall 合批 + 静态烘焙）落地，提交要收进框架内部统一做，
     * 那时这个类型和 `BuildDrawList()` 都会收回去。
     */
    struct DrawItem {
        InkingWidget* node = nullptr;
        InkingWidget* parent = nullptr;
        float absX = 0.0f;
        float absY = 0.0f;
    };

    /** 运行时构造：不绑定场景名，不登记进场景库。 */
    InkingScene();

    /**
     * 配置驱动：传入 sceneName 作为场景名，并登记进场景库。
     *
     * 推荐用 k 常量命名，编译期就能排查问题（见 docs/API.md）。
     * 场景的尺寸与锚点在构造时定下，之后不挪动。
     * 名字被占用时注册被驳回，场景进入停放态（见类注释）。
     */
    explicit InkingScene(const std::string& sceneName);

    /**
     * InkingScene的另一种构造方式
     * 如果不想要用CXXCSS进行初始化，可以选择直接传入场景必须要的参数进行初始化
     * 一般用在小场景的情况下
     * 但是考虑到一般使用场景，Scene一般都需要和window的大小对齐，否则可能会有奇怪的黑边
     * 但是依然要提供这样的一个方法
     */
    explicit InkingScene(const std::string& sceneName, const AnchorData& anchorData);

    virtual ~InkingScene();

    // 禁止拷贝构造和赋值操作：场景持有父指针、还要登记进场景库，
    // 复制出的第二份关系会让父子链和场景库同时指向同一个对象。
    InkingScene(const InkingScene&) = delete;
    InkingScene& operator=(const InkingScene&) = delete;
    InkingScene(InkingScene&&) = delete;
    InkingScene& operator=(InkingScene&&) = delete;

    /** 场景名；构造时定下，没有写入口。 */
    const std::string& GetSceneName() const noexcept;

    /** 这个场景当前是否登记在场景库里（令牌还在自己手上且没被驳回）。 */
    bool IsRegistered() const noexcept;

    /** 注册被驳回了吗（后来者、名字被占）。停放态就是它。 */
    bool IsRejected() const noexcept;

    /** 被库关掉过吗（SceneLibrary::CloseScene）。关掉之后不能再注册。 */
    bool IsClosed() const noexcept;

    /**
     * 这个场景是不是"正在渲染"的那个。
     *
     * 由 SceneLibrary::SetActiveScene 维护：同一时刻最多一个为 true。
     * 切场景时旧的自动变 false（静默）。
     */
    bool IsActive() const noexcept;

    // ---------------- 每帧回调（由时间线 / 框架驱动） ----------------

    /**
     * **固定逻辑步**：由时间线按固定步长派发（默认 50Hz）。
     *
     * 内部做两件事：
     *   1. `onTick(fixedStep)`——场景自己的逻辑；
     *   2. 遍历绘制列表里的**动态组件**，逐个 `Tick(fixedStep)`——
     *      它们才是"几何会自己变"的那一档。静态组件的 `Tick` 压根不存在，
     *      所以这里不用问类型（登记时分流已经在类型上了）。
     *
     * 步长是**固定的**，不是真实 delta：状态机要的就是确定的步。
     * 被驳回 / 已关闭的场景不跑。
     */
    void TickLogic(double fixedStepSeconds);

    /**
     * **每帧一次**：由时间线在每个渲染帧派发，传真实 delta。
     *
     * 内部只在**活跃**（正在渲染）时调 `onRenderBoundTick(delta)`——
     * 插值、和画面同步的东西放这里。静默 / 停放 / 关闭都不跑。
     */
    void TickFrame(double deltaSeconds);

    // ---------------- 绘制列表（框架内部与自检用） ----------------

    /**
     * 把绘制顺序重建到最新，返回只读的扁平列表。
     *
     * 名字用 Get 是为了说明"它不会改你的东西"：没脏时直接返回现成的，
     * 脏了才重建一次。
     */
    const std::vector<DrawItem>& BuildDrawList() noexcept;

    /** 当前绘制列表里有多少项（自检与预算估算用）。 */
    std::size_t GetDrawItemCount() noexcept;

    /** 标脏：骨架变了，绘制顺序需要重建。写入口内部已经调过。 */
    void NotifyStructureChanged() noexcept;

    /**
     * @brief **显式重建收集表**（A5）：`_drawList` + 命中表，立刻按当前树重建一份。
     *
     * A2 / A3 这一类"树变了但收集表没跟上"的问题，从 0.3.0 起由
     * `sortDrawList()` 自己按**树**重建（树是唯一真相源），所以正常路径
     * 不需要手工调它。留着它有三个正当用途：
     *
     *   1. **自检**：想在"下一帧之前"就断言"这棵子树进表了没有"；
     *   2. **宿主诊断**：怀疑收集表与树不一致时，重建一次再看数量；
     *   3. **兜底**：有人在树外改了父子关系（没走 `SetParent`）之后强制对齐。
     *
     * @return 重建后列表里有多少项。
     */
    std::size_t RebuildRenderList();

    /**
     * @brief **强制重建命中表**（D6），不必等帧。
     *
     * `QueryHit` 本来就会先推一次重建，所以"窗口还没 Show 就想验证某个位置
     * 点得到谁"在 0.3.0 之后直接可用；这个入口是给"只要表、不查点"的场合
     * （自检统计条目数、宿主预热）。
     *
     * @return 表里有多少条静态条目。
     */
    std::size_t RebuildHitTable();

    /**
     * @brief 按名字找节点（B2 的配套）：深度优先，先序，找第一个同名的。
     *
     * 名字不唯一是允许的（同名多半是配置复制粘贴），所以这里只承诺
     * "找第一个"；要全部拿到请自己走 `GetChildren()`。
     *
     * @return 找到的节点；没有时返回 nullptr。**不含场景自己**。
     */
    InkingWidget* FindByName(const std::string& name) noexcept;

    /**
     * @brief 按名字**激活**一个节点（D7）：`FindByName` + `InkingWidget::Activate()`。
     *
     * 自检 / 快捷键 / 脚本用它代替 `dynamic_cast<InkingStaticButton*>`
     * 再调 `TriggerClick()`——不绑具体控件类型，换控件也不用改调用方。
     *
     * @return 找到并真的触发了才返回 true。
     */
    bool Activate(const std::string& name) noexcept;

    // ---------------- 标脏的统一消费（T-3） ----------------
    //
    // 三种脏的三个消费点，各管一段，别混：
    //
    //   1. **命中脏**（几何 / 可见性 / 层级 / 父子）→ 帧首。`sortDrawList()` 在
    //      渲染之前把绘制列表重建好（将来命中表落地后，重烘表也在这里）。
    //   2. **指针脏**             → 帧末（下面这个函数清）。指针一动"谁被指着"就可能变，
    //      但**表不用重烘**——这是它必须和命中脏分开的原因，见 `SetPointerState`。
    //   3. **重绘脏**（颜色 / 文字 / 变换）→ 帧末（下面这个函数记账 + 计时清零）。
    //
    // 为什么是"帧末统一消费"而不是"谁改谁立刻处理"：一帧里可能连着 Resize 三次、
    // ChangeZIndex 两次，立刻处理就是重烘五次表；攒到帧末只处理一次
    // （`docs/InputDesign.md` §5 的 isDirty 生命周期、TaskGuide T-4 那句"同一帧里
    // 最后统一消费"）。

    /**
     * 帧末统一消费：由窗口层在主循环的收尾那一步调一次（**只对活跃场景**）。
     *
     * 它做两件事：推进每个节点的安静帧计数（够数就清脏），然后清掉指针脏。
     * 它**不重建绘制列表**——那是帧首 `sortDrawList()` 的事；消费的语义是
     * "这一帧的处理已经完成了，把账记上"，不是"立刻去做重活"。
     *
     * 静默 / 停放 / 关闭的场景不消费：它们的脏标记要留着，切回来时才有意义。
     */
    void ConsumeFrameDirty() noexcept;

    /** 这一帧（上一次消费时）还有多少节点处于**重绘脏**——也就是"画面还在动"。 */
    int GetRepaintingNodeCount() const noexcept;

    /** 这一帧还有多少节点处于**命中脏**（要重算悬停 / 将来要重烘表）。 */
    int GetHitDirtyNodeCount() const noexcept;

    /**
     * 指针脏：指针状态变过、这一帧的悬停判断还没做。
     *
     * 它**不代表命中表要重烘**——指针移动不改任何几何。把两者混在一起，
     * 就会出现"鼠标一动就重烘整张表"（`docs/InputDesign.md` §5 把 isDirty
     * 单列出来正是为了避开这个）。
     */
    bool IsPointerDirty() const noexcept;

    // ---------------- 命中（T-4：烘焙表 + 三态） ----------------
    //
    // 形态照 `docs/InputDesign.md` §6 / §10：静态层查**烘焙好的表**（簇 + 格子 +
    // CSR 候选），动态层自判（它不进表），两者按**同一套层级规则**合并成三态。
    //
    // 两件事和设计稿不同，都是被现状决定的（写在这里免得下一个人以为漏了）：
    //
    //   - **没有溢出桶**（§10.5 规则 2）：那条规则是给"运行时新增的 UI 不在
    //     生成期结构里"准备的。我们的组件**构造即登记进绘制列表**，而表是
    //     跟着列表重建的——所以"新增"表现为"表脏了"，不是"表外还挂着条目"。
    //   - **没有跨簇条目仲裁**（§10.5 规则 4）：那条规则是给"两个条目谁在上面
    //     说不清"的*偏序*准备的。我们的 `IsAbove` 已经是**局部层级**（树序 +
    //     兄弟内 z），但它仍然是一个**严格全序**——任何两个条目都可比，所以查表
    //     按这个序降序取第一个命中的就行。索引层面一个条目会登记进多个簇（大面板
    //     横跨粗块），那只是为了查得到，不涉及"谁在上面"的仲裁。
    //     **等到引入真正不可比的东西（比如允许子节点逃出父级的 portal 层）时，
    //     这条才需要补回来。**

    /**
     * 查一次命中：命中的最上面那个 + 三态。
     *
     * 坐标是**设计坐标**。悬停与点击共用它（§6）；点击不过脏标记，单独调一次。
     *
     * 返回的是**静态层与动态层合并之后**的答案：点在动态组件的洞里时，
     * 会拿动态层最上面那个和静态结果比 z 序。
     */
    HitResult QueryHit(float designX, float designY) noexcept;

    /** 命中表里有多少条静态条目 / 多少个簇 / 多少个格子（自检与预算估算用）。 */
    std::size_t GetHitTableEntryCount() noexcept;
    std::size_t GetHitTableClusterCount() noexcept;
    std::size_t GetHitTableCellCount() noexcept;

    /** 一次查询最多扫过多少候选（上界；用来判断格子尺寸定得是不是太粗）。 */
    std::size_t GetHitTableMaxCellCandidates() noexcept;

    // ---------------- 骨架登记的写入口 ----------------
    //
    // 只由 InkingWidget 的构造 / 析构 / SetParent 调用，别在业务代码里手工调。

    /** 把一个节点加进绘制列表（内容脏）。 */
    void AddDrawNode(InkingWidget* node) noexcept;

    /** 把一个节点从绘制列表里摘掉（内容脏）。 */
    void RemoveDrawNode(InkingWidget* node) noexcept;

    /** 场景析构前把子树里指向自己的 _scene 摘成 nullptr，避免野指针。 */
    void DetachSceneFromNodes() noexcept;

    /** 骨架结构变了多少次（构造 / 析构 / 换父级 / 换场景）。自检用得上。 */
    std::uint64_t GetStructureGeneration() const noexcept;

    // ---------------- 指针输入（今天的最小闭环） ----------------

    /**
     * 把这一帧的指针状态喂进来。由窗口层在主循环里调。
     *
     * 坐标是**设计坐标**（窗口层已经用渲染器换算过，含 letterbox 偏移）。
     * 这里只存不判——真正的派发在 `DispatchPointer()`，那是"每帧只判断一次"
     * 那条设计（docs/InputDesign.md §1）的落点。
     *
     * @param inside 指针在不在窗口里。移出窗口时不该继续悬停/按下。
     */
    void SetPointerState(float designX, float designY, bool down,
                         bool inside) noexcept;

    /**
     * 把这一帧的**滚轮增量**喂进来（A4）。由窗口层在主循环里调。
     *
     * 与 `SetPointerState` 一样"只存不判"：真正的派发在 `DispatchPointer()`。
     * 一帧里调多次会**累加**（窗口层每帧调一次，值是 `MouseInput` 本帧的累积量）。
     *
     * 滚轮是**一次性量**而不是跨帧状态：本帧没派发出去（没人收）也照样在
     * 帧末被 `ConsumeFrameDirty()` 清掉——"滚了但没人要"就是丢掉，
     * 不该攒到下一帧再触发一次（那会变成"切场景/移动鼠标后莫名滚了一下"）。
     */
    void SetPointerWheel(float deltaX, float deltaY) noexcept;

    // ---------------- 指针状态的只读查询（给动态组件"自己拉"用） ----------------
    //
    // 静态组件的输入是**推结果**的（DispatchPointer 扫表之后调 MouseHover /
    // MousePress），动态组件的输入是**拉状态**的：它不进表、场景也不替它判，
    // 只在指针真的变了的那一帧收到一次 `onPointerStateChanged`（0.4.1 T-02），
    // 然后在那一趟里读这里的四个值，自己判命中、自己推三态
    // （docs/InputDesign.md §7，用法见 InkingDynamicButton::JudgePointer）。
    //
    // 四个都只是"当前这一帧的状态"，不标脏、不改任何东西。

    /// 指针的 x，设计坐标。
    float GetPointerX() const noexcept;

    /// 指针的 y，设计坐标。
    float GetPointerY() const noexcept;

    /// 左键按着没（**原始按键状态**，不折进"在不在窗口里"）。
    bool IsPointerDown() const noexcept;

    /// 指针在不在窗口里。窗口外不该继续悬停。
    bool IsPointerInside() const noexcept;

    /**
     * 本帧的滚轮增量（还没消费的那份）。给**动态组件自己拉**用（A4）。
     *
     * 静态组件的滚轮是**推**的（`onPointerWheel`）；动态组件不进派发，
     * 所以在自己的 `Tick` 里读这里。它与"指针位置"的区别同 `onPointerWheel`
     * 的注释：位置每帧都能问，滚轮只是一次性的量（帧末清）。
     */
    float GetPointerWheelX() const noexcept;
    float GetPointerWheelY() const noexcept;

    /**
     * 按当前指针状态派发一次：悬停进出、按下、抬起触发点击。
     *
     * **这是今天的最小实现**：直接线性扫绘制列表（从列表尾往前，z 最大的先问），
     * 取第一个 `HitTest` 命中的节点。完整方案（静态烘焙的命中表、三态
     * Block / PassThrough / Miss）在 docs/InputDesign.md，还没落地——
     * 但接口是照着那时候的形状定的，换实现时调用方不用改。
     *
     * 每帧调一次，幂等：同一帧调两次结果一样。
     */
    void DispatchPointer();

    /** 现在按着哪个节点（没有时为 nullptr）。自检观测口。 */
    InkingWidget* GetPressedNode() const noexcept;

    /**
     * 拖动归属（A4）：现在谁"抓住"了这次按下（没有时为 nullptr）。
     *
     * 它和 `GetPressedNode()` 是同一个答案——拖动捕获不需要第二份状态，
     * "谁被按下"本来就是"谁收这次拖动的全部后续事件"。这里单列一个名字，
     * 是因为两个问题（"按着谁" / "拖动归谁"）问的是不同的事，将来若要把
     * 捕获与按下拆开（例如捕获能转移到别的节点），改这里就行。
     */
    InkingWidget* GetDraggingNode() const noexcept;

    /** 指针现在悬停在哪个节点上（没有时为 nullptr）。自检观测口。 */
    InkingWidget* GetHoveredNode() const noexcept;

    // ---------------- 键盘（A3） ----------------
    //
    // 与指针的分工完全不同：指针事件**按位置**分发（谁被指着谁收），
    // 键盘事件**按焦点**分发（只有焦点节点收）。所以这里有两件事：
    //   1. **焦点归属**：谁拿键盘（规则见 `SetKeyboardFocus`）；
    //   2. **事件派发**：把本帧的按键 / 文本推给那个节点（`DispatchKeyboard`）。
    //
    // 状态（现在按着哪些键 / 在不在打字）由场景转存一份供组件查询——
    // 组件不必自己认识 `KeyboardInput`（那是窗口层的东西），
    // 与"指针状态由 `SetPointerState` 喂进来"同一条思路。

    /**
     * 把这一帧的键盘输入喂进来（状态 + 本帧事件）。**窗口层每帧调一次**。
     *
     * 与 `SetPointerState` 一样"只存不判"：真正的派发在 `DispatchKeyboard()`，
     * 那是"每帧只判断一次"那条设计的落点。
     */
    void SetKeyboardInput(const KeyboardInput& input) noexcept;

    /** 当前键盘状态（组件轮询用：`IsKeyDown` / `IsTyping` / 修饰键）。 */
    const KeyboardState& GetKeyboardState() const noexcept;

    /**
     * 把键盘焦点交给某个节点（传 nullptr = 清空）。
     *
     * **焦点规则（A3 定，只有这三条）**：
     *   1. **点击获得**：指针**按下**时命中的那个节点，若它的
     *      `WantsKeyboardFocus()` 为 true，焦点归它（不想要焦点的组件点了也没用）；
     *   2. **点别处就换**：下一次按下落在别的节点（或空白）上时，旧焦点先收到
     *      `onFocusChanged(false)`，再决定新焦点是谁——点空白 / 点不要焦点的组件
     *      就是"清空"；
     *   3. **看不见了就释放**：焦点节点在树里不可见（自己被隐藏或祖先被隐藏）、
     *      或从渲染树里摘掉（析构 / 换父级）时自动清空——隐藏的输入框不该还在收键盘。
     *
     * 业务也可以直接调它（"打开弹窗时把焦点给输入框""提交完把焦点还回去"），
     * 那不属于上面三条自动规则，是有意的显式接管。
     */
    void SetKeyboardFocus(InkingWidget* node) noexcept;

    /** 现在谁拿着键盘焦点（没有时为 nullptr）。 */
    InkingWidget* GetKeyboardFocus() const noexcept;

    /** 清空焦点（等价 `SetKeyboardFocus(nullptr)`）。 */
    void ClearKeyboardFocus() noexcept;

    /**
     * 把本帧的键盘事件推给**焦点节点**（每帧一次，排在 `DispatchPointer()` 之后）。
     *
     * 顺序有意如此：先按位置把焦点算好（可能刚因为点击换了人），再把这一帧的
     * 按键推给**新的**焦点节点——否则"点进输入框并立刻打字"的第一下会丢。
     *
     * 它还会比较"在不在打字"并在变化时推一次 `onTypingChanged`。
     */
    void DispatchKeyboard();

    // ---------------- 只给 SceneLibrary 调 ----------------

    /**
     * 被库关掉时的通知（`SceneLibrary::CloseScene`）。
     *
     * 摘掉活跃态与登记：关掉之后它不渲染、不再活跃，也不能再注册。
     * 数据全部保留，仍然可读——"先把重要数据 move 出来再关"就是这么用的。
     */
    void MarkClosed() noexcept;

    /** 被库切换活跃态时的通知（`SceneLibrary::SetActiveScene`）。 */
    void SetActiveFromLibrary(bool active) noexcept;

    // ---------------- 预加载：把"构造"与"init"分开 ----------------
    //
    // 场景**构造即登记**，所以构造体里做重活会拖住整个启动。预加载把这些活
    // 挪到构造之后、一个显式的 init 步骤里：
    //
    //   | 走哪条路                          | `onPreload` 跑在   | 收尾在哪 |
    //   | --------------------------------- | ------------------ | -------- |
    //   | `SceneLibrary::PreloadScene(名)`  | **管家线程**       | UI 线程帧首 |
    //   | `InkingWindow::Show(名)`          | UI 线程（同步）    | 紧随其后 |
    //
    // 两条路**共用同一套状态机**，所以"先手动调过、再 Show"只跑一次，反过来
    // 也一样——重复调用在受理那一步就被 CAS 挡掉。

    /** 预加载状态。跨线程：管家线程写、UI 线程读。 */
    enum class PreloadState : std::uint8_t {
        Idle = 0,  ///< 还没人要求过
        Queued,    ///< 已受理：任务进了队列，还没开跑（或正在跑）
        Done,      ///< `onPreload()` 返回了 true
        Failed,    ///< `onPreload()` 返回了 false
    };

    /** 当前预加载状态（读的是原子量，任意线程安全）。 */
    PreloadState GetPreloadState() const noexcept;

    /** 预加载**成功**跑完了吗（`Done`）。 */
    bool IsPreloaded() const noexcept;

    /** 预加载跑完了但失败了吗（`Failed`）。 */
    bool IsPreloadFailed() const noexcept;

    /** 预加载有结果了吗（成不成功都算）。 */
    bool IsPreloadFinished() const noexcept;

    // ---------------- 只给 SceneLibrary / 窗口层调 ----------------

    /**
     * 要求一次**异步**预加载：任务丢进 `TaskQueue`，`onPreload()` 在管家线程跑。
     *
     * @return 受理了才返回 true；已经受理过 / 已跑过 / 场景已关闭 → false。
     *         所以重复调用天然无害，调用方不用自己判重。
     *
     * **只从 UI 线程调**（`SceneLibrary::PreloadScene` 就是那么调的）：里面读的
     * `_closed` 是 UI 线程私有的非原子量。
     */
    bool RequestPreload();

    /**
     * 在**调用线程**（必须是 UI 线程）上直接跑完预加载，并当场做 UI 侧收尾。
     * `InkingWindow::Show()` 走这条——它之后马上就要渲染这个场景，"加载还没好"
     * 没有意义。
     *
     * @return 这次真的跑了才返回 true（只有 `Idle` 那一次能跑）。
     */
    bool PreloadNow();

    /**
     * 预加载有结果了、而且**还没通知过**的话，就在**调用它的线程**上调一次
     * `onPreloadFinished(ok)`。
     *
     * 只有 `SceneLibrary::DrainPreloadCompletions()` 在 UI 线程的帧首调它——
     * **这就是"管家线程干完活之后回 UI 线程"的那条路**。
     * @return 这次真的调了才返回 true。
     */
    bool ConsumePreloadFinish();

protected:
    /**
     * 把 this 登记进 SceneLibrary。
     *
     * 与后端 ShineBasicModule::registerToStatusChecker() 同一套写法：
     * 在**构造函数末尾**调用，表示自身已经构造完成、可以登记了。
     */
    void registerToSceneLibrary();

    // ---------------- [√] 可重写钩子：两组每帧逻辑 ----------------

    /**
     * **固定逻辑步**的场景逻辑：按固定步长跑，和渲染解耦。
     *
     * 适合状态机、计时、和画面无关的推进。即使场景静默（被别的场景顶掉）
     * 也照跑——这样后台的事情不会因为切场景就停摆。
     * 想"不渲染就彻底不动"，把逻辑放 `onRenderBoundTick`。
     */
    virtual void onTick(double fixedStepSeconds);

    /**
     * **跟着渲染的那组逻辑**：只有活跃（正在渲染）时才跑，每个渲染帧一次。
     *
     * 适合插值、和画面同步的推进——跑在渲染帧上，和实际画出来的那帧对齐。
     * 入参是真实 delta，不保证固定。
     */
    virtual void onRenderBoundTick(double deltaSeconds);

    // ---------------- [√] 可重写钩子：预加载 ----------------

    /**
     * @brief 预加载：把**耗时的准备**从构造函数里搬出来，写在这里。
     *
     * 默认什么都不做、直接返回 true——不重写就是"我没什么要预加载的"。
     *
     * 它跑在**管家线程**（`SceneLibrary::PreloadScene` 那条路）或 UI 线程
     * （`InkingWindow::Show` 那条路）。所以里面**只能做与 UI 无关的准备**：
     * 读文件、解析配置、解码成 `SDL_Surface*` 这类。
     *
     * 三条禁令（越界就是数据竞争，而且**不报错、只偶发**）：
     *   1. **不要碰场景树**——`new InkingStaticButton(this, ...)` 会登记进绘制
     *      列表，而 UI 线程正在遍历它；
     *   2. **不要碰渲染器**——`SDL_CreateTextureFromSurface` 不是线程安全的；
     *   3. **不要读写任何渲染 / Tick 里也会读写的成员**，例外是"只在
     *      `IsPreloaded()` 为真之后才读"——那时有一次 release/acquire 的
     *      先行关系兜着。
     *
     * 第 1、2 条要干的活放 `onPreloadFinished()`。
     *
     * @return 准备好了没有。false 会让场景停在 `Failed`，`IsPreloaded()` 为假。
     */
    virtual bool onPreload();

    /**
     * @brief 预加载的 **UI 侧收尾**：在 UI 线程上跑。
     *
     * 按加载结果建组件、建纹理、把 `onPreload` 里读好的数据装进场景——都写在这里。
     * 异步那条路的调用时机是"预加载跑完之后的第一个帧首"，同步那条路是紧随
     * `onPreload()` 之后、同一次调用里。**整个生命周期只来一次。**
     *
     * @param ok 就是 `onPreload()` 的返回值。
     */
    virtual void onPreloadFinished(bool ok);

    /**
     * 场景在被绘制时，除了绘制列表里的子节点，自己要不要也画一笔。
     * 基类默认不画（场景是容器，不是可见组件）。
     */
    virtual void onRenderScene(SDL_Renderer* renderer);

    /**
     * 渲染入口：按 z 序把绘制列表里的节点提交给渲染器。
     *
     * **protected**：只有框架该调它（窗口层经 `SceneLibrary::RenderScene`
     * 走一道，保证"只有活跃场景会被渲染"这条规矩有唯一落点）。
     */
    void Render(SDL_Renderer* renderer);

    /** 场景名，需要在构造时直接构造好（令牌要用它当登记用的 key）。 */
    std::string _sceneName;

private:
    friend class SceneLibrary;

    /** 重建绘制列表：**按树重建**（树是唯一真相源），再按 z 升序排好。 */
    void sortDrawList();

    /**
     * 深度优先（先序）收一棵子树的节点到 `_drawList`（A2/A3 的落点）。
     *
     * 为什么按树扫而不是"谁登记就收谁"：登记是**增量**的，而增量一定有漏
     * ——构造时挂在还没进场景的父级上的子树（先建面板、后 AddChild）、
     * 换父级之后新父级的子孙、`SetVisible` 前后重新挂接的节点，全都会漏。
     * 漏了不报错，只是"节点在、`IsVisibleInTree()` 为真、就是不画"。
     * 树本来就在那儿，扫一遍 O(N)，比维护"哪次登记漏了"便宜。
     */
    void collectDrawList(InkingWidget* node);

    /** 场景根矩形的初始数据；三个构造函数共用。 */
    static AnchorData sceneRootAnchorData();

    /** 三个构造函数的公共收尾：盖章成根 + 登记进库。 */
    void finishConstruction();

    /**
     * 把"在盖章成根之前就建出来、因而没找到场景"的节点补登记进来。
     *
     * 场景是构造完成才盖章的，所以**场景构造函数体里建出来的子节点**
     * （最典型的是按钮成员）沿父链找不到它。不补这一趟，它们既不渲染
     * 也点不到——而且不报错，只有画面上什么都没有。
     */
    void rebindEarlyChildren();

    /** 按当前指针位置找最上面那个命中的节点（线性扫，见 DispatchPointer）。 */
    InkingWidget* hitTestTopmost(float designX, float designY) noexcept;

    /**
     * 重建命中表。由 `sortDrawList()` 在**真的重建了列表之后**调——
     * 表与列表必须同生同灭：顺序或内容一变，候选的 z 序与包络就都过期了。
     *
     * 变换（平移 / 旋转 / 缩放）**不在**重建触发源里：表里存的是组件的
     * **保守包络**（覆盖所有可能变换），精判用当前变换——所以表全程冻结
     * （`docs/InputDesign.md` §11「变换通道不改表」）。
     */
    void rebuildHitTable();

    /**
     * 动态层最上面那个命中者（动态组件不进表，只能现问）。
     *
     * 只在"点落在某个动态洞里"时才会被调用——洞就是为省这一趟而存在的。
     */
    InkingWidget* topmostDynamicHit(float designX, float designY,
                                    HitKind& kind) noexcept;

    /**
     * 绘制列表，按 z 升序（先画的在底下）。
     *
     * 析构顺序上有一件事要记住：本类析构体先跑（去摘子节点上的 `_scene`），
     * 然后是成员 `_drawList`，最后才是基类 `InkingWidget`——而 `_scene`
     * 是**基类的成员**，它比 `_drawList` 活得久。所以"列表先没、_scene 后没"
     * 是正常的，析构体里不要去碰列表。
     */
    std::vector<DrawItem> _drawList;

    /** 延迟构造的登记令牌：登记发生在构造函数体内，不在初始化列表里。 */
    std::optional<SceneToken> _sceneRegisterToken;

    /** 列表内容变了（增删、换父级换场景 → 需要剔除失效项并重算坐标）。 */
    bool _contentDirty = true;
    /** 只有顺序变了（zindex → 内容不变，只需重排）。 */
    bool _orderDirty = true;
    /** 骨架结构变更次数，覆盖"内容"与"顺序"两类。 */
    std::uint64_t _structureGeneration = 0;

    /**
     * 烘焙好的命中表（静态条目 + 动态洞）。
     *
     * 与 `_drawList` **同生同灭**：`sortDrawList()` 真的重建列表时会顺手重建它
     * （`rebuildHitTable()`）。没脏的时候一次都不碰——这正是"每帧 0 次重烘"
     * 的来源（`InputDesign.md` §13）。
     */
    HitTable _hitTable;

    /** 正在渲染（由 SceneLibrary 维护，同期最多一个）。 */
    bool _active = false;
    /** 被库关掉过：不能再注册，也不再活跃。 */
    bool _closed = false;
    /** 注册被驳回（名字被占）。这是"停放态"。 */
    bool _rejected = false;

    // ---------------- 预加载状态 ----------------
    //
    // 它**跨线程**（管家线程写、UI 线程读），所以必须是原子的。三态由一次
    // CAS 推进，"受理"就是唯一的闸门——重复调用在 `Idle → Queued` 那一步
    // 就被挡掉，不需要另加锁。
    std::atomic<PreloadState> _preloadState{PreloadState::Idle};

    /**
     * "UI 侧收尾已经做过"的守卫。
     *
     * 它**只被 UI 线程读写**（`ConsumePreloadFinish` / `PreloadNow` 都只从
     * UI 线程调），所以刻意**不**跟 `_preloadState` 混在一个原子里：那个是
     * 跨线程的，这个是 UI 线程私有的。分开放，才不会有人以为它也得加锁。
     */
    bool _preloadFinishedNotified = false;

    // ---------------- 指针状态（每帧由窗口层喂） ----------------
    //
    // 三个都是**非拥有**指针，生命周期靠"节点析构时从列表里摘掉"这条纪律：
    // RemoveDrawNode 会把这里指向该节点的指针清成 nullptr，所以不会悬垂。

    float _pointerX = 0.0f;     ///< 设计坐标
    float _pointerY = 0.0f;
    bool _pointerDown = false;  ///< 左键按着没
    bool _pointerInside = false;///< 指针在不在窗口里

    /**
     * 上一帧的按下状态。
     *
     * 放成员不放函数内静态量：静态量会让"两个场景的按下历史"串在一起，
     * 自检里建了又拆的场景会把上一帧的状态留给下一个（实测这种串味最难查）。
     */
    bool _pointerWasDown = false;

    /**
     * 指针脏：指针状态变过，这一帧的悬停判断还没做。
     *
     * 由 `SetPointerState` 在**真的变了**的时候置位（每帧都喂同一个坐标不算变），
     * 帧末统一消费时清掉。T-4 的"每帧 query"就是拿它当开关。
     */
    bool _pointerDirty = false;

    /**
     * **当前这份**指针状态已经通知过动态节点了吗（0.4.1 T-02）。
     *
     * 键是"状态"不是"帧"：`SetPointerState` 只要真的变了就会把它清回去
     * （`_pointerDirty` 同时置位），于是"指针变一次 ⇒ 通知一次"。
     * 主循环每帧只喂一次状态，所以它同时也就成了"每帧最多一次"；
     * 而自检 / 手动跑帧会在同一帧里连着改，那时按状态算才对
     * （按帧算会把第二次变化吞掉——实测就是这条把动态档的悬停断言打红的）。
     *
     * 帧末 `ConsumeFrameDirty` 一并结清，避免跨帧残留。
     */
    bool _pointerNotified = false;

    /**
     * 上一次帧末消费时，还有多少节点各自处于脏状态。
     *
     * 纯观测值（自检与诊断用）：动画在跑的时候它应该一直 > 0，
     * 动画停下 `kRepaintQuietFrames` 帧之后回落到 0——"画面稳了"就是这么看的。
     */
    int _repaintingNodeCount = 0;
    int _hitDirtyNodeCount = 0;

    InkingWidget* _hoveredNode = nullptr;  ///< 这一帧悬停在谁身上
    InkingWidget* _pressedNode = nullptr;  ///< 按下去时抓的是谁（拖出按钮也还是它）

    // ---------------- 滚轮与拖动（A4） ----------------

    /**
     * 本帧等待派发的滚轮增量（`SetPointerWheel` 累加、`DispatchPointer` 派发、
     * `ConsumeFrameDirty` 清零）。
     *
     * 派发之后**不清零**：动态组件要在自己的 `Tick` 里读它
     * （`GetPointerWheelX/Y`），而 `Tick` 排在 `DispatchPointer` 之后。
     * "同一帧重复派发给静态组件"由 `_wheelDispatched` 挡住。
     */
    float _wheelX = 0.0f;
    float _wheelY = 0.0f;
    bool _wheelDispatched = false;  ///< 这一帧的滚轮已经推给静态目标了

    /**
     * 上一次拖动事件时的指针位置，用来算 `onPointerDrag` 的增量。
     *
     * 按下那一刻会跟着重置（`_lastDragX = _pointerX`），否则"按下之后的第一帧"
     * 会把从上一个位置到按下点的位移当成拖动增量，滑块会跳一下。
     */
    float _lastDragX = 0.0f;
    float _lastDragY = 0.0f;

    // ---------------- 键盘（A3） ----------------
    //
    // 和指针一样都是**非拥有**指针：焦点节点析构 / 被摘掉时，`RemoveDrawNode` /
    // `DetachSceneFromNodes` 会把 `_keyboardFocus` 清成 nullptr，不留悬垂。

    /// 这一帧的键盘状态（窗口层喂进来的拷贝，供组件查询）。
    KeyboardState _keyboardState{};

    /// 这一帧的键盘事件（同上）。派发完由窗口层在帧末清掉 `KeyboardInput` 自己那份。
    std::vector<KeyInputEvent> _keyboardEvents{};

    /// 键盘焦点归谁；nullptr = 没有人拿键盘。
    InkingWidget* _keyboardFocus = nullptr;

    /**
     * 上一次派发时"在不在打字"。
     *
     * 放成员不放函数内静态量：静态量会让两个场景的打字状态串在一起
     * （和 `_pointerWasDown` 同一个理由）。
     */
    bool _keyboardTyping = false;
};

}  // namespace inking
