// ---------------------------------------------------------------------------
// inking_probe_drag —— 拖拽原语（1.4）的**单控件探针**
//
// 验什么：任务书 `GoldingCore/docs/TASK-SDK-DRAGGER.md` §5 的 A1~A8，一条断言
// 对一条场景。判据全是**数值**（计数 / 指针身份 / 节点数），只有最后一小段
// 用像素确认"幽灵真的画出来了"。
//
// 为什么单独成探针而不是加进 `demo/selftest.cpp`：`AGENTS.md` §4 第 2 条写着
// "新控件的细账别往 selftest 里加"——那边验的是"库作为 DLL 从消费者视角能不能用"，
// 而拖拽这一组是**一个手势的细节**，红了就该只可能是这一件事。
//
// 跑法
//   ./bin/inking_probe_drag                                   # dummy 后端
//   INKING_PROBE_DRIVER=windows ./bin/inking_probe_drag        # 真后端
// 退出码 = 失败项数；真后端建不出窗口时 77（ctest 显示 Skipped）。
// ---------------------------------------------------------------------------

#include <inking.h>

#include <button/InkingStaticButton.h>
#include <config/window_config.h>
#include <dataStruct/InkingShapeSpec.h>
#include <drag/DragSpec.h>
#include <input/KeyboardInput.h>
#include <scene/InkingScene.h>
#include <scene/SceneLibrary.h>

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

int g_fail = 0;

void Check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? " ok " : "FAIL", what);
    if (!ok) {
        ++g_fail;
    }
}

// ---------------------------------------------------------------------------
// 三块砖：源 / 落点 / 干扰落点
// ---------------------------------------------------------------------------

/// 固定的名字与几何（不用主题默认值：主题一改，像素断言就跟着红）。
constexpr float kSrcX = 80.0f;
constexpr float kSrcY = 80.0f;
constexpr int kSrcW = 120;
constexpr int kSrcH = 60;
constexpr float kDstX = 300.0f;
constexpr float kDstY = 80.0f;
constexpr int kDstW = 160;
constexpr int kDstH = 60;
constexpr float kOtherX = 300.0f;
constexpr float kOtherY = 220.0f;
constexpr int kOtherW = 160;
constexpr int kOtherH = 60;

/// 三块砖的中心（指针都往中心放，离边界远一点，2px 的抖动也还在里面）。
constexpr float kSrcCenterX = kSrcX + kSrcW * 0.5f;
constexpr float kSrcCenterY = kSrcY + kSrcH * 0.5f;
constexpr float kDstCenterX = kDstX + kDstW * 0.5f;
constexpr float kDstCenterY = kDstY + kDstH * 0.5f;
constexpr float kOtherCenterX = kOtherX + kOtherW * 0.5f;
constexpr float kOtherCenterY = kOtherY + kOtherH * 0.5f;
/// 空白处：离三块砖都远，命中表在那里答"没有"。
constexpr float kBlankX = 700.0f;
constexpr float kBlankY = 500.0f;

InkingButtonData MakeTile(const char* name, float x, float y, int width,
                          int height, std::uint32_t color) {
    InkingButtonData data;
    data.name = name;
    data.width = width;
    data.height = height;
    data.offsetX = x;
    data.offsetY = y;
    data.normal = InkingButtonAppearance(InkingAppearanceType::Color, color);
    data.hover = data.normal;
    data.onclicked = data.normal;
    return data;
}

/// 落点回调的记账本（`onDrop` 是**裸函数指针**，所以要用 `user` 带一份状态）。
struct DropLog {
    int count = 0;
    void* lastPayload = nullptr;
    float lastX = 0.0f;
    float lastY = 0.0f;
    std::string lastText;
};

void OnDropThunk(void* user, void* payload, float x, float y) {
    auto* log = static_cast<DropLog*>(user);
    ++log->count;
    log->lastPayload = payload;
    log->lastX = x;
    log->lastY = y;
    log->lastText = payload != nullptr ? static_cast<const char*>(payload) : "";
}

int AlwaysAccept(void* /*user*/, void* /*payload*/, float /*x*/, float /*y*/) {
    return 1;
}

int NeverAccept(void* /*user*/, void* /*payload*/, float /*x*/, float /*y*/) {
    return 0;
}

