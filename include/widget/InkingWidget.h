#pragma once

// 实现见 src/widget/InkingWidget.cpp。

#include <dataStruct/InkingAnchorData.h>
#include <dataStruct/InkingShapeSpec.h>
#include <dataStruct/InkingTransform.h>
#include <dataStruct/InkingLayout.h>
#include <drag/DragSpec.h>
#include <input/CursorShape.h>
#include <input/InkingKeyboardTarget.h>
#include <input/InkingPointerHandler.h>
#include <anchor/InkingAnchor.h>

#include <memory>
#include <cstdint>
#include <vector>
#include <string>

struct SDL_Renderer;

//先声明两个前向类型
class InkingScene;

class InkingWidget;

namespace inking_detail {
// 顶点缓存槽
struct VertexCacheSlot;

//统一的默认渲染：一块实心矩形，颜色取节点的 color。
void FillDefaultRect(const InkingWidget& node, float pixelX, float pixelY);
}  // namespace inking_detail

namespace inking_detail {
    //TODO:之后要移除Layout，把Layout做成单独的组件
namespace layout {
/**
 * 对一个容器跑一次排布：读它的InkingLayoutSpec与每个孩子的MeasureLayout，解出矩形，逐个写回孩子，再递归进"本身也是容器"的孩子。
 * 只有它是 `InkingWidget::ApplyLayoutRect` 的 friend那条几何写入口。这样在编译期就对业务代码不可见。
 */
void ArrangeSubtree(InkingWidget& container);
}  // namespace layout
}  // namespace inking_detail

