#include <scene/SceneLibrary.h>

#include <detail/InkLog.h>

#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>

#include <scene/InkingScene.h>

namespace inking {

namespace {

/** 日志里的模块名（`InkLog` 按模块分组，别写成各处不一样的字面量）。 */
constexpr const char* kModuleName = "SceneLibrary";

/** name → 该名字占用的场景。一对一，这是"同名同期只能有一个"的落点。 */
using SceneMap = std::unordered_map<std::string, InkingScene*>;

/**
 * 唯一的登记表。
 *
 * 用函数内静态量而不是类的静态数据成员：它首次被使用时才初始化，
 * 所以一定晚于（也就是活得久于）第一个来登记的静态场景对象，
 * 不会踩静态初始化 / 析构顺序的坑（见头文件里的说明）。
 */
SceneMap& scenes() {
    static SceneMap instance;
    return instance;
}

/** 当前活跃（正在渲染）的场景；谁都不活跃时为 nullptr。 */
InkingScene*& activeScene() {
    static InkingScene* scene = nullptr;
    return scene;
}

/**
 * 被库**拥有**的场景（`AdoptScene` 进来的那一批）。
 *
 * 用 vector 而不是另一个 map：按名字找场景是 `scenes()` 的事，这里只需要
 * "我手上握着这几个对象"这一个事实。
 */
std::vector<std::unique_ptr<InkingScene>>& ownedScenes() {
    static std::vector<std::unique_ptr<InkingScene>> instance;
    return instance;
}

/**
 * 待销毁队列：已标 closed、已摘登记，等帧末真正析构。
 *
 * 单独一份、而不是直接在 `ownedScenes` 里 erase：erase 那一刻就析构了，而
 * "什么时候能安全析构"是**调用时机**的问题，不是容器的问题（见
 * `CommitPendingDestructions` 的说明）。
 */
std::vector<std::unique_ptr<InkingScene>>& pendingDestroy() {
    static std::vector<std::unique_ptr<InkingScene>> instance;
    return instance;
}

}  // namespace

// ---------------------------------------------------------------------------
// 登记 / 注销
// ---------------------------------------------------------------------------

SceneLibrary::RegisterResult SceneLibrary::RegisterScene(
    const std::string& sceneName, InkingScene* scene) {
    if (scene == nullptr || sceneName.empty()) {
        return RegisterResult::Invalid;
    }

    SceneMap& table = scenes();
    if (table.find(sceneName) != table.end()) {
        // 名字被占了就驳回：不覆盖、不排队。
        // 后来者照样构造出来了，但它没进库，所以永远不会被播放。
        return RegisterResult::NameTaken;
    }

    table.emplace(sceneName, scene);
    return RegisterResult::Ok;
}

void SceneLibrary::UnregisterScene(const std::string& sceneName,
                                   InkingScene* scene) {
    if (scene == nullptr) {
        return;
    }

    SceneMap& table = scenes();
    const auto it = table.find(sceneName);
    if (it == table.end()) {
        return;
    }

    // 只摘自己的那一份：名字下已经换成别的场景时什么都不做，
    // 否则会把别人的登记误删掉（换名字重注册那条路上就会碰到）。
    if (it->second != scene) {
        return;
    }

    table.erase(it);

    // 顺手把活跃指针也摘掉，避免它指向一个已经退出登记的场景。
    if (activeScene() == scene) {
        activeScene() = nullptr;
    }
}

InkingScene* SceneLibrary::FindScene(const std::string& sceneName) {
    const SceneMap& table = scenes();
    const auto it = table.find(sceneName);
    return it != table.end() ? it->second : nullptr;
}

bool SceneLibrary::CloseScene(const std::string& sceneName) {
    InkingScene* scene = FindScene(sceneName);
    if (scene == nullptr) {
        return false;
    }

    // 拒绝关掉**当前正在渲染**的那个：关掉它之后 `UnregisterScene` 会顺手把
    // 活跃指针清成 nullptr，于是每帧没有场景可渲染、`Tick*` 也没人收——
    // 表现是**静默黑屏，不报错**。库里没有"下一个是谁"的概念（没有场景栈、
    // 没有切换历史），所以这里不替你挑替代者。
    if (scene == activeScene()) {
        INK_LOG_WARN(kModuleName,
                     "拒绝关闭当前活跃场景：" + sceneName + "（先切走再关）");
        return false;
    }

    // 预加载"在飞"时也拒绝关：管家线程正拿着这个对象跑 `onPreload()`，而
    // "收尾"那条路只对**在库**的场景发放（`DrainPreloadCompletions` 遍历的是
    // 登记表）——先摘了登记，那次收尾就永远等不到了。规则和 `DestroyScene`
    // 保持一致：**在飞的时候既不关、也不销毁**。
    if (scene->GetPreloadState() == InkingScene::PreloadState::Queued) {
        INK_LOG_WARN(kModuleName,
                     "拒绝关闭预加载中的场景：" + sceneName + "（等它落地）");
        return false;
    }

    // 先通知场景自己"被关掉了"，再从库里摘掉：
    // 反过来的话，场景那边还得反查一次自己的名字。
    scene->MarkClosed();
    UnregisterScene(sceneName, scene);
    return true;
}

// ---------------------------------------------------------------------------
// 活跃态
// ---------------------------------------------------------------------------

void SceneLibrary::SetActiveScene(InkingScene* scene) {
    InkingScene*& current = activeScene();
    if (current == scene) {
        return;
    }

    // 旧的自动进入静默：不渲染、跟着渲染的那组逻辑也不动。
    // 数据还在、能读，所以"先把数据拿完再切场景"这条路是通的。
    if (current != nullptr) {
        current->SetActiveFromLibrary(false);
    }

    current = scene;

    if (current != nullptr) {
        current->SetActiveFromLibrary(true);
    }
}

InkingScene* SceneLibrary::GetActiveScene() {
    return activeScene();
}

bool SceneLibrary::RenderScene(SDL_Renderer* renderer) {
    InkingScene* scene = activeScene();

    // 只渲染活跃场景：停放态（注册被驳回）、静默态（被顶掉的）、
    // 已经关闭的场景都不会走到这里。每个场景一帧只渲染一次，
    // 也是靠"活跃只有一个"这条保证的。
    if (scene == nullptr || renderer == nullptr) {
        return false;
    }

    scene->Render(renderer);
    return true;
}

// ---------------------------------------------------------------------------
// 查询
// ---------------------------------------------------------------------------

std::vector<std::string> SceneLibrary::GetAllSceneNames() {
    std::vector<std::string> names;
    names.reserve(scenes().size());
    for (const auto& pair : scenes()) {
        names.push_back(pair.first);
    }
    return names;
}

std::size_t SceneLibrary::SceneCount() {
    return scenes().size();
}

bool SceneLibrary::IsNameTaken(const std::string& sceneName) {
    return scenes().find(sceneName) != scenes().end();
}

// ---------------------------------------------------------------------------
// 预加载
// ---------------------------------------------------------------------------

bool SceneLibrary::PreloadScene(const std::string& sceneName) {
    InkingScene* scene = FindScene(sceneName);
    if (scene == nullptr) {
        // 名字错 / 已关闭 / 停放态（注册被驳回的场景压根不在库里）。
        return false;
    }

    // "受理"这一步在场景那边（CAS），所以这里返回的 true 是"**已受理**"，
    // 不是"已加载完"。真实结果看 `IsPreloaded()` / `IsPreloadFinished()`。
    return scene->RequestPreload();
}

void SceneLibrary::DrainPreloadCompletions() {
    // 复制一份指针再遍历：收尾钩子是用户代码，里面可能构造新场景（构造即
    // 登记，会改 `scenes()`）——拿着 map 的迭代器边遍历边改就是悬垂。
    // `InkingScene::TickLogic` 那边是同一条理由。
    std::vector<InkingScene*> finished;
    finished.reserve(scenes().size());
    for (const auto& pair : scenes()) {
        if (pair.second != nullptr && pair.second->IsPreloadFinished()) {
            finished.push_back(pair.second);
        }
    }

    for (InkingScene* scene : finished) {
        // 收尾钩子跑在**调用它的线程**上——这里就是 UI 线程的帧首。
        scene->ConsumePreloadFinish();
    }
}

// ---------------------------------------------------------------------------
// 所有权与销毁
// ---------------------------------------------------------------------------

bool SceneLibrary::AdoptScene(std::unique_ptr<InkingScene> scene) {
    if (scene == nullptr) {
        return false;
    }

    // 幂等：同一个对象接管两次，第二次只是多一个空壳，而析构时会删两次
    // ——那是最难查的一类重复释放。
    for (const auto& item : ownedScenes()) {
        if (item.get() == scene.get()) {
            return false;
        }
    }

    ownedScenes().push_back(std::move(scene));
    return true;
}

bool SceneLibrary::DestroyScene(const std::string& sceneName) {
    InkingScene* scene = FindScene(sceneName);
    if (scene == nullptr) {
        return false;
    }

    if (scene == activeScene()) {
        INK_LOG_WARN(kModuleName,
                     "拒绝销毁当前活跃场景：" + sceneName + "（先切走再销毁）");
        return false;
    }

    // 预加载"在飞"（已受理、还没出结果）时不能销毁：管家线程正拿着这个对象
    // 跑 `onPreload()`，析构它就是让那个线程踩空。等它落地。
    const InkingScene::PreloadState preload = scene->GetPreloadState();
    if (preload == InkingScene::PreloadState::Queued) {
        INK_LOG_WARN(kModuleName,
                     "拒绝销毁预加载中的场景：" + sceneName + "（等它落地）");
        return false;
    }

    // 找到它的所有权。**没被接管的拒绝销毁**：库对它没有所有权，硬删会踩到
    // "栈上的场景""派生类的值成员子组件"这些库根本看不见的东西。
    std::vector<std::unique_ptr<InkingScene>>& owned = ownedScenes();
    for (auto it = owned.begin(); it != owned.end(); ++it) {
        if (it->get() != scene) {
            continue;
        }

        std::unique_ptr<InkingScene> holder = std::move(*it);
        owned.erase(it);

        // 标 closed（顺带摘活跃态）并把令牌丢掉，再摘登记——名字立刻空出来，
        // 可以再建一个同名的。这两步和 `CloseScene` 同一套，只是不碰
        // "拒绝活跃"那条守卫（前面已经判过了）。
        scene->MarkClosed();
        UnregisterScene(sceneName, scene);

        // 真正的析构推到帧末。此时 `holder` 只是把所有权搬个地方，
        // 不触发任何析构。
        pendingDestroy().push_back(std::move(holder));
        return true;
    }

    return false;
}

std::size_t SceneLibrary::CommitPendingDestructions() {
    std::vector<std::unique_ptr<InkingScene>>& pending = pendingDestroy();
    const std::size_t count = pending.size();

    // clear() 逐个析构。析构里那条"令牌注销"会再调一次 `UnregisterScene`——
    // 那边有"名字没了就什么都不做"的保护，所以是幂等的空操作。
    pending.clear();
    return count;
}

}  // namespace inking