/// `canBegin` 的取样：拒绝一切按下的位置（验"某些位置不能起拖"）。
bool RefuseBegin(void* /*user*/, float /*x*/, float /*y*/) {
    return false;
}

struct DragScene : public InkingScene {
    DragScene() : InkingScene("probe_drag") {}
};

/// 喂一帧指针（与 `demo/selftest.cpp` 的 idiom 一致：不依赖真实鼠标）。
void Feed(InkingScene& scene, float x, float y, bool down) {
    scene.SetPointerState(x, y, down, true);
    scene.DispatchPointer();
}

void FeedEsc(InkingScene& scene) {
    InkingKeyboardInput input;
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = SDLK_ESCAPE;
    input.UpdateFromSDL(event);
    scene.SetKeyboardInput(input);
    scene.DispatchKeyboard();
}

/// 场景下叫这个名字的节点有几个（幽灵的唯一性靠它数）。
int CountByPrefix(InkingScene& scene, const char* name) {
    int count = 0;
    for (InkingWidget* child : scene.GetChildren()) {
        if (child->GetName() == name) {
            ++count;
        }
    }
    return count;
}

std::vector<std::uint32_t> ReadPixelRegion(SDL_Renderer* renderer,
                                          const SDL_Rect& rect) {
    std::vector<std::uint32_t> out;
    SDL_Surface* shot = SDL_RenderReadPixels(renderer, &rect);
    if (shot == nullptr) {
        return out;
    }
    SDL_Surface* argb = SDL_ConvertSurface(shot, SDL_PIXELFORMAT_ARGB8888);
    SDL_DestroySurface(shot);
    if (argb == nullptr || !SDL_LockSurface(argb)) {
        SDL_DestroySurface(argb);
        return out;
    }
    out.resize(static_cast<std::size_t>(rect.w) * static_cast<std::size_t>(rect.h));
    for (int row = 0; row < rect.h; ++row) {
        std::memcpy(&out[static_cast<std::size_t>(row) * rect.w],
                    static_cast<const std::uint8_t*>(argb->pixels) +
                        static_cast<std::size_t>(row) * argb->pitch,
                    sizeof(std::uint32_t) * static_cast<std::size_t>(rect.w));
    }
    SDL_UnlockSurface(argb);
    SDL_DestroySurface(argb);
    return out;
}