//组件节点，在场景中以树的形式存在
class InkingWidget : public InkingAnchor,
                     public InkingPointerHandler,
                     public InkingKeyboardTarget {
public:
    // 虚析构 + 禁拷贝/移动：本类要被继承、持有父指针、拥有子节点，还要登记进场景的绘制列表，复制出的第二份关系会让父子链和注册表同时指向同一个对象。
    // 析构体在 .cpp 里：它要反注册自己并释放拥有的子节点，不能写成 = default。
    ~InkingWidget() override;
    InkingWidget(const InkingWidget&) = delete;
    InkingWidget& operator=(const InkingWidget&) = delete;
    InkingWidget(InkingWidget&&) = delete;
    InkingWidget& operator=(InkingWidget&&) = delete;

    //只读查询：身份与父子
    const std::string& GetName() const noexcept;

    //父节点
    InkingWidget* GetParent() const noexcept;

    //直接子节点，本节点不负责子节点的生命周期
    const std::vector<InkingWidget*>& GetChildren() const noexcept;

    //只读：设计宽度
    int GetWidth() const noexcept override;
    //只读：设计高度
    int GetHeight() const noexcept override;

    //相对于根的绝对横轴设计坐标，统一绘制、命中、包络、裁剪四处
    float GetAbsX() const noexcept;
    //相对于根的绝对纵轴设计坐标，统一绘制、命中、包络、裁剪四处
    float GetAbsY() const noexcept;

    //尺寸会随着状态改变吗？用于确定静态组件如果存在变换（如button的三态变换）。true为会变换
    virtual bool HasLayoutAnimation() const noexcept { return false; }

    //本节点在兄弟之间的层级
    int GetZIndex() const noexcept;
    //兄弟之间层级相同时决定先后
    int GetRegisterOrder() const noexcept; 

    //本节点的占位填充色，0xAARRGGBB。颜色不标命中脏。
    std::uint32_t GetColor() const noexcept;

    //设计单位向设备像素的倍率，包括窗口缩放和系统DPI，letterbox模式下，横轴/纵轴上倍率相同
    float GetDeviceWidth() const noexcept;
    //设计单位向设备像素的倍率，包括窗口缩放和系统DPI，letterbox模式下，横轴/纵轴上倍率相同
    float GetDeviceHeight() const noexcept;

    //节点对整棵子树施加变换
    virtual InkingTransformSpec GetSubtreeTransform() const noexcept {
        return InkingTransformSpec{};
    }

    //回答这个节点是否负责整棵子树的命中，如果负责（true），那么子节点将不进入命中表
    virtual bool OwnsSubtreeHit() const noexcept { return false; }
    //这个节点是否要把子孙超出裁减区的部分裁掉？
    virtual bool HasSubtreeClip() const noexcept { return false; }
    //裁剪区：本节点布局空间里的矩形，相对本节点左上角（设计坐标）。如果决定要裁减才起效。默认是本体的整块矩形。
    virtual InkingShapeBounds GetSubtreeClipBounds() const noexcept;
    //裁减区的跟随变换
    virtual InkingTransformSpec GetClipTransform() const noexcept;

    //命中脏，上一次会影响命中的改动还没有被消费，在渲染之前消费
    bool IsDirty() const noexcept;
    //清理命中脏，通常帧末统一消费
    void ClearDirty() noexcept;
    //重绘脏，如果非脏的情况下，跳过一些不必要的重建绘制列表
    bool IsRepaintDirty() const noexcept;
    //清理重绘脏，同样帧末统一消费
    void ClearRepaintDirty() noexcept;
    //重绘脏要连续安静多少帧才算画面稳了
    static constexpr int kRepaintQuietFrames = 5;
    //命中脏要连续安静多少帧
    static constexpr int kHitQuietFrames = 3;
    //帧末统一消费里的一步：推进这个节点自己的安静帧计数，够数就清脏，避免多个组件每次标脏都重建
    void ConsumeDirtyFrame() noexcept;

    //回答自己就是某棵渲染树的根，没有场景祖先可登记。
    bool IsSceneRoot() const noexcept;

    //回答自己是否是动态组件
    bool IsDynamic() const noexcept;

    //沿父链找到最近的场景祖先；自己是场景、或不在任何场景下时返回 nullptr。
    InkingScene* GetSceneAncestor() noexcept;

    //回答我的子树中是否有静态节点，如果有那么需要全量重建
    bool HasStaticInSubtree() const noexcept;

    // ---------------- 运行期增删子节点（A10）：所有权只有一个地方 ----------------
    //
    // 组件写成成员（`{this}`）是**构造期**的事；长列表 / 动态内容要在运行期
    // "造一个、挂上去、摘下来、销毁掉"。这一节就是那条路。
    //
    // **所有权只有一个地方**，两条路可以混用：
    //   - **拥有型**（`AddChild`）：节点由本节点持有（`unique_ptr`），只有
    //     `RemoveChild` / `ClearChildren` / 本节点析构这三件事会结束它的生命；
    //   - **借用型**（构造期传 parent 的成员、栈对象）：基类一直如此——父级
    //     只记住指针，**不**负责销毁。
    // `GetChildren()` 是"所有子节点"的视图（两种都含）；`GetOwnedChildCount()`
    // 数的是其中由本节点拥有的那一部分。规则与坑见 `UsageReport.md` §6.7。

    /**
     * 挂一个**由本节点拥有**的子节点（运行期）。
     *
     * 返回裸指针只是便利（接着配字段时用）；所有权已经进了本节点的表，
     * **别**再把它塞进别处的 `unique_ptr`。
     *
     * 传进来的节点会：成为本节点的子节点（`GetParent()` / `GetChildren()` /
     * 沿父链登记进场景的绘制列表）；若它原来挂在别处，先被从旧父级摘干净
     * （跨场景也行）。骨架变化照常标脏，所以下一帧就会被画到、被点到。
     */
    InkingWidget* AddChild(std::unique_ptr<InkingWidget> child);

    /**
     * 摘下一个由本节点拥有的子节点，**把所有权交回调用方**。
     *
     * @return 摘下来的节点；`nullptr` = 它不在本节点的拥有表里（借用型子节点
     *         走这条路会拿到 nullptr——它本来就不归本节点管，要摘用
     *         `SetParent(nullptr)`）。
     *
     * 摘下来之后它脱离了渲染树（不再被画、也不参与命中），但**还活着**：
     * 调用方可以挂到别处，或者让它就此析构（不接返回值就是后者）。
     */
    std::unique_ptr<InkingWidget> RemoveChild(InkingWidget* child);

    /**
     * 把**本节点拥有**的子节点全部摘下并销毁（借用型的不受影响）。
     * @return 销毁了几个。
     */
    std::size_t ClearChildren() noexcept;

    /// 本节点拥有几个子节点（`GetChildren().size()` 是"所有子节点"，可能更多）。
    std::size_t GetOwnedChildCount() const noexcept;

    // ---------------- 命中 ----------------

    /**
     * 指针在这个位置上吗。坐标是**世界坐标（设计空间）**。
     *
     * 默认返回 false：基类不可实例化，也没有形状概念——"怎么算在里面"是
     * 具体组件的知识（按钮问自己的 SDF 形状，见 `InkingShapeContains`）。
     *
     * 谁负责 z 序：**调用方**。这个函数只回答"我在不在这个点下面"，
     * 不回答"是不是我最上面"——后者要按绘制列表从高到低问，规则只有一份
     * （`InkingScene` 的命中表 / `hitTestTopmost`）。
     *
     * 它只回答"几何上在不在"，**不回答"要不要让过"**——后者是三态里的
     * `PassThrough`，见 `SetHitPassThrough`（把几何与策略分开）。
     */
    virtual bool HitTest(float worldX, float worldY) const;

    /**
     * @brief 指针停在我身上时，系统光标该是什么形状（T-09）。默认 `Default`。
     *
     * 谁能声明：只有"这里确实能做事"的组件——按钮 / 滑块 / 勾选件 = `Hand`，
     * 输入框 = `Text`。**不要**给容器声明：容器的悬停目标最终是它里面的具体组件，
     * 容器自己抢着声明会让"整块面板都是手型"。
     *
     * 窗口每帧读**一次**（当前悬停节点），形状不变就一个字节都不做——所以这里
     * 是个纯查询，不许有副作用（`AGENTS.md` §3 第 3 条：查询不标脏）。
     */
    virtual InkingCursorShape GetCursorShape() const noexcept;

    /**
     * @brief 命中测试的**节点版**：这个点落在我的子树里，"最上面那个该收事件"的
     * 节点是谁。默认实现：**子树优先**，子树里没有 Block 命中才轮到自己（A8a）。
     *
     * 为什么除了 `HitTest` 还要有它：`HitTest` 只回答"在不在我身上"，而事件要发给
     * **最具体的那一个**。容器（`OwnsSubtreeHit() == true`）的子孙**不进全局命中表**
     * ——表里只有容器那一条包络——所以光靠查表只能查到容器，于是容器里的按钮 /
     * 输入框收不到 hover、点击、滚轮、拖动，也**拿不到键盘焦点**（焦点是"点击获得"
     * 的，见 `InkingScene::SetKeyboardFocus`）。有了这一步，场景在查到容器之后可以
     * 再问一次"你子树里是谁"，把事件发到该去的地方。
     *
     * **默认实现就是"容器"语义**（0.4.0 A1）：先按 `IsAbove` 取子树里最上面那个
     * Block，取不到才看自己。于是任何声明了 `OwnsSubtreeHit()` 的组件（动态容器、
     * 业务自写的弹层）**不用再抄一遍递归**——以前只有 `InkingStaticPanel` 有一份，
     * 动态组件子树里的静态按钮因此永远收不到点击。
     *
     * **契约**（重写时按这三条来）：
     *  1. 返回子树里**最上面那个 `Block`** 命中的节点（不按 `IsHitPassThrough`
     *     过滤掉自己那一笔——组件自己是不是 Block 由调用方看
     *     `IsHitPassThrough()` 决定）；
     *  2. 子树里**只有 `PassThrough`**（例如标签）命中时返回 `nullptr`：那种情况
     *     让调用方**保留"容器自己"那份答案**，不去改全局三态的语义
     *     （"容器里的标签盖住容器外的按钮"是**局部层级**的事，暂未做）；
     *  3. 什么都没命中返回 `nullptr`。
     *
     * 非 const：返回的指针要拿去派发事件。规则只有一份——容器里的顺序用的还是
     * `IsAbove`（z 优先、同 z 看注册序号），不另立一套。
     */
    virtual InkingWidget* HitTestNode(float worldX, float worldY) noexcept;

    /**
     * @brief 用**语义方式**触发一次"点击"（D7），不经过指针。
     *
     * 自检 / 快捷键 / 脚本要"点一下这个控件"，就不该 `dynamic_cast` 到
     * `InkingStaticButton*` 再调它自己的 `TriggerClick()`——那会让调用方和
     * 具体控件类型绑死（换控件类型编译期就崩）。这里给基类一条通道：
     * 组件自己决定"被激活"是什么意思。
     *
     * 默认返回 false（"我不是能被激活的东西"），按钮那两档返回
     * `TriggerClick()` 的答案。它不是写入口、不标脏、不改变三态。
     *
     * @return 真的发生了什么才返回 true（例如按钮没有订阅者时为 false）。
     */
    virtual bool Activate() { return false; }

    /**
     * 命中索引用的**保守包络**，相对本节点左上角（设计坐标）。
     *
     * 默认给整块矩形（基类不认识形状，那是组件的事）——格子只负责缩小范围，
     * 包络给大了只是候选多几个，**不会误命中**，所以"保守"是安全的默认。
     *
     * **什么时候必须 override**：本节点的变换会在运行期变（三态之间的
     * `transform`）时。包络要覆盖**所有可能变换**，否则表里登记不全 → 漏命中。
     * 变换通道的设计是"表冻结、查表时反变换"（`InputDesign.md` §11），
     * 靠的就是这里的包络不动、而精判用当前变换。
     */
    virtual InkingShapeBounds GetHitEnvelope() const;

    /**
     * 命中之后让不让过（三态里的 `PassThrough`）。
     *
     * 默认 `false` = **Block**（事件到此为止）——半透明遮罩、普通控件都是这一类；
     * 置 true 表示"我在这儿，但事件继续往下找"。点击与悬停共用这条规则。
     * 它**不标脏**：让不让过是策略，不是几何，改了不会让命中表失效
     * （代价是当前这一帧的判定还用旧策略，最迟下一帧生效）。
     */
    void SetHitPassThrough(bool passThrough) noexcept;
    bool IsHitPassThrough() const noexcept;

    // ---------------- 拖拽原语（1.4） ----------------
    //
    // 声明式的两句话：「我能被拖」「我收不收」。起拖阈值、幽灵、独占、取消
    // 四条路都在场景侧（`drag/InkingDragSession.h`），这里只存数据。
    //
    // **不继承任何新基类**：拖拽不是"一种控件"，是任何控件都能挂上的两个标签。
    // 挂上之后行为的变化只有一条——按着它移动超过 `InkingDragStartThreshold`
    // 会起拖，而那一次松手**不再派发 click**（拖拽是独占会话）。
    //
    // 明确不做（理由见 `docs/zh/api/InkingDrag.md`）：边缘自动滚动、跨窗口 /
    // 跨进程拖拽、多选批量拖、排序 / 落位动画、payload 生命周期管理。

    /**
     * @brief 声明"我能被拖，拖的是什么"。传 `payloadType = nullptr` 即清除。
     *
     * **payload 的生命周期归调用方**：库只原样转交，不解引用、不 clone、
     * 不释放。源控件在拖拽途中被删时库只清指针（任务书 §6 风险表）。
     *
     * 不是写入口：不标脏、不改几何。挂上 / 摘掉只影响以后的手势，不影响这一帧。
     */
    void SetDragSource(const InkingDragSourceSpec& spec);

    /// 声明"我收，收的时候给落点"。传 `acceptType = nullptr` 即清除。
    ///
    /// 落点按**最近祖先**解析（与 `HandlesSubtreeWheel` 同一条"组件说一句我要、
    /// 框架负责找路"）：指针下命中的是行里的标签时，也会落到行上。
    void SetDropTarget(const InkingDropTargetSpec& spec);

    /// 我声明的拖拽源 / 落点（没声明时是默认构造的那份，字段全空）。
    const InkingDragSourceSpec& GetDragSource() const noexcept;
    const InkingDropTargetSpec& GetDropTarget() const noexcept;

    /**
     * @brief 我正在被当作落点高亮吗（拖拽中的指针底下，且类型匹配、`canDrop` 说行）。
     *
     * **状态不是外观**：库不认识"高亮边框 / 选中底色"长什么样，它只把这件事
     * 告诉你，画由 `onRender` 自己画——和 `MouseHover()` 同一条分工。
     *
     * 它变化时会标**重绘脏**（画面变了，但命中表一点都不用重烘）。
     */
    bool IsDragOver() const noexcept;

    /// 框架调：置 / 清高亮标记。别在业务代码里调（那是会话的账）。
    void MarkDragOver(bool over) noexcept;

    // ---------------- 输入派发：在两个接口上 ----------------
    //
    // `PushPointerHover` / `PushPointerPress` / `PushPointerClick` /
    // `PushPointerWheel` / `PushPointerDrag` 在
    // `include/input/InkingPointerHandler.h`；
    // `PushKeyDown` / `PushKeyUp` / `PushTextInput` / `PushTypingChanged` 在
    // `include/input/InkingKeyboardTarget.h`。
    //
    // 场景判出"谁该收这次输入"之后直接调那些 `Push*`——**不做任何类型查询**
    // （A1 那条：早年 `static_cast<InkingStaticButton*>` 是 UB 的来源）。

    /// 沿父链找最近的"要滚轮"的容器（含自己）；没人要就返回 nullptr。
    ///
    /// **只有它留在本层**：接口给的是"我要不要滚轮"（`HandlesSubtreeWheel`），
    /// 而"沿父链找"要用树结构。
    InkingWidget* FindSubtreeWheelOwner() noexcept;

    /// 键盘焦点现在在不在我这儿（由场景维护，见 `InkingScene::SetKeyboardFocus`）。
    ///
    /// **字段留在本层**（接口是零状态的）：焦点是**场景级**的唯一归属，改它要跟
    /// 场景打交道，而 `InkingKeyboardTarget` 不该知道场景。
    bool HasKeyboardFocus() const noexcept { return _hasKeyboardFocus; }

    /// 框架调：把"焦点归属变了"记到自己身上并转给钩子。别在业务代码里调。
    void MarkKeyboardFocus(bool focused) noexcept {
        if (_hasKeyboardFocus == focused) {
            return;
        }
        _hasKeyboardFocus = focused;
        onFocusChanged(focused);
    }

    // ---------------- 归属与场景 ----------------

    /**
     * 重新认定"我属于哪个场景"，并补登记进它的绘制列表。
     *
     * 只给场景在构造末尾用：场景是"构造完成才盖章成渲染树的根"的，所以
     * **场景构造函数体里建出来的子节点**沿父链找不到它（那时它还没盖章），
     * 既不渲染也点不到。场景盖章之后回头把这些节点补登记一遍，就是这个函数。
     *
     * 已经在列表里的节点是空操作（`AddDrawNode` 自己查重），所以重复调用安全。
     * 只由框架调用，别在业务代码里手工调。
     */
    void RebindScene(InkingScene* scene) noexcept;

    /**
     * 只改"我属于哪个场景"这条指针，**不碰场景的绘制列表**（0.4.0 A2）。
     *
     * 给"场景按树重建收集表"那条路用：树是唯一真相源，重建时扫到的节点
     * 一律认领，但列表正在重建中——再回头调 `AddDrawNode` 会边遍历边改容器。
     * 只由 `InkingScene::collectDrawList()` 调。
     */
    void AdoptIntoScene(InkingScene* scene) noexcept { _scene = scene; }

    /// 本节点所属的场景，非拥有指针；没有场景时为 nullptr。
    InkingScene* GetScene() const noexcept;

    // ---------------- [final] 写入口：骨架变化 ----------------
    //
    // 可见性、层级、父子关系这类"骨架变化"两个版本都有，所以放在基类。
    // 一律不加 virtual：子类不可重写（docs/API.md「[final] 写入口」）。
    // 标脏与钩子都在内部完成，所以不存在"哪次改动忘了标脏"的漏。
    // 返回 bool 表示这次调用是否真的改动了；没变就不标脏、不触发钩子。
    //
    // 几何写入口（尺寸 / 自身锚点 / 上级锚点 / 偏移）**不在这里**——那是动态
    // 组件才有的东西，见 `InkingDynamicWidget`。放进基类就等于"表里存的基类
    // 指针也能改静态组件的几何"，而那正是要避免的事。

    /// 可见性变化：不画了，但结构还在（数据、子节点、命中条目都保留）。
    /// 连带影响绘制顺序，所以按骨架变化处理——标脏 + 通知场景重排。
    bool SetVisible(bool visible) noexcept;

    /// 改本节点在**兄弟之间**的层级（局部层级：与父级的兄弟无关）。
    /// 层级变化是要标脏的四件事之一。
    bool ChangeZIndex(int zIndex) noexcept;
    bool SetParent(InkingWidget* parent) noexcept;

    void SetMagnification(float magnification) noexcept;  ///< 窗口层写，不标脏

    // ---------------- 运行期显示变换（A13） ----------------
    //
    // "显示变换"是**变换通道**（`InkingTransform.h`）在运行期的那条写入口：
    // 移动 / 旋转 / 缩放"看起来的样子"，而**命中用同一条变换的逆**——所以视觉上
    // 挪到哪、点就得到哪（`AGENTS.md` §6 第 38 条：正逆必须严格互逆）。
    //
    // 它和组件自己那份变换的关系是**复合**：最终显示 = 外部 ∘ 组件内部那一份
    // ——三态过渡 / 开合动画照旧，外面的拖动叠在它之上。
    //
    // **它不是几何写入口**：本地几何（`_width` / `_height` / 锚点 / 偏移）一个
    // 字节都不动，所以静态档那条铁律（没有几何写入口）不受影响。代价是
    // **不改变占位**：兄弟不让位、布局与滚动范围都不认它。
    //
    // 三条要记住的：
    //   1. **默认关**：组件类型要重写 `AcceptsExternalTransform()` 声明"我认"，
    //      否则 `SetDisplayTransform` 会**拒绝并记一笔**——不静默（`AGENTS.md`
    //      §6 第 30 / 43 条那一族）；
    //   2. **会动命中表**：表里登记的是"组件所有可能变换的并集"，而任意拖动是
    //      **无界**的、并集算不出来 ⇒ 每次真的改了变换都要标**结构脏**，下一帧
    //      整表重烘。这是框架里**唯一**一条会动表的显示通道；
    //   3. **幂等**：同一份变换重复设置返回 false、一个脏都不标
    //      （观测口 `GetDisplayTransformRevision()`）。

    /**
     * @brief 设运行期显示变换（绕组件中心；与组件自己的变换**复合**）。
     *
     * @return 真的改了才返回 true。落在四种 false 上：组件没声明支持
     *         （`AcceptsExternalTransform()` 为假，同时记一笔拒绝）、这份变换
     *         与当前那份**逐位相同**（幂等）、组件已被移出场景之类。
     */
    bool SetDisplayTransform(const InkingTransformSpec& transform) noexcept;

    /// 清掉外部显示变换（回到"只有组件自己那份"）。同样幂等、同样标两份脏。
    bool ClearDisplayTransform() noexcept;

    /// 当前的外部显示变换（默认单位变换；没设过就是它）。
    const InkingTransformSpec& GetExternalTransform() const noexcept;

    /**
     * 把"组件自己那份显示变换"与外部变换合起来——**给各组件的
     * `GetDisplayTransform()` 用**（照抄这一行：`return CombineWithExternal(内部那份);`）。
     *
     * 两个都绕**同一个中心**（组件中心），所以复合结果是闭式的：
     *   `scale = 外×内`、`rotate = 外+内`、
     *   `translate = 外.scale × R(外.rotate) × 内.translate + 外.translate`。
     * 外部是单位变换时**原样返回**（没开这条通道的组件零开销）。
     */
    InkingTransformSpec CombineWithExternal(const InkingTransformSpec& own) const noexcept;

    /**
     * 这个组件类型认不认外部显示变换（**默认 false**，照 `WantsKeyboardFocus()`
     * 那条先例：框架给的通道一律"默认不要、声明才有"）。
     *
     * 不声明就调 `SetDisplayTransform` 会被拒绝并计数——故意的：默认开启会让
     * "某块东西莫名被挪走 / 缩放"变成难查的问题，而拒绝是当场可断言的。
     */
    virtual bool AcceptsExternalTransform() const noexcept;

    /// 外部显示变换**真的**改过几次（幂等的不算）。自检的计数观测口。
    std::uint64_t GetDisplayTransformRevision() const noexcept;

    /// 被拒绝了几次（组件没声明支持时调的）。可断言，不静默。
    std::uint64_t GetDisplayTransformRejectedCount() const noexcept;

    /// a 是否盖在 b 上面：**按树序 + 兄弟内 z** 比较（局部层级）。
    ///
    /// 规则三条：① 父容器先于自己的子孙画（底色永远在下）；② `zIndex` 只在
    /// **兄弟之间**生效，与父级的兄弟无关；③ 同 z 时晚注册的在上。
    /// 等价于"从根到自己的 (z, 注册序号) 序列按字典序比较"，前缀短者（祖先）在前。
    ///
    /// 规则只能有一条，跨子树 / 跨簇 / 溢出桶才好统一比较（docs/InputDesign.md
    /// §1、§15）。这条仍是**严格全序**，所以四处调用方可以继续共用它。
    ///
    /// **渲染顺序和命中顺序必须共用这一个比较器**，否则会出现"画的是 A、
    /// 点到的是 B"——两边单独看都对，合起来才错。
    ///
    /// 代价与边界（写清楚免得下一个人以为是漏了）：
    ///   - 子节点的 z 再大也盖不过**父级的兄弟**（真正的逃逸要"提升到根层"的
    ///     portal，尚未提供）。所以"展开的下拉要压住别的内容"应该抬高**容器
    ///     自己**（`Select` 展开时正是这么做的），而不是抬孙子。
    ///   - 容器的自绘只有一笔（底色+描边一起提交），它整体在子孙之下；想让
    ///     描边压在子孙之上需要拆成背景笔 / 前景笔，暂不支持。
    static bool IsAbove(const InkingWidget& a, const InkingWidget& b) noexcept;

    /// 标记自己"变了"。写入口内部已经调过，子类不要自己调。
    void MarkDirty() noexcept;

    /// 标记自己"画面变了"。写入口与**动画钩子**内部调（`onAnimationTick` 里
    /// 颜色/变换推进了一帧就该调它）——动画每帧都在变，但它不该惊动命中表。
    void MarkRepaintDirty() noexcept;

    /// 子表的维护（T-7）：构造时登记、析构 / 搬家时摘掉。私有——外部不该直接动。
    void AttachToParentChildren() noexcept;
    void DetachFromParentChildren() noexcept;

    /// 通知所属场景"骨架变了，绘制顺序得重排"。
    /// 构造、析构、SetParent、ChangeZIndex、SetVisible 内部都会调。
    void NotifySceneStructureChanged() noexcept;

    /// 把 this 登记进场景的扁平绘制列表。
    /// 由 InkingWidget 构造时沿父链登记自己，以及 InkingScene 在场景构造时
    /// 对整棵现有子树补登记一次（场景必须比子树后构造时才会用到）。
    /// 只由框架调用，别在业务代码里手工调。
    void RegisterToScene(InkingScene* scene) noexcept;

    /// 从所属场景的绘制列表里摘掉自己（解引用、换父级时用）。
    void UnregisterFromScene() noexcept;

    /// 把 `_scene` 指针置空，**但不碰场景的绘制列表**。
    ///
    /// 给"InkingScene 正在析构"这条路径用：那时列表马上随场景一起销毁，
    /// 而列表里的裸指针可能正指向正在析构的兄弟节点，碰不得。
    /// 只清指针，节点留着的那份"我属于这个场景"就不会变成野指针。
    void DetachFromScene() noexcept;

    /// 把本节点提交给渲染器。由场景的渲染入口按 z 序逐个调用，
    /// 传进来的绝对坐标是**设计坐标**，这里换算成设备像素再交给 onRender。
    void SubmitToRenderer(SDL_Renderer* renderer, float absX, float absY) const;

    /**
     * 把本节点的**装饰笔**提交给渲染器（0.2.0 T-07）。
     *
     * 与 `SubmitToRenderer` 的差别只在调用的钩子（`onRenderDecoration`），
     * 槽位搭建（当前渲染器 / 祖先仿射 / 顶点缓存）完全是同一份。
     *
     * 调用时机由场景掌握：**本节点的整棵子树画完之后、上层兄弟画之前**——
     * 于是描边这类"要压在子孙之上"的一笔不被子孙盖住，也不越界压到
     * 兄弟头上（局部层级，与 `IsAbove` 同一套语义）。
     */
    void SubmitDecorationToRenderer(SDL_Renderer* renderer, float absX,
                                    float absY) const;

    /**
     * 本帧有没有装饰笔要画（默认 false）。
     *
     * 场景的装饰提交靠它**跳过无装饰的节点**：绝大多数节点没有装饰笔，
     * 有这个口子，"每帧多一趟"的稳态成本就是一次布尔虚调用，而不是
     * 每节点再搭一遍渲染槽位（祖先仿射是 O(深度) 的）。
     *
     * 公开：调用方是场景（它决定何时落笔），与 `IsDynamic` / `OwnsSubtreeHit`
     * 同一档——**查询**，不是"怎么画"的实现细节。
     */
    virtual bool HasRenderDecoration() const noexcept { return false; }

    /**
     * **每渲染帧一次**：推进这个节点自己的动画（颜色 / 透明度过渡）。
     *
     * 由场景在渲染之前调（`InkingScene::TickFrame` 里分发），传真实 delta。
     *
     * 为什么静态组件也有这个入口：`InkingDynamicWidget::Tick` 是**动态档**的逻辑步
     * （几何会变的那一档才收得到），而颜色过渡不该因为"几何不变"就没法推进。
     * 两条通道分开是有意的——颜色动画不标脏、不进命中表。
     *
     * 放渲染帧而不是逻辑步：过渡要和画面同拍，否则快速动画下会差帧
     * （docs/InputDesign.md §11「命中与渲染必须用同一个 t」）。
     */
    void TickAnimation(float deltaSeconds) noexcept {
        onAnimationTick(deltaSeconds);
    }

    // ---------------- 布局（A6） ----------------
    //
    // 规则（`InkingLayoutSpec`）住在**父容器**的数据里，孩子只在自己数据里写意愿
    // （`InkingLayoutItem`）。算出来的尺寸与位置**写进运行期几何**（`_width` /
    // `_height` / `_offsetX` / `_offsetY`），所以位置推导一条路都没变——
    // 绘制 / 命中 / AABB / 子树变换都不知道"有布局这回事"。

    /// 我的布局规则（来自配置）。`kind == None` = 我不排孩子（老行为）。
    const InkingLayoutSpec& GetLayoutSpec() const noexcept { return _layout; }

    /// 我在父容器布局里的意愿（margin / fillMain / min-max / selfAlign）。
    const InkingLayoutItem& GetLayoutItem() const noexcept { return _layoutItem; }

    /**
     * 测量钩子（`[√]`）：**我在这份约束下想要多大**。
     *
     * 契约：**纯函数**——不许改自己的几何、不许标脏、不许有副作用（布局可能要
     * 问好几遍）。默认实现返回"我当前的尺寸"；内容驱动的组件重写它才有意义
     * （label 去问文字层要高度、图片去要原始尺寸……）。
     */
    virtual InkingLayoutSize MeasureLayout(const InkingLayoutConstraints& constraints) const;

    /**
     * 请求重排（`[final]`）：从**我这棵子树**开始同步排一次。
     *
     * 什么时候调：**凡是我这棵子树的排布输入变了**——
     *   - 内容变了（文字改长、换语言、列表增删）之后：那些变化平时只标重绘脏，
     *     布局不会自己知道；
     *   - 容器**自己**尺寸变了之后（例如抽屉拉开时改尺寸）：在 `onSizeChanged()`
     *     里调一句就行。
     *
     * **框架不做隐式重排**（不在结构脏消费里替所有容器跑一遍）：那会让"什么时候
     * 重排"变得看不见，也会在容器里套容器时出现难以推理的重入。代价是调用方要
     * 记得喊一声——所以它是幂等的：算出来跟当前几何一模一样时**一个脏都不标**，
     * 多喊几次不会白重建绘制列表与命中表（自检里用 `GetStructureGeneration()`
     * 钉这条）。
     */
    void Relayout() noexcept;

    /**
     * 请求**上游**重排（`[final]`）：往父链找第一个"声明了布局"的祖先，让它排一次。
     *
     * 给"内容驱动的尺寸变了"的组件用——例如标签换了更长的文字：它自己的高度变了，
     * 而**兄弟要让位**，那是父容器的活儿。`InkingStaticLabel::SetText` 就是调它。
     *
     * 找不到布局容器时（`0.1.1` 起）**改走自身测量**：见
     * `RelayoutSelfIfContentSized()`。以前这里是静默 `return`，于是
     * "内容驱动尺寸 + 无布局容器"这一整类组合会把尺寸留在 0——组件一个像素都
     * 不画、也不报错（反馈方那张 200×0 的标签就是这么来的）。
     */
    void RelayoutParent() noexcept;

protected:
    /// 只给两个派生类调：**本类不可直接实例化**，每个组件声明时必须选一种
    /// （静态还是动态）。根（窗口 / 场景）传 nullptr 当 parent。
    /// `dynamic` 就是"这个组件属于哪一档"，由派生类在构造时如实传进来。
    InkingWidget(InkingWidget* parent, const InkingAnchorData& data, bool dynamic);

    /// 配置驱动：只给 parent + 名字，怎么读 CXXCSS 交给子类自己实现。
    InkingWidget(InkingWidget* parent, const std::string& name, bool dynamic);

    // ---------------- [√] 可重写钩子 ----------------
    //
    // 钩子只挂附加逻辑：不负责标脏（写入口已经标了），也不拦截写入口。
    virtual void onSizeChanged();
    virtual void onSelfAnchorChanged();
    virtual void onTraceAnchorChanged();
    virtual void onOffsetChanged();
    virtual void onZIndexChanged(int zIndex);
    virtual void onParentChanged(InkingWidget* parent);
    virtual void onVisibleChanged(bool visible);
    virtual void onDirty();

    // ---------------- [√] 输入钩子：在两个接口上 ----------------
    //
    // 指针 5 个（`onPointerHover` / `onPointerPress` / `onPointerClick` /
    // `onPointerWheel` / `onPointerDrag`）见 `InkingPointerHandler.h`；
    // 键盘 5 个（`onKeyDown` / `onKeyUp` / `onTextInput` / `onTypingChanged` /
    // `onFocusChanged`）见 `InkingKeyboardTarget.h`。**重写时要写 `override`**。
    //
    // 0.5.0 起 `onPointerPress` / `onPointerClick` 带 `InkingPointerButton`：
    // 组件只见 Primary（左键 / 轻点）/ Secondary（右键 / 长按），**看不见设备**。
    // 重写时记得判键——不判的话右键也会走进"左键"那条逻辑。

    /**
     * 本节点自己的动画，**每渲染帧一次**（真实 delta）。基类什么都不做。
     *
     * 静态 / 动态两档都会收到：颜色过渡与几何无关，所以它不归动态档那条 `Tick`。
     * 只被 `TickAnimation` 调，别在业务代码里手工调。
     */
    virtual void onAnimationTick(float deltaSeconds);

    /// 本节点怎么画。场景的渲染入口在提交每个节点时调一次。
    /// 传进来的是本节点的绝对左上角，已经是**设备像素**（展示倍率换算过了）。
    /// 现在的实现只是"按 color 填一个矩形"，SDF 形状层接手后换成形状描述。
    virtual void onRender(float pixelX, float pixelY) const;

    /**
     * 本节点的**装饰笔**（0.2.0 T-07）：画在**整棵子树之后**的那一笔。
     *
     * 存在的理由：容器的自绘（`onRender`）整体在子孙之下（`IsAbove` 保证祖先
     * 先画），所以"描边"跟着填充走会被子节点盖住——加完等于没加。把描边挪到
     * 这一笔，它就压在子孙之上；同时它仍画在**上层兄弟之前**，不会越界盖住
     * 层级更高的别的子树（局部层级）。
     *
     * 基类什么都不画。**阴影不该放这里**：它是"形状之下的偏移副本"，要跟填充
     * 同一趟（放这里反而会盖住自己的内容）。
     *
     * 装饰笔**不参与命中**、不进绘制列表（`GetDrawItemCount` 不变）、不标脏——
     * 它与 `onRender` 同帧提交，重绘脏由各自的写入口照旧标。
     */
    virtual void onRenderDecoration(float pixelX, float pixelY) const;

    /**
     * **骨架变化**的统一出口：命中脏 + 重绘脏 + 通知场景重排，一次做完。
     *
     * 写入口（几何四个 + \c SetVisible / \c ChangeZIndex / \c SetParent）内部都调它。
     * 集中在一个函数里有两个好处：
     *   1. 不会漏——漏一个就是"这条改动没被消费"，而且往往不报错、只是画面或命中悄悄不对；
     *   2. 将来要按 \c InputDesign.md §8 把标脏"送到最近的表边界"时，只改这一处。
     */
    void MarkStructureChanged() noexcept;

    /**
     * **几何移动**的统一出口（T-19）：按"子树里有没有静态节点"分流。
     *
     *   - 有静态子孙 → `MarkStructureChanged()`（照旧全量重建）。理由：静态子孙的
     *     绝对坐标用的是命中表 / 绘制列表里冻住的快照（`DrawItem::absX`），祖先一动
     *     就全部过期，而"让静态子也现算"要改渲染热路径上每个静态节点的取坐标方式。
     *   - 没有（叶子 / 全动态子树）→ 只标脏 + 让场景**只刷洞**。列表、排序、静态条目
     *     都不会因为"某个动态节点挪了几像素"而变：`IsAbove` 只比树序与兄弟内 z。
     *
     * 四个几何入口（`Resize` / `ChangeSelfAnchor` / `ChangeTraceAnchor` /
     * `ChangeOffset`）调它——它们是**唯一**会"移动一个已经在树里的节点"的写入口
     * （`SetDisplayTransform` 走 A13，那一份包络本来就覆盖所有可能变换，洞不用刷）。
     *
     * **判据写错的症状**（所以配了自检）：快路走错 → 移动中的组件在新位置**点不到**
     * （洞过期）；全量走错（本该快却全量）只是慢，不会错。
     */
    void MarkGeometryMoved() noexcept;

    /**
     * 只告诉场景"我挪了，洞该刷了"（T-19）。不动列表、不动排序、不重烘表。
     * 与 `NotifySceneStructureChanged()` 是两条通道，别混：那个是"骨架变了"。
     */
    void NotifySceneDynamicMoved() noexcept;

    /// 场景专用：进构造函数体后把自己声明为渲染树的根。
    ///
    /// 不能指望父链查找在构造期就认出场景——构造 InkingScene 时它的动态类型
    /// 还是 InkingAnchor，那一趟查找认不出来，所以由场景自己在构造末尾盖章。
    void MarkAsSceneRoot(InkingScene* scene) noexcept;

    /// 场景专用：场景析构前把子树里指向自己的 _scene 摘掉，
    /// 避免留下指向已销毁场景的野指针。
    void DetachSceneFromSubtree() noexcept;

    /**
     * 布局层专用的几何写入口（A6）。
     *
     * 为什么静态档也需要它：布局是"父级算好、写给孩子"，而静态档本来**没有**
     * 几何写入口（`Resize` / `ChangeOffset` 只挂在 InkingDynamicWidget 上，
     * 那是"静态几何定死"这条铁律的实现方式）。A6 的判据是"排一列**按钮**"
     * 而按钮是静态档，所以这里必须开一条口子，但把它开得尽可能窄：
     *
     *   - **只有 `inking_detail::layout::ArrangeSubtree` 是它的 friend**：业务代码
     *     想调是**编译错误**（不是文档约定）；
     *   - 名字带 `Layout`、参数是布局层的 `InkingLayoutRect`，不会被误认成 resize；
     *   - 幂等：矩形没变就返回 false、**一个脏都不标**（重复排不白干）。
     *
     * 实现把矩形**反解**成锚点公式里的量：锚点归零（布局里锚点不参与定位）、
     * 尺寸取整写进 `_width` / `_height`、偏移写进 `_offsetX` / `_offsetY`。
     */
    bool ApplyLayoutRect(const InkingLayoutRect& rect) noexcept;

    // ---------------- 内容驱动尺寸的自测量（0.1.1 T-01） ----------------

    /**
     * @brief 我是**内容驱动**的、又没被布局管着时，自己量一次尺寸。
     *
     * 语义只有一条：**只补"没配置"的那一维**（`<= 0` 用测量值），已经配置过的
     * 维度一个字节都不动。这一条是硬约束，不是保守：实测那个 200 宽的标签，
     * 按内容量出来的宽是 76——若拿测量值覆盖，`width: 200` 会被压成 76，
     * 右对齐文字与背景对位全部错位。
     *
     * 触发点只有一个，在 `RelayoutParent()` 的末尾：它原本的语义是"往上游找
     * 布局容器"，找不到时**以前是静默返回**——那正是"内容驱动尺寸 + 无布局
     * 容器"整类组合静默失效的来源（`AGENTS.md` §6 那条"漏一处就是画不出来"）。
     * 现在找不到就自己量，于是 `SetText` 那条既有链路自动生效，不必在每个
     * 组件里各加一处。
     *
     * **布局容器内一律不自测量**：那种情况下尺寸归父容器管，自己量会和布局
     * 打架（也会在父容器尚未构造完时量出一个偏小的值）。
     */
    void RelayoutSelfIfContentSized() noexcept;

    /**
     * @brief 尺寸为零时告警（`0.1.1` T-02）。
     *
     * **可绘制组件**在构造末尾各调一行：0 尺寸意味着"画不出来、也点不到"，
     * 而它不报错、不崩溃——界面上只是少了一块，最难查的那一类。
     *
     * 边界：**不查纯容器**。0 尺寸的分组节点常被用来做逻辑分组（`visible`
     * 由业务控制、自己不画东西），对它告警只会变成噪声。判据是"有没有
     * 自己那一笔"，所以这条由组件自己决定调不调，基类不替它判断。
     */
    void WarnIfZeroSize(const char* moduleName) const;

    friend void inking_detail::layout::ArrangeSubtree(InkingWidget& container);

    /**
     * **唯一的第二个例外**：动画组要按"换到哪个动作"改自己的尺寸（0.6.6 T-18）。
     *
     * 为什么它非得进得来：一个角色有 4 个动作、格宽 40/48/56 不等，组是"一个角色"
     * ——尺寸跟着动作走是**组的职责**。而单个 `InkingSprite` 换配置时尺寸归调用方
     * （那两处语义相反，文档都写了）。
     *
     * 开得同样窄：一个具体类、不是"所有派生类"，而且它只能通过 `ApplySelfSize`
     * （不动锚点 / 不动 offset 那一版）改尺寸。
     */
    friend class InkingAnimateGroup;

private:
    /**
     * 渲染槽位的搭 / 拆（T-07 从 SubmitToRenderer 抽出）：当前渲染器、祖先
     * 仿射、顶点缓存三样。两个提交入口（填充笔 / 装饰笔）共用同一份——
     * 抄第二份就会出现"填充和描边看到的祖先变换不一样"。
     *
     * @return 设计坐标 → 设备像素的倍率，调用方拿去换算提交坐标。
     */
    float BeginRenderSlots(SDL_Renderer* renderer) const;
    void EndRenderSlots() const;

    /**
     * 自测量那条路唯一的几何写入（`0.1.1` T-01）。**private**：和
     * `ApplyLayoutRect` 同一个理由——"业务代码改不了静态几何"要是编译期
     * 保证，就得让它在派生类里也看不见。
     *
     * 与 `ApplyLayoutRect` 的关键区别：**不动锚点、不动 offset**，
     * 只写 `_width` / `_height`。后者会把锚点归零，而自测量发生在
     * "靠锚点定位"的那种界面里——归零就等于让标签跳到父级左上角。
     *
     * @return 尺寸真的变了才 true（幂等；重复量出同一个数不标脏）。
     */
    bool ApplySelfSize(int width, int height) noexcept;

protected:

    std::string _name;

    /// 布局（A6）：**我是容器时**的规则，与**我是孩子时**的意愿。
    /// 和别的字段一样，构造时从 `InkingAnchorData` 拆存一份（基类不存整份 data）。
    InkingLayoutSpec _layout{};
    InkingLayoutItem _layoutItem{};

    /**
     * 这个节点的**顶点缓存槽**（T-6）：静止帧直接复用顶点，不必每帧重算轮廓。
     * 懒创建（只有真的填过形状的节点才会有一个）。所有权在这儿，
     * 所以节点销毁就跟着销毁——提交层不必维护"谁还活着"。
     */
    /// `mutable`：提交是 const 的（绘制不改节点状态），但缓存得在这儿搭起来
    /// ——缓存不是可观测状态，正是 `mutable` 该用的地方。
    mutable std::unique_ptr<inking_detail::VertexCacheSlot> _vertexCache;

    /// 直接子节点（非拥有）：构造时登记、`SetParent` 时搬家、析构时摘掉。
    /// （**视图**：拥有型的子节点也在这里露面；所有权见 `_ownedChildren`。）
    std::vector<InkingWidget*> _children;

    /**
     * 由本节点**拥有**的子节点（A10）。声明顺序故意排在 `_children` **之后**：
     * 成员按声明顺序的逆序析构，所以本类析构时它先走（子节点先销毁），
     * 而子节点析构体要访问的 `_children` 与基类的 `_scene` 都还活着。
     */
    std::vector<std::unique_ptr<InkingWidget>> _ownedChildren;

    /**
     * 运行期**外部显示变换**（A13）：默认单位（= 没开这条通道）。
     *
     * 它**不属于几何**：本地几何一个字节都不动，只改"画在哪儿 / 画多大"，
     * 命中过它的逆（`CombineWithExternal` 给各组件合成用）。
     */
    InkingTransformSpec _displayTransform{};

    /// 外部变换真的改过几次（`GetDisplayTransformRevision()`）。
    std::uint64_t _displayTransformRevision = 0;

    /// 因为组件没声明支持而被拒了几次（`GetDisplayTransformRejectedCount()`）。
    std::uint64_t _displayTransformRejected = 0;

    int _zIndex = 0;        ///< 兄弟间层级：同父下 z 越高画得越晚（越靠上）
    int _registerOrder = 0; ///< 注册序号，构造时自动赋；只在 z 相同时决定先后

    /// 命中后让不让过（三态里的 PassThrough）。默认 Block。
    ///
    /// 放基类而不是"每个组件自己实现三态"：**让不让过和形状无关**，
    /// 它是策略；而"在不在形状里"才是组件知识。两件事分开之后，
    /// 组件只管把形状判对，策略由写入口改、由场景合并。
    bool _hitPassThrough = false;

    /// 拖拽源 / 落点声明（1.4）：纯数据，默认空 = 不可拖、不是落点。
    /// 挂上摘掉都不标脏——它们不进几何、不进层级，只影响接下来的手势。
    InkingDragSourceSpec _dragSource{};
    InkingDropTargetSpec _dropTarget{};

    /// 正在被当作落点高亮（会话置位，`IsDragOver()` 读）。缺省 false。
    /// 变化时标**重绘脏**：这是外观，命中一点没变。
    bool _dragOver = false;

    /// 键盘焦点在不在自己这儿（A3）。只由场景通过 `MarkKeyboardFocus` 改。
    /// 放基类而不是"每个组件自己存一个 bool"：焦点是**场景级**的唯一归属，
    /// 存两份就会出现"我以为我有焦点、场景以为我没有"。
    bool _hasKeyboardFocus = false;

    /// 属于哪一档：静态（false，几何构造即定型）还是动态（true，几何会自己变）。
    /// 派生类在构造时如实传进来，之后不变——不支持运行期在两者之间迁移
    /// （docs/InputDesign.md §2 第 3 条）。
    bool _dynamic = false;

    std::uint32_t _color = 0xFF569CD6u;  ///< 占位填充色 0xAARRGGBB

    InkingScene* _scene = nullptr;  ///< 所属场景，非拥有；构造时沿父链定下

    /// **命中脏**：会影响命中的改动还没被消费（帧首消费）。
    bool _dirty = true;

    /// **重绘脏**：画面变了还没重绘（帧末消费）。初始为真——第一帧总得画一次。
    bool _repaintDirty = true;

    /// 两个脏各自"已经安静了多少帧"。够 `kHitQuietFrames` / `kRepaintQuietFrames`
    /// 就清掉对应的脏；任何一次新的标脏都会把它归零。
    int _hitQuietFrames = 0;
    int _repaintQuietFrames = 0;

    /// 自测量的重入位（`0.1.1` T-01）：`MeasureLayout` 是个开放钩子，万一某个
    /// 组件在里面又触发一次重排，没有它就会无限递归。只在自测量那一小段为真。
    bool _selfSizing = false;
};

