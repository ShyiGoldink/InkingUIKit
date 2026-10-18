#include <scene/InkingScene.h>

#include <config/window_config.h>
#include <detail/InkLog.h>
#include <render/InkingBatch.h>

// `detail::AncestorClipInDevice`（裁剪链换算）与动态档那个 `Tick` 入口：
// 前者是跨类几何工具，后者只有"几何会自己变"的那一档才有。
#include <detail/InkingGeometry.h>
#include <widget/InkingDynamicWidget.h>

// 异步预加载的底座：任务队列 + 它的任务模型。
#include <thread/TaskQueue.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <any>
#include <vector>

namespace inking {

namespace {

constexpr const char* kModuleName = "InkingScene";

/**
 * 设计坐标 → 设备像素的换算。
 *
 * letterbox 是整幅等比缩放，倍率由窗口层每帧写进根锚点（不标脏）。
 * 这里只做绘制换算，不参与几何，命中表也不会因为缩放重烘。
 */
float magnify(const InkingWidget& node) {
    const float value = node.GetMagnification();
    return value > 0.0f ? value : 1.0f;
}

}  // namespace

// ---------------------------------------------------------------------------
// 构造 / 析构
// ---------------------------------------------------------------------------

InkingScene::InkingScene() : InkingStaticWidget(nullptr, sceneRootAnchorData()) {
    finishConstruction();
}

InkingScene::InkingScene(const std::string& sceneName)
    : InkingStaticWidget(nullptr, sceneRootAnchorData()), _sceneName(sceneName) {
    finishConstruction();
}

InkingScene::InkingScene(const std::string& sceneName,
                         const AnchorData& anchorData)
    : InkingStaticWidget(nullptr, anchorData), _sceneName(sceneName) {
    finishConstruction();
}

InkingScene::~InkingScene() {
    // 注销在令牌的析构里做，这里只说明顺序：本类析构体 → 成员（令牌、
    // 绘制列表）→ 基类 InkingStaticWidget。所以令牌一定在 _sceneName 之前销毁，
    // 而注销正是拿 _sceneName 当 key 的——key 全程有效。

    // 场景是渲染树的根，子树里的每个节点都记着"我属于这个场景"（_scene）。
    // 场景先走（比如两个场景互相独立、析构顺序不由场景决定）时，那些指针
    // 就成了野指针，而节点析构时还会拿着它去反注册——先摘干净再拆自己。
    DetachSceneFromNodes();
}

void InkingScene::finishConstruction() {
    // 盖章成渲染树的根：构造期不能靠父链查找认出场景——构造 InkingScene 时
    // 它的动态类型还是 InkingWidget，那一趟查找认不出来（见头文件说明）。
    //
    // 注意场景**自己不进自己的绘制列表**：它是容器，不是要画的一笔。
    // "只留属于本场景的节点"这条过滤（见 sortDrawList）靠 _scene == this
    // 判定，而场景自己正是唯一一个 _scene == this 的根，所以它天然被排除。
    //
    // 这里不需要"补登记已有子树"：子节点要认这个场景当父级，场景就必须先
    // 存在，所以子节点只可能在场景之后构造，而那时沿父链登记已经生效。
    MarkAsSceneRoot(this);

    // 盖章之后回头补登记：在构造函数体里建出来的子节点（最典型的是按钮成员）
    // 那时还没有"场景"可认，沿父链找不到它。不补这一趟它们既不渲染也点不到，
    // 而且一声不响——画面上什么都没有，最难查。
    rebindEarlyChildren();

    registerToSceneLibrary();
}

void InkingScene::rebindEarlyChildren() {
    // 复制一份再遍历：补登记会往 _drawList 里 push，边遍历边改容器就是悬垂。
    std::vector<InkingWidget*> early;
    early.reserve(_drawList.size());
    for (const DrawItem& item : _drawList) {
        if (item.node != nullptr && item.node->GetScene() == nullptr) {
            early.push_back(item.node);
        }
    }

    for (InkingWidget* node : early) {
        // **只认领"父链的根就是本场景"的节点。**
        //
        // 为什么要这道校验：构造早于"场景盖章"的成员（按钮、标签、遮罩……）在
        // 构造时沿父链找不到场景，于是「还没有场景」；而**别的场景**的成员也是
        // 这个状态。原来这里只看 `GetScene() == nullptr` 就收下，于是谁的构造先
        // 跑完、谁的补登记就先到——另一个场景的成员就被**认领进了错误的场景**。
        //
        // 症状（实测）：标题场景那层 `titleShade`（1280×720、`0.02|0.03|0.07|0.46`
        // 的全屏遮罩，z=5）被正片场景认领，于是**整个正片被压暗 46%**——立绘的
        // 透明区、背景、对话框全部发黑，看着像"到处都是半透明黑底"。
        //
        // 注意：**不能用 `GetScene()` 判**（那正是被 RebindScene 写坏的那个值，
        // 认领之后它看起来"本来就属于这里"），也不能用 `GetSceneAncestor()`
        // （场景还没盖章时它返回 nullptr）——只能顺着 `GetParent()` 找根。
        InkingWidget* root = node;
        while (root->GetParent() != nullptr) {
            root = root->GetParent();
        }
        if (root != this) {
            continue;
        }
        node->RebindScene(this);
    }
}

// ---------------------------------------------------------------------------
// 登记
// ---------------------------------------------------------------------------

void InkingScene::registerToSceneLibrary() {
    // 构造即在令牌里完成登记，析构即在令牌里完成注销：
    // 调用方永远不会忘记写注销，这正是 RAII 的用意。
    //
    // 登记可能被**驳回**：同名场景已经占了库。驳回时令牌是空转的
    // （不会去注销别人的登记），本场景进入停放态——对象还在、数据还在，
    // 但永远不渲染也不 tick，直到占用者被关闭后重建。
    _sceneRegisterToken.emplace(_sceneName, this);

    switch (_sceneRegisterToken->GetResult()) {
        case SceneLibrary::RegisterResult::Ok:
            break;
        case SceneLibrary::RegisterResult::NameTaken:
            _rejected = true;
            INK_LOG_WARN(kModuleName,
                         "场景名已被占用，注册被驳回：" + _sceneName
                             + "（此场景不会渲染；要复用这个名字请先 "
                               "SceneLibrary::CloseScene）");
            break;
        case SceneLibrary::RegisterResult::Invalid:
            INK_LOG_DEBUG(kModuleName, "场景未登记（名字为空）");
            break;
    }
}

// ---------------------------------------------------------------------------
// 查询
// ---------------------------------------------------------------------------

const std::string& InkingScene::GetSceneName() const noexcept {
    return _sceneName;
}

bool InkingScene::IsRegistered() const noexcept {
    // "有令牌"不算数：令牌可能是被驳回后空转的那个（见 RegisterToken）。
    // 要问令牌自己真的进库了没有——这个 bug 以前被自检抓到过。
    return _sceneRegisterToken.has_value() && _sceneRegisterToken->IsRegistered();
}

bool InkingScene::IsRejected() const noexcept {
    return _rejected;
}

bool InkingScene::IsClosed() const noexcept {
    return _closed;
}

bool InkingScene::IsActive() const noexcept {
    return _active;
}

std::uint64_t InkingScene::GetStructureGeneration() const noexcept {
    return _structureGeneration;
}

// ---------------------------------------------------------------------------
// 每帧回调
// ---------------------------------------------------------------------------

void InkingScene::TickLogic(double fixedStepSeconds) {
    // 停放（注册被驳回）与已关闭的场景不跑任何逻辑：它们连"存在感"都没有。
    if (_rejected || _closed) {
        return;
    }

    // 场景自己的逻辑：固定步长，和渲染解耦。静默态也照跑——
    // 后台的事情不该因为切场景就停摆。
    onTick(fixedStepSeconds);

    // 动态组件才是"几何会自己变"的那一档，逐个喂固定步。
    // 静态组件压根没有 Tick 入口，所以这里不用问类型——
    // 分流在登记那一刻就在类型上了（docs/InputDesign.md §2）。
    //
    // 注意复制一份指针再遍历：组件的 Tick 里可能会增删节点
    // （构造 / 析构会改 _drawList），拿着引用边遍历边改就是悬垂。
    //
    // 但"复制一份"只挡住了**容器本身**失效（迭代器 / 引用），挡不住
    // "回调里把**后面某个**节点析构掉"——那一份快照里剩下的指针就成了野指针。
    // 所以每步再比一次结构代次：变过就停掉本轮（剩下的节点下一帧照常收 Tick），
    // 代价是一次整数比较（`AGENTS.md` §6 第 103 条）。
    std::vector<InkingWidget*> dynamicNodes;
    dynamicNodes.reserve(_drawList.size());
    for (const DrawItem& item : _drawList) {
        if (item.node != nullptr && item.node->IsDynamic()) {
            dynamicNodes.push_back(item.node);
        }
    }

    const std::uint64_t generation = _structureGeneration;
    for (InkingWidget* node : dynamicNodes) {
        if (_structureGeneration != generation) {
            INK_LOG_DEBUG(kModuleName,
                          "逻辑步里骨架变了：本轮剩下的节点不再 Tick（下一帧再来）");
            break;
        }
        static_cast<InkingDynamicWidget*>(node)->Tick(
            static_cast<float>(fixedStepSeconds));
    }
}

void InkingScene::TickFrame(double deltaSeconds) {
    // 只有活跃（正在渲染）时才跑"跟着渲染的那组"。
    // 静默 / 停放 / 关闭都不跑——画面外的插值推进没有意义。
    if (_rejected || _closed || !_active) {
        return;
    }

    onRenderBoundTick(deltaSeconds);

    // 再把这一帧的真实 delta 分发给**所有**节点（静态也要）：
    // 颜色 / 透明度过渡不该因为"几何不变"就收不到帧——那正是静态档的代价边界。
    // 动态档的几何推进走的是另一条（`TickLogic` 的固定逻辑步），两者别混。
    //
    // 复制一份指针再遍历：回调里可能增删节点（构造 / 析构会改 _drawList），
    // 拿着引用边遍历边改就是悬垂——和 TickLogic 那边同一个理由。
    std::vector<InkingWidget*> nodes;
    nodes.reserve(_drawList.size());
    for (const DrawItem& item : _drawList) {
        if (item.node != nullptr) {
            nodes.push_back(item.node);
        }
    }

    const float delta = static_cast<float>(deltaSeconds);
    const std::uint64_t generation = _structureGeneration;
    for (InkingWidget* node : nodes) {
        // 同 TickLogic：快照只挡容器失效，挡不住"回调里析构掉后面的节点"。
        // 结构代次一变就停掉本轮（过渡动画少推进一帧，看不出来）。
        if (_structureGeneration != generation) {
            INK_LOG_DEBUG(kModuleName,
                          "渲染帧里骨架变了：本轮剩下的节点不再推进动画（下一帧再来）");
            break;
        }
        node->TickAnimation(delta);
    }
}

void InkingScene::onTick(double /*fixedStepSeconds*/) {}

void InkingScene::onRenderBoundTick(double /*deltaSeconds*/) {}

bool InkingScene::onPreload() { return true; }

void InkingScene::onPreloadFinished(bool /*ok*/) {}

// ---------------------------------------------------------------------------
// 预加载的实现（构造与 init 分开）
// ---------------------------------------------------------------------------

InkingScene::PreloadState InkingScene::GetPreloadState() const noexcept {
    // **acquire** 是有意的：一旦读到 `Done`，`onPreload` 里写下的那批数据也
    // 就跟着可见了。"管家线程写完 → release 状态 / UI 线程 acquire 状态"这条
    // 先行关系是整套跨线程预加载的安全依据，别把它降成 relaxed。
    return _preloadState.load(std::memory_order_acquire);
}

bool InkingScene::IsPreloaded() const noexcept {
    return GetPreloadState() == PreloadState::Done;
}

bool InkingScene::IsPreloadFailed() const noexcept {
    return GetPreloadState() == PreloadState::Failed;
}

bool InkingScene::IsPreloadFinished() const noexcept {
    const PreloadState state = GetPreloadState();
    return state == PreloadState::Done || state == PreloadState::Failed;
}

bool InkingScene::RequestPreload() {
    // 已关闭的场景不该再加载东西：它的登记已经摘了，没人会渲染它。
    if (_closed) {
        return false;
    }

    // **受理闸门**：只有 `Idle` 能推进到 `Queued`。重复调用在这里被挡掉——
    // 不另加标志位，CAS 本身就是那个标志位（要的"幂等"就落在这一行）。
    PreloadState expected = PreloadState::Idle;
    if (!_preloadState.compare_exchange_strong(expected,
                                               PreloadState::Queued,
                                               std::memory_order_acq_rel)) {
        return false;
    }

    // ⚠️ 任务里捕的是**裸 this**，所以场景在被销毁之前必须等这次预加载落地。
    // `SceneLibrary::DestroyScene` 带着这条守卫（拒绝销毁 `Queued` 的场景）。
    // 这不是疏忽，是这套设计的前提——`TaskQueue` 没有取消机制。
    TaskQueue::instance().addTask(Task<std::any>{
        [this](const std::vector<std::any>&) -> std::any {
            const bool ok = onPreload();
            // release：把"上面写下的数据"与"状态变成结果"绑成一次发布。
            _preloadState.store(ok ? PreloadState::Done : PreloadState::Failed,
                                std::memory_order_release);
            return std::any{};
        }});
    return true;
}

bool InkingScene::PreloadNow() {
    if (_closed) {
        return false;
    }

    PreloadState expected = PreloadState::Idle;
    if (!_preloadState.compare_exchange_strong(expected,
                                               PreloadState::Queued,
                                               std::memory_order_acq_rel)) {
        return false;  // 已经受理过 / 已经跑过
    }

    // 与异步那条**同一条路**，只是不进线程：就地跑完、就地把 UI 侧收尾做掉。
    // 于是 `Show()` 返回时场景已经是就绪的，不会先画一帧空的。
    const bool ok = onPreload();
    _preloadState.store(ok ? PreloadState::Done : PreloadState::Failed,
                        std::memory_order_release);
    ConsumePreloadFinish();
    return true;
}

bool InkingScene::ConsumePreloadFinish() {
    // 只被 UI 线程调，所以这个守卫不用原子（见头文件里那段说明）。
    if (_preloadFinishedNotified) {
        return false;
    }

    const PreloadState state = _preloadState.load(std::memory_order_acquire);
    if (state != PreloadState::Done && state != PreloadState::Failed) {
        return false;  // Idle / Queued：还没有结果
    }

    // 先立守卫再调用户代码：万一 `onPreloadFinished` 里又碰了一次预加载，
    // 也不会把同一个收尾跑两遍。
    _preloadFinishedNotified = true;
    onPreloadFinished(state == PreloadState::Done);
    return true;
}

// ---------------------------------------------------------------------------
// 只给 SceneLibrary 调
// ---------------------------------------------------------------------------

void InkingScene::MarkClosed() noexcept {
    _closed = true;
    _active = false;

    // 登记已经由库那边摘掉了，把令牌丢掉：留着它的话，场景析构时会再去
    // 注销一次——虽然库那边有"指针不匹配就不动"的保护，但让状态说实话更好。
    _sceneRegisterToken.reset();
}

void InkingScene::SetActiveFromLibrary(bool active) noexcept {
    _active = active;
}

// ---------------------------------------------------------------------------
// 绘制列表的维护
// ---------------------------------------------------------------------------

AnchorData InkingScene::sceneRootAnchorData() {
    // 场景不做布局变化，但第一次渲染总要有对齐的锚点：按设计画布定型
    // （自身左上角对上窗口左上角）。
    //
    // 这里**不会**改成"由 CXXCSS/Scene/*.json 驱动"：场景不交给生成器
    // （评估结论见 CXXCSS/CXXCSS.md §7.2）。尺寸跟随**设计画布**（运行期值，
    // 来自 CXXCSS/config.json），锚点按设计画布，就是它该有的样子，
    // 没有可配的东西。
    //
    // 顺手把"设计尺寸已定型"记一笔：场景构造是第二个按它对几何的地方，
    // 从这一刻起 `SetDesignSize` 就该拒绝改动。
    AnchorData data;
    data.width  = inking::DesignWidth();
    data.height = inking::DesignHeight();
    inking::detail::NoteDesignSizeTaken();
    return data;
}

void InkingScene::NotifyStructureChanged() noexcept {
    // 只标脏，不排序：一帧里连改十个 zindex 也就重建一次（见头文件说明）。
    _contentDirty = true;
    _orderDirty = true;
    ++_structureGeneration;
}

void InkingScene::ConsumeFrameDirty() noexcept {
    // 停放 / 关闭的场景不消费：它们的脏要留着，切回来时才有意义。
    if (_rejected || _closed) {
        return;
    }

    // 节点那一半：推进安静帧计数（够数就清脏）。直接在绘制列表上走一遍——
    // 每节点一次 bool 读，几十个节点可以忽略；换"维护一张脏节点表"反而要处理
    // 去重（同一节点一帧可能标脏很多次），得不偿失。
    _repaintingNodeCount = 0;
    _hitDirtyNodeCount = 0;
    for (const DrawItem& item : _drawList) {
        InkingWidget* node = item.node;
        if (node == nullptr) {
            continue;
        }
        node->ConsumeDirtyFrame();
        // 统计的是"消费之后**仍**处于脏状态"的节点数，也就是"画面还在动"。
        if (node->IsRepaintDirty()) {
            ++_repaintingNodeCount;
        }
        if (node->IsDirty()) {
            ++_hitDirtyNodeCount;
        }
    }

    // 指针那一半：这一帧的悬停判断已经做过了（T-4 起由 query 消费）。
    _pointerDirty = false;

    // 滚轮那一半（A4）：滚轮是**一次性量**，帧末一律结清——没人收也丢掉，
    // 攒到下一帧就会变成"移动鼠标之后莫名滚了一下"。
    //
    // 顺序上它是安全的：动态组件的 `Tick`（会在里面读 `GetPointerWheelX/Y`）
    // 排在主循环的 `DispatchPointer` 之后、`ConsumeFrameDirty` 之前。
    _wheelX = 0.0f;
    _wheelY = 0.0f;
    _wheelDispatched = false;
}

int InkingScene::GetRepaintingNodeCount() const noexcept {
    return _repaintingNodeCount;
}

int InkingScene::GetHitDirtyNodeCount() const noexcept {
    return _hitDirtyNodeCount;
}

bool InkingScene::IsPointerDirty() const noexcept {
    return _pointerDirty;
}

void InkingScene::AddDrawNode(InkingWidget* node) noexcept {
    if (node == nullptr) {
        return;
    }

    // 先查重：同一个指针登记两次会让列表里出现两个同样的节点，
    // 于是同一帧画两遍，去掉一份时又只去掉一份。
    const auto it = std::find_if(_drawList.begin(), _drawList.end(),
                                 [node](const DrawItem& item) {
                                     return item.node == node;
                                 });
    if (it != _drawList.end()) {
        return;
    }

    DrawItem item;
    item.node = node;
    item.parent = node->GetParent();
    item.absX = node->GetAbsX();  // 静态节点算一次就冻住
    item.absY = node->GetAbsY();
    _drawList.push_back(item);

    NotifyStructureChanged();
}

void InkingScene::RemoveDrawNode(InkingWidget* node) noexcept {
    if (node == nullptr) {
        return;
    }

    // 指针状态里可能正指着这个节点（悬停 / 按下）。这里必须一起清掉：
    // 节点析构之后再有人带着这个指针派发事件，就是野指针（AGENTS §6 第 18 条
    // 那条"生命周期成对处理"的同一条纪律）。
    if (_hoveredNode == node) {
        _hoveredNode = nullptr;
    }
    if (_pressedNode == node) {
        _pressedNode = nullptr;
    }
    // 键盘焦点也一样：节点析构 / 被摘掉时绝不能留着这个指针
    // （AGENTS §6 第 18 条那条"生命周期成对处理"）。这里**只清指针**、
    // 不去调它的 `MarkKeyboardFocus(false)`——对象可能正在析构中途。
    if (_keyboardFocus == node) {
        _keyboardFocus = nullptr;
    }

    // 只比指针、不解引用：调用方（节点析构）可能已经在销毁中途了。
    const auto it = std::remove_if(_drawList.begin(), _drawList.end(),
                                   [node](const DrawItem& item) {
                                       return item.node == node;
                                   });
    if (it == _drawList.end()) {
        return;  // 没在列表里，不用标脏
    }
    _drawList.erase(it, _drawList.end());
    NotifyStructureChanged();
}

void InkingScene::DetachSceneFromNodes() noexcept {
    // 只把节点的"我属于这个场景"摘掉，不动 _drawList：列表马上随场景一起销毁，
    // 而且里面的裸指针有可能是正在析构的兄弟节点，只能比指针、不能解引用。
    for (const DrawItem& item : _drawList) {
        if (item.node != nullptr && item.node->GetScene() == this) {
            item.node->DetachFromScene();
        }
    }
    _drawList.clear();

    // 键盘焦点这一侧也断开：节点自己那份 `_hasKeyboardFocus` 随它们各自析构时
    // 一起走，这里只要保证**场景**不再指着一个可能已经没了的对象。
    _keyboardFocus = nullptr;
}

const std::vector<InkingScene::DrawItem>& InkingScene::BuildDrawList() noexcept {
    sortDrawList();
    return _drawList;
}

std::size_t InkingScene::GetDrawItemCount() noexcept {
    sortDrawList();
    return _drawList.size();
}

void InkingScene::sortDrawList() {
    if (!_contentDirty && !_orderDirty) {
        return;
    }

    if (_contentDirty) {
        // 内容重建只剔除两类**真的不该在列表里**的条目：
        // (1) 已经不属于本场景的（换父级跨了场景）；
        // (2) 空指针。
        //
        // **不按可见性剔除**：可见性是可逆的，而"可见"这条信息只有节点自己
        // 知道。列表一旦把隐藏的节点丢掉，就再也没人把它加回来——构造函数
        // 只跑一次，于是"隐藏过的节点永远回不来"。可见性留给提交时判断
        // （见 Render）：那里 IsVisibleInTree 会顺带把整棵隐藏子树短路掉。
        const auto stale = std::remove_if(
            _drawList.begin(), _drawList.end(), [this](const DrawItem& item) {
                return item.node == nullptr || item.node->GetScene() != this;
            });
        _drawList.erase(stale, _drawList.end());

        // 重算绝对坐标：列表里的快照可能是构造那一刻的，而父级之后挪过地方。
        // 动态节点每帧提交时现算，这里的值只当基准。
        for (DrawItem& item : _drawList) {
            if (item.node != nullptr) {
                item.parent = item.node->GetParent();
                item.absX = item.node->GetAbsX();
                item.absY = item.node->GetAbsY();
            }
        }
    }

    // 稳定排序，用 `IsAbove` 的**反序**：这个比较器回答"谁在上面"，而提交
    // 要的是"谁先画"。反过来的结果是**树序（父先于子）+ 兄弟内 z 从小到大**——
    // 容器的底色总在它的子孙之前画出去，所以再也不会盖住自己的内容。
    //
    // 必须复用同一个比较器（连同"同 z 看注册序号"的尾规则），否则会出现
    // "画的是 A、点到的是 B"。
    std::stable_sort(_drawList.begin(), _drawList.end(),
                     [](const DrawItem& a, const DrawItem& b) {
                         return InkingWidget::IsAbove(*b.node, *a.node);
                     });

    _contentDirty = false;
    _orderDirty = false;

    // 表与列表**同生同灭**：内容或顺序一变，候选项的 z 序与包络就都过期了。
    // 时机也正合设计——这里是"帧首重建一次"的落点，而 Render / BuildDrawList /
    // QueryHit 都会先推一次 sortDrawList，所以谁先来拿到的都是最新的表。
    //
    // **变换不在这里**：组件上报的是"所有可能变换"的保守包络（它不动），
    // 精判用当前变换。所以动画期间表一次都不用重建（§11「变换通道不改表」）。
    rebuildHitTable();
}

// ---------------------------------------------------------------------------
// 指针输入（今天的最小闭环）
// ---------------------------------------------------------------------------

void InkingScene::SetPointerState(float designX, float designY, bool down,
                                  bool inside) noexcept {
    // 只有**真的变了**才算指针脏：窗口层每帧都喂一次（哪怕鼠标没动），
    // 不判一下的话"每帧都脏"就等于这个标记永远是摆设。
    const bool moved = (designX != _pointerX) || (designY != _pointerY)
        || (down != _pointerDown) || (inside != _pointerInside);

    _pointerX = designX;
    _pointerY = designY;
    _pointerDown = down;
    _pointerInside = inside;

    if (moved) {
        _pointerDirty = true;
    }
}

InkingWidget* InkingScene::GetPressedNode() const noexcept {
    return _pressedNode;
}

InkingWidget* InkingScene::GetDraggingNode() const noexcept {
    return _pressedNode;
}

void InkingScene::SetPointerWheel(float deltaX, float deltaY) noexcept {
    // 累加而不是覆盖：一帧里的多个滚轮事件是"这一帧一共滚了多少"，
    // 后一个盖掉前一个就是"滚得越快、滚得越少"。
    _wheelX += deltaX;
    _wheelY += deltaY;
}

float InkingScene::GetPointerWheelX() const noexcept {
    return _wheelX;
}

float InkingScene::GetPointerWheelY() const noexcept {
    return _wheelY;
}

float InkingScene::GetPointerX() const noexcept {
    return _pointerX;
}

float InkingScene::GetPointerY() const noexcept {
    return _pointerY;
}

bool InkingScene::IsPointerDown() const noexcept {
    return _pointerDown;
}

bool InkingScene::IsPointerInside() const noexcept {
    return _pointerInside;
}

InkingWidget* InkingScene::GetHoveredNode() const noexcept {
    return _hoveredNode;
}

// ---------------------------------------------------------------------------
// 键盘（A3）：焦点归属 + 事件派发
// ---------------------------------------------------------------------------

void InkingScene::SetKeyboardInput(const KeyboardInput& input) noexcept {
    // 拷一份状态与事件：场景要能在"组件问状态"时随问随答，而 `KeyboardInput`
    // 属于窗口层、下一帧就会被覆写。拷贝的是小 vector（按下的键通常个位数），
    // 每帧一次可以忽略。
    _keyboardState = input.GetState();
    _keyboardEvents = input.GetFrameEvents();
}

const KeyboardState& InkingScene::GetKeyboardState() const noexcept {
    return _keyboardState;
}

InkingWidget* InkingScene::GetKeyboardFocus() const noexcept {
    return _keyboardFocus;
}

void InkingScene::ClearKeyboardFocus() noexcept {
    SetKeyboardFocus(nullptr);
}

void InkingScene::SetKeyboardFocus(InkingWidget* node) noexcept {
    if (_keyboardFocus == node) {
        return;  // 幂等：同一个节点重复设置什么都不做
    }

    // 顺序：先把"你没有焦点了"告诉旧的，再交给新的。中间那一刻**没有人有焦点**，
    // 所以不存在"两个节点同时认为自己有焦点"的窗口。
    if (_keyboardFocus != nullptr) {
        _keyboardFocus->MarkKeyboardFocus(false);
    }
    _keyboardFocus = node;

    if (_keyboardFocus == nullptr) {
        return;
    }
    _keyboardFocus->MarkKeyboardFocus(true);
    // 新焦点立刻知道"现在在不在打字"：否则它要等下一次变化才知道，而
    // "点进输入框时其实已经有键按着"是真实存在的情形（按住键再点）。
    _keyboardFocus->PushTypingChanged(_keyboardState.IsTyping());
    _keyboardTyping = _keyboardState.IsTyping();
}

void InkingScene::DispatchKeyboard() {
    // 停放 / 关闭的场景什么都不派发（和 DispatchPointer 一致）。
    if (_rejected || _closed) {
        return;
    }

    // ---- 焦点规则 3：看不见了就释放 ----
    // 放在派发**之前**：这一帧它就不该再收到东西（隐藏的输入框不该还在收键盘）。
    if (_keyboardFocus != nullptr && !_keyboardFocus->IsVisibleInTree()) {
        SetKeyboardFocus(nullptr);
    }

    // ---- "在不在打字"的变化先报 ----
    // 顺序：组件先知道自己进了输入态，再收到第一个按键。反过来的话，
    // "按键到了、但我还不知道自己在打字"会让状态驱动的逻辑慢一拍。
    const bool typing = _keyboardState.IsTyping();
    if (typing != _keyboardTyping) {
        if (_keyboardFocus != nullptr) {
            _keyboardFocus->PushTypingChanged(typing);
        }
        // 没有焦点时也要记账：否则把焦点给出去的那一刻会被误判成"刚进入输入态"。
        _keyboardTyping = typing;
    }

    if (_keyboardFocus == nullptr) {
        return;
    }

    // 事件按到达顺序推。这一趟里组件**可能把焦点转走**（例如"Tab 键切到下一个
    // 控件"）——那没关系：本帧剩下的按键仍按原定焦点派发，下一帧的 `topmost`
    // 与焦点会在 `DispatchPointer` 之后重新算。这种情况很罕见（一帧里多个按键
    // 事件），不值得为它把事件缓存起来重放。
    for (const KeyInputEvent& event : _keyboardEvents) {
        switch (event.kind) {
            case KeyInputEvent::Kind::Down:
                _keyboardFocus->PushKeyDown(event.key);
                break;
            case KeyInputEvent::Kind::Up:
                _keyboardFocus->PushKeyUp(event.key);
                break;
            case KeyInputEvent::Kind::Text:
                _keyboardFocus->PushTextInput(event.text);
                break;
        }
    }
}

InkingWidget* InkingScene::hitTestTopmost(float designX,
                                          float designY) noexcept {
    // 从列表**尾部**往前问：列表按 z 升序排（先画的在底下），所以尾部是最上面。
    // 第一个命中的就是答案——这条顺序规则和渲染的提交顺序同源，
    // 不会出现"画的是 A、点到的是 B"。
    //
    // **这是参考实现**：正式路径走烘焙表（`QueryHit`）。留它是因为自检要拿
    // "逐点对账"钉住两者同答案——一条规则两种实现，只有在它们互相印证时
    // 才敢说"换实现没换语义"。
    for (std::size_t i = _drawList.size(); i > 0; --i) {
        InkingWidget* node = _drawList[i - 1].node;
        if (node == nullptr) {
            continue;
        }
        // 不可见的节点不参与命中：隐藏的组件不该还能被点到。父级隐藏时
        // IsVisibleInTree 会顺带把整棵子树短路掉。
        if (!node->IsVisibleInTree()) {
            continue;
        }
        if (node->HitTest(designX, designY)) {
            return node;
        }
    }
    return nullptr;
}

void InkingScene::rebuildHitTable() {
    _hitTable.Clear();

    for (const DrawItem& item : _drawList) {
        InkingWidget* node = item.node;
        if (node == nullptr) {
            continue;
        }

        // 隐藏的组件：**既不进表、也不留洞**——渲染那边也是提交时跳过它，
        // 两边必须同一个答案（"隐藏的还能点到"是最容易出的那种不一致）。
        if (!node->IsVisibleInTree()) {
            continue;
        }

        // T-7：**容器的子孙不进表**——它们的绝对包络会随容器变换过期（那正是
        // "要重烘"的来源）。改由容器自己登记一条盖住整棵子树的包络，查询落到
        // 容器上之后由它的 `HitTest` 递归解答（InputDesign.md §9 的"子树自判"）。
        {
            bool insideContainer = false;
            for (const InkingWidget* up = node->GetParent(); up != nullptr;
                 up = up->GetParent()) {
                if (up->OwnsSubtreeHit()) {
                    insideContainer = true;
                    break;
                }
            }
            if (insideContainer) {
                continue;
            }
        }

        // 包络是组件按"自己的形状 + 所有可能的变换"上报的（本地坐标），
        // 这里补上绝对位置。静态节点用列表里的快照，动态节点现算
        // （它每帧都在动，快照已经过期）。
        ShapeBounds envelope = node->GetHitEnvelope();
        if (node->IsDynamic()) {
            envelope.x += node->GetAbsX();
            envelope.y += node->GetAbsY();
        } else {
            envelope.x += item.absX;
            envelope.y += item.absY;
        }

        if (node->IsDynamic()) {
            // 动态组件**不进表**（它自己判命中、自己维护状态，§7），
            // 但它占的地方要记成"洞"：点不在任何洞里，就不必再去问动态层
            // （§10.5 规则 3）。洞只是性能开关——正确性由 QueryHit 里的
            // z 序合并保证，所以洞少登一个也只是多问一次，不会点错。
            _hitTable.AddHole(envelope);
            continue;
        }

        _hitTable.AddEntry(node, envelope, node->IsHitPassThrough());
    }

    _hitTable.Build();
}

InkingWidget* InkingScene::topmostDynamicHit(float designX, float designY,
                                             HitKind& kind) noexcept {
    // 从列表尾部往前（z 由高到低），和渲染顺序同源。动态组件通常只有几个，
    // 而且只有"点落在洞里"时才会走到这里。
    InkingWidget* passTarget = nullptr;
    for (std::size_t i = _drawList.size(); i > 0; --i) {
        InkingWidget* node = _drawList[i - 1].node;
        if (node == nullptr || !node->IsDynamic()) {
            continue;
        }
        if (!node->IsVisibleInTree()) {
            continue;
        }
        if (!node->HitTest(designX, designY)) {
            continue;
        }
        if (node->IsHitPassThrough()) {
            // 让过：记下最上面那个，继续往下找 Block（和静态层同一套三态规则）。
            if (passTarget == nullptr) {
                passTarget = node;
            }
            continue;
        }
        kind = HitKind::Block;
        return node;
    }

    if (passTarget != nullptr) {
        kind = HitKind::PassThrough;
        return passTarget;
    }
    kind = HitKind::Miss;
    return nullptr;
}

HitResult InkingScene::QueryHit(float designX, float designY) noexcept {
    // 用之前保证表是新的。表跟着绘制列表一起重建（`sortDrawList` 里），
    // 所以这里只推一次列表的脏。
    sortDrawList();

    bool inHole = false;
    HitResult staticResult = _hitTable.Query(designX, designY, &inHole);

    // **深挖一层**（A8a）：容器（`OwnsSubtreeHit()`）的子孙**不进命中表**，
    // 所以表只能答"容器"。把点再交给容器，让它说"我子树里最上面那个该收事件的
    // 是谁"——容器里的按钮 / 输入框靠这一步才收得到 hover / 点击 / 滚轮 / 拖动，
    // 也才拿得到键盘焦点（焦点是"点击获得"的，见上面的 `SetKeyboardFocus`）。
    //
    // 只在"表答的是 Block、且它是个容器"时替换，而且只在容器真的指出一个
    // **Block 子孙**时才换：容器返回 nullptr（子树里只有标签那类 PassThrough 命中）
    // 就保留原答案。**全局三态没有变**——容器在表里仍然只是一条 Block 条目，
    // "容器里的东西盖住容器外的兄弟"是局部层级（A8b，暂缓）。
    if (staticResult.kind == HitKind::Block && staticResult.target != nullptr
        && staticResult.target->OwnsSubtreeHit()) {
        if (InkingWidget* deep
            = staticResult.target->HitTestNode(designX, designY)) {
            staticResult.target = deep;
        }
    }

    // 点不在任何动态洞里 → 静态表的答案就是最终答案，动态层一次都不用问。
    // 这就是"洞"的全部价值（§13：`isDirty == false` 时每帧 0 次 query 的加强版——
    // 哪怕在查，也常常是一次表查询就结束）。
    if (!inHole) {
        return staticResult;
    }

    HitKind dynamicKind = HitKind::Miss;
    InkingWidget* dynamicHit = topmostDynamicHit(designX, designY, dynamicKind);
    if (dynamicHit == nullptr) {
        return staticResult;
    }

    // 静态与动态**按同一套层级规则合并**（§6）。动态组件压在静态组件上面时，
    // 静态表仍会算出它自己那一份答案，所以这一步不能省。
    if (staticResult.target == nullptr
        || InkingWidget::IsAbove(*dynamicHit, *staticResult.target)) {
        return HitResult{dynamicKind, dynamicHit};
    }
    return staticResult;
}

std::size_t InkingScene::GetHitTableEntryCount() noexcept {
    // 和 BuildDrawList 一样先推一次脏：表的生命周期挂在列表上，
    // 不推的话"刚建的节点还没进表"——调用方看到 0 会以为表坏了。
    sortDrawList();
    return _hitTable.GetEntryCount();
}

std::size_t InkingScene::GetHitTableClusterCount() noexcept {
    sortDrawList();
    return _hitTable.GetClusterCount();
}

std::size_t InkingScene::GetHitTableCellCount() noexcept {
    sortDrawList();
    return _hitTable.GetCellCount();
}

std::size_t InkingScene::GetHitTableMaxCellCandidates() noexcept {
    sortDrawList();
    return _hitTable.GetMaxCellCandidates();
}

void InkingScene::DispatchPointer() {
    // 停放 / 关闭的场景什么都不派发：它们连渲染都不参与。
    if (_rejected || _closed) {
        return;
    }

    // 场景的骨架可能刚变过（增删、换 z），命中要按最新的顺序问。
    sortDrawList();

    // 通用指针派发（A1 补齐）：**不再只认 Button**。
    //
    // 改之前这里把任何非动态节点 `static_cast<InkingStaticButton*>` 再调
    // `MouseHover` —— 第一个不进 Button 家族的**可命中组件**（Panel）一被指针
    // 扫到就是未定义行为，而"场景的指针派发只认 Button"这条边界也意味着
    // 新组件永远收不到输入。现在走基类的钩子：组件重写 `onPointerXxx` 就能收，
    // 没重写的拿默认空实现。**场景层从此不认识任何具体组件**
    // （本文件上面那个 `#include <button/InkingStaticButton.h>` 也一起删掉了）。
    //
    // 动态组件仍然跳过：它们的输入是自己在 `Tick` 里**拉**的
    // （`docs/InputDesign.md` §7），场景推过去反而会和它自己那份判定打架。
    const auto dispatchTarget = [](InkingWidget* node) -> InkingWidget* {
        if (node == nullptr || node->IsDynamic()) {
            return nullptr;
        }
        return node;
    };

    // ---- 1. 命中：谁是"指针下最上面那个" ----
    // 命中走**烘焙好的表**（T-4）：静态层查表，动态层自判，
    // 两者按同一套层级规则合并（`QueryHit` 里做的事）。
    // 这一步的**语义与之前完全一样**——换的是实现，不是规则。
    const HitResult hit =
        _pointerInside ? QueryHit(_pointerX, _pointerY) : HitResult{};
    InkingWidget* topmost = hit.IsHit() ? hit.target : nullptr;

    // ---- 2. 按下 / 抬起 ----
    //
    // 顺序（A4 起）：**先更新捕获，再据此算悬停**。倒过来的话，"抬起那一帧"
    // 指针下面的组件要等下一帧才亮起来——指针明明已经指着它了。
    const bool justPressed = _pointerDown && !_pointerWasDown;
    const bool justReleased = !_pointerDown && _pointerWasDown;
    _pointerWasDown = _pointerDown;

    // 指针离开窗口（移出去 / 窗口失焦）→ **这次捕获作废**。
    //
    // 和键盘那边"按着键切走、抬起事件永远不来"是同一类问题（§6 第 55 条）：
    // 抬起事件不会再来，不清掉的话拖动会永远停在"我还在拖"，而且那个节点
    // 再也不收 hover、状态也回不到 Normal——不报错，只是行为诡异。
    const bool captureAborted = (_pressedNode != nullptr) && !_pointerInside;

    if (justPressed) {
        // 指针在哪按下，谁就抓住这次交互（之后移出去也算它按着）。
        _pressedNode = topmost;

        // 拖动增量从**按下点**起算：不在这里重置的话，按下后的第一帧会把
        // "上一个位置到按下点"的位移当成拖动增量，滑块会先跳一下。
        _lastDragX = _pointerX;
        _lastDragY = _pointerY;

        // 键盘焦点规则 1 / 2：**点击获得、点别处就换**（见 SetKeyboardFocus）。
        // 只有"想要焦点"的节点拿得到；不想要（或点了空白）就是清空——于是
        // "点空白 = 失焦"是同一条规则的自然结果，不需要额外分支。
        SetKeyboardFocus(topmost != nullptr && topmost->WantsKeyboardFocus()
                             ? topmost
                             : nullptr);

        if (InkingWidget* pressed = dispatchTarget(_pressedNode)) {
            pressed->PushPointerPress(true);
        }
    }

    if (justReleased || captureAborted) {
        InkingWidget* pressed = dispatchTarget(_pressedNode);
        // 只有"抬起时指针还在它身上"才算点击：按下去再滑开松开是取消，
        // 这是用户唯一能表达"我反悔了"的方式。捕获作废（指针出了窗口）
        // 同理不算点击。
        const bool stillOnIt = (justReleased) && (_pressedNode != nullptr)
                            && (_pressedNode == topmost) && _pointerInside;
        _pressedNode = nullptr;
        if (pressed != nullptr) {
            pressed->PushPointerPress(false);
            if (stillOnIt) {
                pressed->PushPointerClick();
            }
        }
    }

    // ---- 2.5 存活校准：回调里可能把**这次命中的节点**析构掉 ----
    //
    // 这是一条实测踩过的野指针（`AGENTS.md` §6 第 103 条）：点击回调里重建容器
    // （例如"点目录行 → 重新枚举 → 列表整体重建"）会把这次命中的那个节点析构掉，
    // 而 `_pressedNode` / `_hoveredNode` 由 `RemoveDrawNode` 清成了 nullptr
    // （那条纪律只管它俩，`AGENTS.md` §6 第 18 条），**`topmost` 没有任何人清**
    // ——它还是本帧开头查出来的那个答案。于是下面"让新进来的进入 hover"
    // 就会给一块已经析构的内存推事件：表现是"点一下就炸"，且 debug / release
    // 时好时坏（UB 不一定当场崩）。
    //
    // 判据只能是"它还在绘制列表里吗"——**只比指针、不解引用**。
    if (topmost != nullptr) {
        bool alive = false;
        for (const DrawItem& item : _drawList) {
            if (item.node == topmost) {
                alive = true;
                break;
            }
        }
        if (!alive) {
            topmost = nullptr;
        }
    }

    // ---- 3. 悬停（A4：拖动独占） ----
    //
    // **拖动独占**——按着某个节点时，指针扫过的别的节点不进入 hover。
    //
    // 为什么：拖动期间指针会从捕获者身上划过去、划过别的组件；那些组件如果
    // 照样亮起，画面就在说"你正在指着它"，而实际上这次交互从头到尾只属于
    // 捕获者。这条和"拖动只推给捕获者"是同一个归属规则的两半。
    // 捕获者自己不受影响：指针还在它身上就保持 hover，滑出去就收 hover(false)
    // （按钮"按着滑出去要回 Normal"那条手感的来源）。
    InkingWidget* hoverTarget = topmost;
    if (_pressedNode != nullptr && topmost != _pressedNode) {
        hoverTarget = nullptr;
    }

    // 先让"不再被悬停的"退出。要处理两个：
    //   - 上一帧悬停、这一帧不在它身上了；
    //   - 按着的那个（拖拽时指针滑出按钮，它得知道自己在外面了，
    //     抬起时才能回到 Normal 而不是 Hover）。
    if (_hoveredNode != nullptr && _hoveredNode != hoverTarget) {
        if (InkingWidget* left = dispatchTarget(_hoveredNode)) {
            left->PushPointerHover(false);
        }
    }
    if (_pressedNode != nullptr && _pressedNode != hoverTarget
        && _pressedNode != _hoveredNode) {
        if (InkingWidget* pressed = dispatchTarget(_pressedNode)) {
            pressed->PushPointerHover(false);
        }
    }

    // 再让新进来的进入。钩子的实现是幂等的，重复同一状态什么都不做。
    if (hoverTarget != nullptr && _hoveredNode != hoverTarget) {
        if (InkingWidget* entered = dispatchTarget(hoverTarget)) {
            entered->PushPointerHover(true);
        }
    }
    _hoveredNode = hoverTarget;

    // ---- 4. 拖动（A4）：只推给捕获者 ----
    //
    // 归属唯一：谁被按下谁收，指针移到别的组件上也不改归属（跨组件不被抢）。
    // 增量相对**上一次拖动事件**，所以组件直接加到自己身上即可。指针不在
    // 窗口里时既不发拖动，也不累积基准（否则"移出去再移回来"会凭空多出一段
    // 位移）。没捕获者时这一块一次都不进——普通移动不产生任何事件。
    if (_pressedNode != nullptr && _pointerInside) {
        const float dragDx = _pointerX - _lastDragX;
        const float dragDy = _pointerY - _lastDragY;
        if (dragDx != 0.0f || dragDy != 0.0f) {
            if (InkingWidget* dragging = dispatchTarget(_pressedNode)) {
                dragging->PushPointerDrag(_pointerX, _pointerY, dragDx, dragDy);
            }
        }
    }
    _lastDragX = _pointerX;
    _lastDragY = _pointerY;

    // ---- 5. 滚轮（A4）：归属唯一 ----
    //
    // 目标：拖动捕获生效时给捕获者，否则给**指针下最上面那个**（和点击同一个
    // 命中答案）。滚轮不做三态合并——它是"一次性量"，只问一次"谁是目标"，
    // 不让过（PassThrough）在滚轮上没有意义（那是遮挡策略，不是滚动策略）。
    //
    // 派发之后**不清零**：动态组件要在自己的 `Tick` 里读它
    // （`GetPointerWheelX/Y`，§7 的"自己拉"那条路）；清账在帧末的统一消费里。
    // `_wheelDispatched` 挡住同一帧的重复派发——"每帧只判断一次"在这里也要成立。
    if (!_wheelDispatched && (_wheelX != 0.0f || _wheelY != 0.0f)) {
        _wheelDispatched = true;
        if (_pointerInside) {
            InkingWidget* wheelTarget =
                (_pressedNode != nullptr) ? _pressedNode : topmost;
            if (InkingWidget* target = dispatchTarget(wheelTarget)) {
                // B12：滚动容器**优先**。指针底下常常是容器里的子按钮，而滚轮该滚的
                // 是"外面那个能滚的东西"——沿父链找最近的声明者（用户裁定的手感）。
                //
                // 注意必须**在 `dispatchTarget` 之后**做：那道守卫负责把"不可见 /
                // 不可收输入"的节点挡掉（以及 `nullptr`），插在它前面就是拿空指针
                // 解引用——debug 下侥幸能跑，release 下当场段错误（踩过）。
                if (InkingWidget* scrollOwner =
                        target->FindSubtreeWheelOwner()) {
                    scrollOwner->PushPointerWheel(_wheelX, _wheelY);
                } else {
                    target->PushPointerWheel(_wheelX, _wheelY);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 渲染
// ---------------------------------------------------------------------------

void InkingScene::onRenderScene(SDL_Renderer* /*renderer*/) {
    // 基类不画：场景是容器不是可见组件。派生场景要画自己的背景时重写这里。
}

void InkingScene::Render(SDL_Renderer* renderer) {
    if (renderer == nullptr) {
        return;
    }

    // 顺序重建推迟到这一步，之后本帧就是最新的。
    sortDrawList();

    // T-6 的提交层：本帧的几何先缓冲、连续同类合成一次提交；纹理绘制与帧末
    // 会把缓冲刷出去。顺序完全按绘制列表走——**只合并连续同类，绝不重排**。
    detail::BeginFrame(renderer);

    // A5：裁剪状态跟着遍历走。切换裁剪（设置 / 清除）时提交层会先刷缓冲，
    // 所以"缓冲里的顶点永远在它自己那一份裁剪下画出去"这条纪律由提交层保证。
    bool clipActive = false;
    ShapeBounds activeClip{};

    for (const DrawItem& item : _drawList) {
        InkingWidget* node = item.node;
        if (node == nullptr) {
            continue;
        }

        // 可见性在提交时才判断：隐藏的节点留在列表里（这样"再显示出来"
        // 不需要谁把它加回去），但不提交给渲染器。
        // 不可见的父级会把自己和整棵子树一起短路掉。
        if (!node->IsVisibleInTree()) {
            continue;
        }

        // 绝对坐标：静态组件几何构造即定型，用建表时算好的快照；
        // 动态组件每帧都在动，必须现算，否则会"画在这儿、点在别处"。
        float absX = item.absX;
        float absY = item.absY;
        if (node->IsDynamic()) {
            absX = node->GetAbsX();
            absY = node->GetAbsY();
        }

        // 祖先裁剪（A5）：容器声明的裁剪区只剪**子孙**，所以这里问的是
        // "这个节点的祖先们剪不剪它"。没有裁剪祖先时一次都不设置，
        // 于是常规场景的提交路径与以前完全一样。
        ShapeBounds wantedClip{};
        const bool wantClip = detail::AncestorClipInDevice(*node, wantedClip);
        const bool clipChanged =
            wantClip != clipActive
            || (wantClip
                && (wantedClip.x != activeClip.x
                    || wantedClip.y != activeClip.y
                    || wantedClip.width != activeClip.width
                    || wantedClip.height != activeClip.height));
        if (clipChanged) {
            if (wantClip) {
                detail::SetClipRect(renderer, wantedClip);
            } else {
                detail::ClearClipRect(renderer);
            }
            clipActive = wantClip;
            activeClip = wantedClip;
        }

        node->SubmitToRenderer(renderer, absX, absY);
    }

    // 帧末把裁剪恢复：下一个渲染目标（或下一帧）不该继承别人的裁剪区。
    if (clipActive) {
        detail::ClearClipRect(renderer);
    }

    // 场景自己那一笔（如果有）也在这一帧的批次里；刷完再收尾。
    onRenderScene(renderer);
    detail::EndFrame();
}

}  // namespace inking