bool NearColor(std::uint32_t a, std::uint32_t b, int tolerance = 8) {
    for (int shift = 0; shift <= 16; shift += 8) {
        const int left = static_cast<int>((a >> shift) & 0xFFu);
        const int right = static_cast<int>((b >> shift) & 0xFFu);
        const int delta = left > right ? left - right : right - left;
        if (delta > tolerance) {
            return false;
        }
    }
    return true;
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("== 探针：控件拖拽原语（drag source / drop target / 幽灵层）==\n");

    const char* driverEnv = std::getenv("INKING_PROBE_DRIVER");
    const std::string driver =
        (driverEnv != nullptr && driverEnv[0] != '\0') ? driverEnv : "dummy";
    std::printf("   视频驱动 = %s\n", driver.c_str());

    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, driver.c_str());
    Check(SDL_Init(SDL_INIT_VIDEO), "SDL_Init");
    SDL_Window* window =
        SDL_CreateWindow("ink-probe-drag", InkingDesignWidth(),
                         InkingDesignHeight(), 0);
    SDL_Renderer* renderer =
        window != nullptr ? SDL_CreateRenderer(window, nullptr) : nullptr;

    if ((window == nullptr || renderer == nullptr) && driver != "dummy") {
        std::printf("[skip] 真后端不可用：%s\n",
                    SDL_GetError()[0] != '\0' ? SDL_GetError() : "（无错误信息）");
        if (renderer != nullptr) {
            SDL_DestroyRenderer(renderer);
        }
        if (window != nullptr) {
            SDL_DestroyWindow(window);
        }
        SDL_Quit();
        return 77;
    }

    DragScene scene;
    if (!scene.IsRegistered()) {
        // 撞名时**必须红**：不红的话后面所有断言都在跑一个停放态的场景。
        Check(false, "探针场景注册成功（名字唯一）");
        std::printf("\n失败项：%d\n", g_fail);
        return g_fail;
    }

    // ---- 建三块砖 ----
    const char* kPayloadText = "r7";  // A3 断言里要比的那份 payload

    DropLog log;

    auto* src = static_cast<InkingStaticButton*>(scene.AddChild(
        std::make_unique<InkingStaticButton>(&scene,
                                             MakeTile("drag_src", kSrcX, kSrcY,
                                                      kSrcW, kSrcH, 0xFF3070B0u))));
    auto* dst = static_cast<InkingStaticButton*>(scene.AddChild(
        std::make_unique<InkingStaticButton>(&scene,
                                             MakeTile("drag_dst", kDstX, kDstY,
                                                      kDstW, kDstH, 0xFF40A060u))));
    auto* other = static_cast<InkingStaticButton*>(scene.AddChild(
        std::make_unique<InkingStaticButton>(&scene,
                                             MakeTile("drag_other", kOtherX,
                                                      kOtherY, kOtherW, kOtherH,
                                                      0xFFA05050u))));

    // ---- 声明：src 能被拖，dst 收 "golding.resource"，other 只收别的类型 ----
    InkingDragSourceSpec sourceSpec;
    sourceSpec.payloadType = "golding.resource";
    sourceSpec.payload = const_cast<char*>(kPayloadText);
    sourceSpec.ghostText = "r7";
    src->SetDragSource(sourceSpec);

    InkingDropTargetSpec dropSpec;
    dropSpec.acceptType = "golding.resource";
    dropSpec.canDrop = AlwaysAccept;
    dropSpec.onDrop = OnDropThunk;
    dropSpec.user = &log;
    dst->SetDropTarget(dropSpec);

    InkingDropTargetSpec otherSpec;
    otherSpec.acceptType = "other.type";
    otherSpec.onDrop = OnDropThunk;
    otherSpec.user = &log;
    other->SetDropTarget(otherSpec);

    int clickCount = 0;
    src->SetOnClicked([&clickCount] { ++clickCount; });

    Check(scene.GetDrawItemCount() == 3, "三块砖都进了绘制列表（幽灵还没造）");

    // -----------------------------------------------------------------------
    // A1：按下 → 移 2px → 抬起 ⇒ **不算拖**，click 一次
    // -----------------------------------------------------------------------
    std::printf("\n-- A1 阈值：2px 还是点击 --\n");
    Feed(scene, kSrcCenterX, kSrcCenterY, true);
    Feed(scene, kSrcCenterX + 2.0f, kSrcCenterY, true);
    Check(!scene.IsDragging(), "移 2px（< 4）不算拖");
    Check(scene.GetDragGhostNode() == nullptr, "没有幽灵");
    Feed(scene, kSrcCenterX + 2.0f, kSrcCenterY, false);
    Check(clickCount == 1, "抬起在这一颗砖上 ⇒ onClicked 触发一次");
    Check(scene.GetDragGhostNode() == nullptr, "松手后仍然没有幽灵");
    Check(scene.GetDragSession().GetDropCount() == 0
              && scene.GetDragSession().GetCancelCount() == 0,
          "没过阈值 ⇒ 连'取消'都不算（它从来不是一场拖拽）");

    // -----------------------------------------------------------------------
    // A2：按下 → 移 10px ⇒ 幽灵出现、源进入被拖状态
    // -----------------------------------------------------------------------
    std::printf("\n-- A2 起拖：幽灵挂在场景下、z=850、吃不到命中 --\n");
    Feed(scene, kSrcCenterX, kSrcCenterY, true);
    Feed(scene, kSrcCenterX + 10.0f, kSrcCenterY, true);
    Check(scene.IsDragging(), "移 10px（> 4）⇒ 起拖");
    InkingWidget* ghost = scene.GetDragGhostNode();
    Check(ghost != nullptr, "幽灵节点存在");
    Check(ghost != nullptr && ghost->GetParent() == &scene,
          "幽灵**挂在场景下**（挂错父级就会被面板裁掉）");
    Check(ghost != nullptr && ghost->GetZIndex() == InkingDragGhostZIndex,
          "幽灵 z = 850（高于弹窗 400、低于光标 900）");
    Check(ghost != nullptr && !ghost->HitTest(kSrcCenterX + 10.0f, kSrcCenterY),
          "幽灵**不参与命中**（指针底下的答案必须是落点，不是跟着跑的那块板）");
    Check(scene.GetDragSourceNode() == src, "源进入被拖状态（会话记的就是它）");
    Check(CountByPrefix(scene, "$inking.drag.ghost") == 1, "场景里只有一只幽灵");

    // 移到落点上：高亮必须是 dst，而不是邻居
    Feed(scene, kDstCenterX, kDstCenterY, true);
    Check(dst->IsDragOver(), "指针在 dst 上 ⇒ IsDragOver() 为真");
    Check(!other->IsDragOver(), "别处的落点不亮");
    Check(scene.GetDragTargetNode() == dst, "会话认的落点也是 dst");
    Check(!src->IsDragOver(), "源自己不算落点（它没声明 acceptType）");

    // -----------------------------------------------------------------------
    // A3：继续移到 dst 上抬起 ⇒ onDrop 一次，参数含 payload 与落点
    // -----------------------------------------------------------------------
    std::printf("\n-- A3 落靶：onDrop 一次，幽灵消失，无 click --\n");
    const int clicksBeforeDrop = clickCount;
    Feed(scene, kDstCenterX, kDstCenterY, false);
    Check(log.count == 1, "onDrop 恰好一次");
    Check(log.lastText == kPayloadText, "payload 原样转交（\"r7\"）");
    Check(std::fabs(log.lastX - kDstCenterX) <= 0.01f
              && std::fabs(log.lastY - kDstCenterY) <= 0.01f,
          "落点坐标 = 松手处");
    Check(scene.GetDragGhostNode() == nullptr && !scene.IsDragging(),
          "幽灵消失、会话结束");
    Check(!dst->IsDragOver(), "高亮跟着一起清掉");
    Check(clickCount == clicksBeforeDrop, "拖拽这次松手**不派发 click**（规则 2）");
    Check(scene.GetDragSession().GetDropCount() == 1, "落靶计数 +1");

    // -----------------------------------------------------------------------
    // A3b：拖出去再拖回源身上松开 ⇒ 仍然不派发 click
    // -----------------------------------------------------------------------
    //
    // 这一条盯的是"拖拽期间源收不到 click"那句的**最难的一半**：松手位置上
    // 指针确实还在源身上，只按 `slot == topmost` 判就会误发一次点击。
    std::printf("\n-- A3b 独占：拖回源身上松开也不算点击 --\n");
    const int clicksBeforeBack = clickCount;
    Feed(scene, kSrcCenterX, kSrcCenterY, true);
    Feed(scene, kSrcCenterX + 20.0f, kSrcCenterY, true);
    Check(scene.IsDragging(), "（前置）已在拖");
    Feed(scene, kSrcCenterX, kSrcCenterY, true);
    Feed(scene, kSrcCenterX, kSrcCenterY, false);
    Check(clickCount == clicksBeforeBack, "松手在源身上 —— 仍然没有 click");
    Check(log.count == 1, "也没有落到任何落点上");
    Check(scene.GetDragGhostNode() == nullptr, "幽灵已消失");

    // -----------------------------------------------------------------------
    // A4：移到空白处抬起 ⇒ 不调 onDrop，幽灵消失，无 click
    // -----------------------------------------------------------------------
    std::printf("\n-- A4 落空：取消而不是落靶 --\n");
    const int dropsBeforeMiss = log.count;
    const int clicksBeforeMiss = clickCount;
    const std::uint64_t cancelsBeforeMiss = scene.GetDragSession().GetCancelCount();
    Feed(scene, kSrcCenterX, kSrcCenterY, true);
    Feed(scene, kSrcCenterX, kSrcCenterY + 12.0f, true);
    Check(scene.IsDragging(), "（前置）已在拖");
    Feed(scene, kBlankX, kBlankY, true);
    Check(scene.GetDragTargetNode() == nullptr, "空白处没有落点");
    Feed(scene, kBlankX, kBlankY, false);
    Check(log.count == dropsBeforeMiss, "落空 ⇒ onDrop 一次都不调");
    Check(scene.GetDragGhostNode() == nullptr && !scene.IsDragging(),
          "幽灵消失、会话结束");
    Check(clickCount == clicksBeforeMiss, "落空也不产生 click");
    Check(scene.GetDragSession().GetCancelCount() == cancelsBeforeMiss + 1,
          "取消计数 +1");

    // -----------------------------------------------------------------------
    // A5：拖拽中按 Esc ⇒ 幽灵消失、onDrop 不调
    // -----------------------------------------------------------------------
    std::printf("\n-- A5 Esc：模态取消 --\n");
    const int dropsBeforeEsc = log.count;
    const int clicksBeforeEsc = clickCount;
    Feed(scene, kSrcCenterX, kSrcCenterY, true);
    Feed(scene, kDstCenterX, kDstCenterY, true);
    Check(scene.IsDragging(), "（前置）拖到落点上、还没松手");
    FeedEsc(scene);
    Check(!scene.IsDragging() && scene.GetDragGhostNode() == nullptr,
          "Esc ⇒ 幽灵消失、会话结束");
    Check(log.count == dropsBeforeEsc, "Esc ⇒ onDrop 不调（哪怕指针正停在落点上）");
    Check(!dst->IsDragOver(), "高亮清空");
    Check(src->GetState() != InkingButtonState::Pressed,
          "源回到常态（取消顺手放掉了主键捕获）");
    Feed(scene, kDstCenterX, kDstCenterY, false);
    Check(clickCount == clicksBeforeEsc && log.count == dropsBeforeEsc,
          "取消之后那次松手什么都不发生（既无 click 也无 drop）");

    // -----------------------------------------------------------------------
    // A6：拖拽中把源节点摘掉 ⇒ 不崩、幽灵消失、状态清零
    // -----------------------------------------------------------------------
    std::printf("\n-- A6 源被删：会话跟着走 --\n");
    {
        Feed(scene, kSrcCenterX, kSrcCenterY, true);
        Feed(scene, kDstCenterX, kDstCenterY, true);
        Check(scene.IsDragging(), "（前置）已在拖");
        scene.RemoveChild(src);   // 不接返回值 = 当场析构
        src = nullptr;
        Check(!scene.IsDragging() && scene.GetDragGhostNode() == nullptr,
              "源被摘掉 ⇒ 会话取消、幽灵消失（而且没崩）");
        Check(!dst->IsDragOver(), "高亮也清了");
        Feed(scene, kBlankX, kBlankY, false);
        Check(!scene.IsDragging(), "松手之后仍然干净");
    }

    // -----------------------------------------------------------------------
    // A7：类型不匹配的落点 ⇒ 不高亮、不落
    // -----------------------------------------------------------------------
    std::printf("\n-- A7 类型不符：看得见也落不下去 --\n");
    auto* src2 = static_cast<InkingStaticButton*>(scene.AddChild(
        std::make_unique<InkingStaticButton>(&scene,
                                             MakeTile("drag_src2", kSrcX, kSrcY,
                                                      kSrcW, kSrcH, 0xFF3070B0u))));
    src2->SetDragSource(sourceSpec);
    src2->SetOnClicked([&clickCount] { ++clickCount; });

    const int dropsBeforeMismatch = log.count;
    Feed(scene, kSrcCenterX, kSrcCenterY, true);
    Feed(scene, kOtherCenterX, kOtherCenterY, true);
    Check(scene.IsDragging(), "（前置）已在拖");
    Check(!other->IsDragOver(), "acceptType 不符 ⇒ **不高亮**");
    Check(scene.GetDragTargetNode() == nullptr, "会话也不认它作落点");
    Feed(scene, kOtherCenterX, kOtherCenterY, false);
    Check(log.count == dropsBeforeMismatch, "松手 ⇒ onDrop 不调（落空）");
    Check(scene.GetDragGhostNode() == nullptr, "幽灵已经清掉");

    // -----------------------------------------------------------------------
    // A8：已经在拖，第二个源尝试起拖 ⇒ 被拒绝
    // -----------------------------------------------------------------------
    std::printf("\n-- A8 单实例：一次只允许一个源 --\n");
    Feed(scene, kSrcCenterX, kSrcCenterY, true);
    Feed(scene, kSrcCenterX + 10.0f, kSrcCenterY, true);
    Check(scene.IsDragging(), "（前置）src2 已在拖");
    InkingWidget* ghostBefore = scene.GetDragGhostNode();
    Check(!scene.BeginDrag(dst), "dst 没声明拖拽源 ⇒ BeginDrag 拒绝");
    Check(!scene.BeginDrag(src2), "同一个源再来一次 ⇒ 拒绝（已经在拖）");
    Check(scene.GetDragGhostNode() == ghostBefore, "幽灵还是那一只，没被替换");
    Check(CountByPrefix(scene, "$inking.drag.ghost") == 1, "场景里仍然只有一只幽灵");
    Feed(scene, kBlankX, kBlankY, false);
    Check(!scene.IsDragging(), "收拾干净");

    // -----------------------------------------------------------------------
    // 附加：canBegin 说不 ⇒ 拖不动（"只有按住手柄才能拖"那种配置）
    // -----------------------------------------------------------------------
    std::printf("\n-- 附加：canBegin 拒绝起拖 --\n");
    InkingDragSourceSpec lockedSpec = sourceSpec;
    lockedSpec.canBegin = RefuseBegin;
    src2->SetDragSource(lockedSpec);
    Feed(scene, kSrcCenterX, kSrcCenterY, true);
    Feed(scene, kSrcCenterX + 30.0f, kSrcCenterY, true);
    Check(!scene.IsDragging() && scene.GetDragGhostNode() == nullptr,
          "canBegin 返回 false ⇒ 移多远都不起拖");
    Feed(scene, kSrcCenterX + 30.0f, kSrcCenterY, false);
    src2->SetDragSource(sourceSpec);  // 还原

    // -----------------------------------------------------------------------
    // 像素：幽灵真的画出来了吗（有渲染器才跑）
    // -----------------------------------------------------------------------
    std::printf("\n-- ④ 像素：幽灵画出来了没 --\n");
    if (renderer == nullptr) {
        std::printf("      [skip] 没有渲染器——像素断言不适用\n");
        std::printf("\n失败项：%d\n", g_fail);
        return g_fail;
    }

    SDL_SetRenderLogicalPresentation(renderer, InkingDesignWidth(),
                                     InkingDesignHeight(),
                                     SDL_LOGICAL_PRESENTATION_DISABLED);
    InkingSceneLibrary::SetActiveScene(&scene);

    // 造一次拖拽，让幽灵出现在一个**没有其它组件**的位置。
    Feed(scene, kSrcCenterX, kSrcCenterY, true);
    Feed(scene, kBlankX, kBlankY, true);
    Check(scene.IsDragging(), "（前置）拖到空白处，幽灵跟着指针");
    InkingWidget* shotGhost = scene.GetDragGhostNode();
    scene.TickFrame(1.0f / 60.0f);

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    InkingSceneLibrary::RenderScene(renderer);

    const int ghostX = shotGhost != nullptr
                           ? static_cast<int>(std::lround(shotGhost->GetAbsX()))
                           : 0;
    const int ghostY = shotGhost != nullptr
                           ? static_cast<int>(std::lround(shotGhost->GetAbsY()))
                           : 0;
    const std::vector<std::uint32_t> inside =
        ReadPixelRegion(renderer, SDL_Rect{ghostX + 8, ghostY + 8, 1, 1});
    const std::vector<std::uint32_t> outside =
        ReadPixelRegion(renderer, SDL_Rect{ghostX + 8, ghostY - 40, 1, 1});
    const std::uint32_t insidePixel = inside.empty() ? 0u : inside[0];
    const std::uint32_t outsidePixel = outside.empty() ? 0u : outside[0];
    std::printf("     幽灵左上 (%d, %d)；内部 0x%08X / 上方空处 0x%08X\n", ghostX,
                ghostY, insidePixel, outsidePixel);
    Check(!NearColor(insidePixel, 0xFF000000u), "幽灵内部**不是**纯黑背景（画出来了）");
    Check(NearColor(outsidePixel, 0xFF000000u),
          "幽灵上方是清屏色（取样位置对得上，上一条才有区分力）");

    // 收尾：松手（落在空白处 ⇒ 取消），把会话清干净再退。
    Feed(scene, kBlankX, kBlankY, false);

    std::printf("\n失败项：%d\n", g_fail);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return g_fail;
}