// ---------------------------------------------------------------------------
// 两种 Widget：静态组件与动态组件
//
// 判据只有一条 —— **几何会不会在构造之后自己变**。基类不可直接实例化，
// 所以每个组件声明的时候就必须选一种：
//
//   静态（InkingStaticWidget）：构造时定型，之后几何不动 → 进命中表。
//   动态（InkingDynamicWidget）：几何会自己变 → 不进表，自己维护命中状态，
//       每帧收到一次 Tick。
//
// 分流不靠运行期标记：谁属于哪一档，在**登记**那一刻就是明确的（静态的登记
// 进表、动态的登记进动态档），所以查询与合并时不需要再问一次"你是不是动态的"。
//
// 两版共用的东西（都在 InkingWidget 里）：只读查询、层级、父级、可见性、
// 展示倍率、标脏、IsAbove。也就是说"静态"少的**只有几何写入口**。
// ---------------------------------------------------------------------------

// 几何辅助（`AncestorAffine` / `SubtreeChainAffine` / `ClipChainAffine` /
// `AncestorClipInDevice` / `SubtreeHitEnvelope` / `ClipBoundsInDevice` /
// `IntersectBounds` / `TransformBounds`）**不在这里**了——
// 它们是不属于任何类的自由函数，搬到了 `include/detail/InkingGeometry.h`
// （实现 `src/detail/InkingGeometry.cpp`）。那样依赖是单向的：几何层包含本头，
// 本头不包含几何层，不会循环包含。

