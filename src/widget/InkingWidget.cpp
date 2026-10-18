#include <widget/InkingWidget.h>

#include <detail/InkingGeometry.h>

#include <scene/InkingScene.h>

// 渲染器槽与填色助手：框架内部共用，见 src/render/InkingDraw.h。
#include <render/InkingDraw.h>
#include <render/InkingBatch.h>
#include <detail/InkLog.h>

#include <cmath>

namespace inking {

namespace {

/**
 * 注册序号的来源：只增不减的进程内计数器。
 *
 * 构造时自动赋值的意义是让 IsAbove 在 z 相同时有一条确定的先后规则
 * （后构造的在上面），而不是靠未定义的遍历顺序。以前这件事要调用方
 * 自己调 SetRegisterOrder，漏一次就退化成"顺序看运气"。
 */
int nextRegisterOrder() {
    static int counter = 0;
    return ++counter;
}

/** 设计坐标 → 设备像素的倍率；展示倍率非法时退回 1（`SubmitToRenderer` 用）。 */
float magnify(const InkingWidget& node) {
    const float value = node.GetMagnification();
    return value > 0.0f ? value : 1.0f;
}


/**
 * 层级下限：低于背景层的 z 一律夹到背景层。
 *
 * 绘制是从 z 低到高提交的，"画得最晚的盖在最上面"。如果允许 z 任意低，
 * 一块铺满整屏的背景板只要 z 比某个组件还低、又恰好排在它后面画，
 * 就会把那个组件整个盖掉——而且这种情况下"谁盖谁"完全取决于注册序号，
 * 属于最难查的一类错。夹住下限，背景层之下的空间就不存在了。
 */
int clampZIndex(int zIndex) {
    return zIndex < InkingZIndex::background ? InkingZIndex::background : zIndex;
}

/**
 * 一条父链有多长（自己是根时为 0）。
 *
 * `IsAbove` 要做"从根到自己的 z 序列按字典序比较"，第一步就是把两个节点
 * 抬到同一深度——只有同深度的两个节点才可能在某一层成为兄弟。深度这里现算：
 * 每次比较多两次上溯，但排序只在骨架脏的那一帧跑一次（1000 个节点约一万次
 * 比较、深度 3~5，量级 100 µs），比给每个节点多维护一个 `_treeDepth` 字段、
 * 还要在换父级时递归重算，代价小得多。
 */
int ChainDepth(const InkingWidget& node) noexcept {
    int depth = 0;
    for (const InkingWidget* p = node.GetParent(); p != nullptr;
         p = p->GetParent()) {
        ++depth;
    }
    return depth;
}

}  // namespace

// `detail::FillDefaultRect`（第 0 层渲染：一块实心矩形）的定义在
// `src/detail/InkingGeometry.cpp`——它要用渲染层的 `fillShape`，而几何那一份
// 已经依赖渲染层；放在这里会让本文件（节点类）也背上那个依赖。

// ---------------------------------------------------------------------------
// 构造 / 析构
// ---------------------------------------------------------------------------

/// 把自己登记进父级的子表（T-7）。顺序 = 注册先后（同 z 时"后构造的在上"）。
void InkingWidget::AttachToParentChildren() noexcept {
    if (_parent != nullptr) {
        GetParent()->_children.push_back(this);
    }
}

/// 从父级的子表里摘掉自己（析构、搬父级时用）。
void InkingWidget::DetachFromParentChildren() noexcept {
    if (_parent == nullptr) {
        return;
    }
    std::vector<InkingWidget*>& siblings = GetParent()->_children;
    for (std::size_t i = 0; i < siblings.size(); ++i) {
        if (siblings[i] == this) {
            siblings.erase(siblings.begin()
                           + static_cast<std::ptrdiff_t>(i));
            return;
        }
    }
}

InkingWidget::InkingWidget(InkingWidget* parent, const std::string& name,
                           bool dynamic)
    // 基类先跑：定位字段（锚点 / 偏移 / 尺寸 / 可见性）与 `_parent` 都由它持有。
    // 配置驱动那条路的定位数据默认全零（`AnchorData` 的默认值），由子类读好
    // CXXCSS 之后再配自己的字段。
    : InkingAnchor(parent, AnchorData{}),
      _name(name),
      _registerOrder(nextRegisterOrder()),
      _dynamic(dynamic) {
    // 先记住自己在哪个场景（沿父链找），再向场景登记。
    // 这一步是 O(深度)，而且只在构造时付一次，不是每帧。
    AttachToParentChildren();
    RegisterToScene(GetSceneAncestor());
}

InkingWidget::InkingWidget(InkingWidget* parent, const AnchorData& data,
                           bool dynamic)
    // 定位那半段交给基类（它就是为这件事存在的）；本层只接自己那半段。
    : InkingAnchor(parent, data),
      _layout(data.layout),
      _layoutItem(data.layoutItem),
      _zIndex(clampZIndex(data.zIndex)),
      _registerOrder(nextRegisterOrder()),
      // 顺序按**声明顺序**写：`_dynamic` 在头文件里声明在 `_color` 之前，
      // 初始化列表里写反了编译器会按声明顺序初始化，并给一条 -Wreorder
      // （读代码的人会以为写在前面的先初始化）。
      _dynamic(dynamic),
      _color(data.color) {
    AttachToParentChildren();
    RegisterToScene(GetSceneAncestor());
}

InkingWidget::~InkingWidget() {
    // **拥有的子节点先走**（A10）：它们的析构体会访问本节点的 `_children`
    // （把自己摘掉）与 `_scene`（从绘制列表摘掉），而此刻两者都还活着
    // ——本类析构体还没走完，两个成员表也还没到析构的时候。
    // 反过来（让成员表自动析构）也能work（声明顺序保证了顺序），但显式写出来
    // 才能让"谁先谁后"这件事在代码里看得见。
    _ownedChildren.clear();

    // 先从父级的子表里摘掉自己（T-7 的"父 → 子列表"）：留着一个已经析构的
    // 指针，父级的变换就会去动一块已经没了的内存。
    DetachFromParentChildren();

    // 从所属场景的绘制列表里摘掉自己。
    //
    // 这一步不能省：场景的绘制列表存的是裸指针，节点销毁后如果还留在列表里，
    // 就是一份野指针，下一帧提交时直接崩（docs/InputDesign.md §8 那条
    // "生命周期成对处理"说的就是这个）。场景已经先销毁时 _scene 已经被摘成
    // nullptr，这里是空操作。
    UnregisterFromScene();
}

// ---------------------------------------------------------------------------
// 只读查询
// ---------------------------------------------------------------------------

const std::string& InkingWidget::GetName() const noexcept {
    return _name;
}

InkingWidget* InkingWidget::GetParent() const noexcept {
    // 树语义视图 = 同一条 `_parent` 的**一次定型转换**：一份存储、两个视图，
    // 所以不存在"两个父指针要同步"的问题（`SKILL.md` 反对的正是"多一份要被
    // 同步的真相"）。基类那条指针是 `InkingAnchor*`——它只服务位置推导，
    // **不认识树**，这是定位层的边界。
    //
    // 转换成立的前提是"**所有**锚点都是 Widget"：基类构造函数是 protected、
    // 不可直接实例化（组件必须选一档），而 `_parent` 的唯一写入点是本类的
    // 构造函数与 `SetParent`，两者收的都是 `InkingWidget*`。所以这是一次
    // **结构性的**定型，不是"猜类型"——别在这里加 `dynamic_cast` 的路子。
    return static_cast<InkingWidget*>(_parent);
}

const std::vector<InkingWidget*>& InkingWidget::GetChildren() const noexcept {
    return _children;
}

TransformSpec InkingWidget::GetClipTransform() const noexcept {
    // 默认：裁剪跟着和内容**同一份**变换——没有滚动这类"只动内容不动窗口"的
    // 通道时，两个语义就是同一个（滚动容器才需要把它们分开，见头文件）。
    return GetSubtreeTransform();
}

// 几何辅助（`AncestorAffine` / `SubtreeChainAffine` / `ClipChainAffine` /
// `AncestorClipInDevice` / `SubtreeHitEnvelope` / `ClipBoundsInDevice`）搬到了
// `src/detail/InkingGeometry.cpp`——它们不属于这个类，塞在这里会让"谁负责
// 几何换算"变得看不见。

// `GetSelfAnchor` / `GetTraceAnchor` / `GetOffsetX` / `GetOffsetY` 的实现在**基类**
// `InkingAnchor` 上（它们的字段已经由基类持有），这里不再重复实现——重复会让
// 同一件事有两个入口（"双份真相"，AGENTS §6 第 40 条那一族）。

// 尺寸的对外入口：基类给了默认实现（返回推导位置用的 `_width`），这里重写只是
// 给"每态尺寸"那一档（T-5）留一个明确落点。**声明里带 `override`**（见头文件）——
// 漏了就变成"隐藏"而不是"重写"，通过基类指针调用会静默拿到基类那份推导尺寸。
int InkingWidget::GetWidth() const noexcept {
    return _width;
}

int InkingWidget::GetHeight() const noexcept {
    return _height;
}

ShapeBounds InkingWidget::GetSubtreeClipBounds() const noexcept {
    // 默认就是本体整块矩形——绝大多数"剪子孙"的容器要的正是它
    // （下拉 / 滚动 / 抽屉的窗口就是自己那一块）。
    return ShapeBounds{0.0f, 0.0f, static_cast<float>(_width),
                       static_cast<float>(_height)};
}

// 自己矩形里的「自身锚点」那个点，落在父级矩形里的「上级锚点」那个点上：
//   parentW * traceAnchor.x == GetX() + ownW * selfAnchor.x
// 这条公式（连同 `GetX/Y` / `GetOffsetX/Y`）的实现在**基类** `InkingAnchor` 上。
//
// `GetAbsX/Y` 在**本层**而不是定位层：它累加的是"父级的绝对位置 + 自己相对
// 父级的位置"，而递归要沿**树语义**的父指针走（`GetParent()`）。定位层那条
// `_parent` 只是位置推导的输入、不认识"树"，也不该认识（见 `InkingAnchor.h`
// 顶上那两条边界），所以整条链的定型视图在本层，这两个 getter 跟着在本层。
// 绘制 / 命中 / 包络 / 裁剪问的都是它，四处是同一个答案。

float InkingWidget::GetAbsX() const noexcept {
    const InkingWidget* parent = GetParent();
    if (parent == nullptr) {
        return _offsetX;
    }
    // 累加的是**父级的绝对位置**与**自己相对父级的位置**。两半都按"推导用的
    // 尺寸"算（`GetX()` 走基类公式、父级那一层走它自己的 `_width`），所以
    // 位置推导与"谁画在哪儿"共用同一份锚点语义。
    return parent->GetAbsX() + GetX();
}

float InkingWidget::GetAbsY() const noexcept {
    const InkingWidget* parent = GetParent();
    if (parent == nullptr) {
        return _offsetY;
    }
    return parent->GetAbsY() + GetY();
}

int InkingWidget::GetZIndex() const noexcept {
    return _zIndex;
}

int InkingWidget::GetRegisterOrder() const noexcept {
    return _registerOrder;
}

// `IsVisible` / `IsVisibleInTree` 的实现在**基类**（它们读的 `_visible` 与
// 沿父链上溯的能力都属于定位层）。

std::uint32_t InkingWidget::GetColor() const noexcept {
    return _color;
}

// 展示倍率的**写入口** `SetMagnification` 与本层同处（它是窗口层调的、要跟
// 场景打交道），而只读查询 `GetMagnification` 在基类。

/// 设备像素尺寸按 `GetWidth()` / `GetHeight()` 算（**不是** `_width`）：
/// 重写了尺寸入口的组件（T-5 的每态尺寸）在这一层也要拿到"当前那一份"，
/// 否则"画出来是宽的、算出来是窄的"。
float InkingWidget::GetDeviceWidth() const noexcept {
    return static_cast<float>(GetWidth()) * GetMagnification();
}

float InkingWidget::GetDeviceHeight() const noexcept {
    return static_cast<float>(GetHeight()) * GetMagnification();
}

bool InkingWidget::IsDirty() const noexcept {
    return _dirty;
}

void InkingWidget::ClearDirty() noexcept {
    _dirty = false;
    _hitQuietFrames = 0;
}

bool InkingWidget::IsRepaintDirty() const noexcept {
    return _repaintDirty;
}

void InkingWidget::ClearRepaintDirty() noexcept {
    _repaintDirty = false;
    _repaintQuietFrames = 0;
}

void InkingWidget::ConsumeDirtyFrame() noexcept {
    // 帧末统一消费的**节点那一半**：只推进计时，不清"刚标过的"。
    // 顺序很关键——先把计数加一再看够不够，于是"标脏那一帧"算第 1 帧安静，
    // 连续 kXxxQuietFrames 帧没有新脏才清。
    if (_repaintDirty) {
        ++_repaintQuietFrames;
        if (_repaintQuietFrames >= kRepaintQuietFrames) {
            _repaintDirty = false;
            _repaintQuietFrames = 0;
        }
    }
    if (_dirty) {
        ++_hitQuietFrames;
        if (_hitQuietFrames >= kHitQuietFrames) {
            _dirty = false;
            _hitQuietFrames = 0;
        }
    }
}

bool InkingWidget::IsSceneRoot() const noexcept {
    // 场景 / 窗口自己就是根：它们没有父级，也不登记到别的树里。
    return _parent == nullptr;
}

bool InkingWidget::IsDynamic() const noexcept {
    return _dynamic;
}

InkingScene* InkingWidget::GetScene() const noexcept {
    return _scene;
}

bool InkingWidget::HitTest(float /*worldX*/, float /*worldY*/) const {
    // 基类不回答：它不可实例化，也没有形状。具体组件按自己的形状判定
    // （按钮走 ShapeContains，和渲染共用同一份定义）。
    return false;
}

InkingWidget* InkingWidget::HitTestNode(float worldX, float worldY) noexcept {
    // 叶子与普通组件：命中即自己。容器重写它去**深挖子树**
    // （`InkingStaticPanel::HitTestNode`，A8a）——子孙不进命中表，
    // 只有容器自己知道它们谁在点子底下。
    return HitTest(worldX, worldY) ? this : nullptr;
}

ShapeBounds InkingWidget::GetHitEnvelope() const {
    // 保守默认：整块矩形。基类不认识形状（形状是组件的事），而包络给大了
    // 只是候选多几个、不会误命中，所以这里宁可保守。
    ShapeBounds bounds;
    bounds.x = 0.0f;
    bounds.y = 0.0f;
    bounds.width = static_cast<float>(_width);
    bounds.height = static_cast<float>(_height);
    return bounds;
}

void InkingWidget::SetHitPassThrough(bool passThrough) noexcept {
    _hitPassThrough = passThrough;
}

bool InkingWidget::IsHitPassThrough() const noexcept {
    return _hitPassThrough;
}

InkingScene* InkingWidget::GetSceneAncestor() noexcept {
    // 自己就是根（场景 / 窗口）→ 没有场景祖先可登记。
    if (IsSceneRoot()) {
        return nullptr;
    }
    // 沿父链向上找第一个"由场景盖章过"的节点，也就是场景本身。
    for (InkingWidget* node = GetParent(); node != nullptr;
         node = node->GetParent()) {
        if (node->_scene != nullptr && node->IsSceneRoot()) {
            return node->_scene;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// 层级
// ---------------------------------------------------------------------------

bool InkingWidget::IsAbove(const InkingWidget& a,
                           const InkingWidget& b) noexcept {
    if (&a == &b) {
        return false;
    }

    // 「谁在上面」= 从根到自己的 (z, 注册序号) 序列按**字典序**比较。
    //
    // 以前这里是全局一条 z（z 优先、同 z 看注册序），于是"面板 z=20、里面
    // 的标签 z=1"会让标签先画、面板底色后画、整块内容被背景盖掉——而且不报错。
    // 改成按路径比较之后，z 只在**兄弟之间**生效，父容器永远先于自己的子孙
    // 画（底色在下），子孙的 z 与父级的兄弟再无关系。这就是各家用"层级只在
    // 同级生效"的那套语义，也是本头文件原注释里预留的那条路（"换成从根向下的
    // z 序列按字典序比较，调用方不用改"）。
    //
    // 实现不真的去拼两个序列（那样每次比较都要申请内存），而是：
    //   ① 先把两个节点抬到同一深度；
    //   ② 一起上溯，找到**分叉点**——分叉处的两个节点才是兄弟，只有它们需要
    //      比 z；分叉之前的部分两条链完全一样，比了也没意义；
    //   ③ 若一路上去就是同一个节点，说明一个是另一个的祖先：祖先先画，
    //      所以**深的那个在上面**（前缀短者在前，字典序也是这个规矩）。
    //
    // 这仍然是**一个严格全序**（只有一条规则，跨子树 / 跨簇都可比），所以
    // 绘制、命中表、面板子树仲裁、动静合并四处可以继续共用它，不会出现
    // "画的是 A、点到的是 B"。

    const int depthA = ChainDepth(a);
    const int depthB = ChainDepth(b);

    const InkingWidget* pa = &a;
    const InkingWidget* pb = &b;
    int da = depthA;
    int db = depthB;

    // ① 对齐深度。
    while (da > db) {
        pa = pa->GetParent();
        --da;
    }
    while (db > da) {
        pb = pb->GetParent();
        --db;
    }

    // ③ 祖孙关系：深的（后代）画得晚，所以在上面。
    if (pa == pb) {
        return depthA > depthB;
    }

    // ② 同步上溯到分叉点。
    for (;;) {
        const InkingWidget* qa = pa->GetParent();
        const InkingWidget* qb = pb->GetParent();
        if (qa == qb) {
            break;  // 分叉：pa / pb 是兄弟（两个根时同为 nullptr，也走这里）
        }
        if (qa == nullptr || qb == nullptr) {
            // 两个节点不在同一棵树里（跨场景 / 父链被手工截断）。正常路径
            // 到不了这里；到了就退化成"整个根的全局比较"，至少不读空指针。
            return (pa->_zIndex != pb->_zIndex)
                ? pa->_zIndex > pb->_zIndex
                : pa->_registerOrder > pb->_registerOrder;
        }
        pa = qa;
        pb = qb;
    }

    // ④ 兄弟之间：z 大的在上；同 z 看注册序号（后挂的在上）。
    if (pa->_zIndex != pb->_zIndex) {
        return pa->_zIndex > pb->_zIndex;
    }
    return pa->_registerOrder > pb->_registerOrder;
}

void InkingWidget::MarkDirty() noexcept {
    _dirty = true;
    // 新脏一到，安静帧计数归零：清脏看的是"**连续**多少帧没有新脏"。
    _hitQuietFrames = 0;
    onDirty();
}

void InkingWidget::MarkRepaintDirty() noexcept {
    _repaintDirty = true;
    _repaintQuietFrames = 0;
}

void InkingWidget::MarkStructureChanged() noexcept {
    // 骨架一变，三件事同时成立：
    //   1. 命中结果可能变（几何 / 顺序 / 可见性都影响）→ 命中脏；
    //   2. 画面一定变 → 重绘脏；
    //   3. 绘制列表里缓存的绝对坐标快照过期 → 通知场景重排。
    //
    // 第 3 条只在**真的改了**的时候才该发生，所以写入口都是"先判有没有变、
    // 变了才调这里"，而不是反过来（`Resize` 传同一个尺寸必须什么都不做）。
    MarkDirty();
    MarkRepaintDirty();
    NotifySceneStructureChanged();
}

void InkingWidget::NotifySceneStructureChanged() noexcept {
    if (_scene != nullptr) {
        _scene->NotifyStructureChanged();
    }
}

// ---------------------------------------------------------------------------
// 布局（A6）
// ---------------------------------------------------------------------------

namespace {
/// 设计坐标里的尺寸取整：**四舍五入到最近的整数**，并且保证"占了格子就有像素"。
int RoundSize(float value) {
    if (value <= 0.0f) {
        return 0;
    }
    return static_cast<int>(value + 0.5f);
}
}  // namespace

LayoutSize InkingWidget::MeasureLayout(const LayoutConstraints& /*constraints*/) const {
    // 默认：我"想要"的就是我现在的大小。
    //
    // 内容驱动的组件（label 按文字撑高、img 按原始尺寸）会重写这个钩子——
    // 它们去问文字层 / 纹理层，而**不是**看自己的 `_width`（那个可能正是上一次
    // 布局给的结果，问它就成了自己证明自己）。
    return LayoutSize{static_cast<float>(_width), static_cast<float>(_height)};
}

bool InkingWidget::ApplyLayoutRect(const LayoutRect& rect) noexcept {
    // 目标：让"我的矩形（相对父级左上角）"正好是 rect。
    //
    // 位置由锚点公式给出：GetX() = parentW × trace.x − w × self.x + offsetX。
    // 布局给的是**绝对**的左上角，所以这里把锚点归零（布局里锚点不参与定位，
    // 见 `InkingLayout.h` 头顶第 3 条），于是 GetX() 落在 offsetX 上——干净、
    // 且不受"父级尺寸变了"的影响（锚点归零后公式里没有 parentW 项）。
    const int width = RoundSize(rect.width);
    const int height = RoundSize(rect.height);
    const float offsetX = rect.x;
    const float offsetY = rect.y;

    const bool sameSize = width == _width && height == _height;
    const bool sameAnchor = _selfAnchor.x == 0.0f && _selfAnchor.y == 0.0f
                         && _traceAnchor.x == 0.0f && _traceAnchor.y == 0.0f;
    const bool sameOffset = offsetX == _offsetX && offsetY == _offsetY;
    if (sameSize && sameAnchor && sameOffset) {
        return false;  // 一模一样：**一个脏都不标**（重复排不白干）
    }

    _selfAnchor = AnchorPoint{0.0f, 0.0f};
    _traceAnchor = AnchorPoint{0.0f, 0.0f};
    _offsetX = offsetX;
    _offsetY = offsetY;
    _width = width;
    _height = height;

    // 尺寸 / 偏移 / 锚点都变了，按"骨架变化"一条路走：命中脏 + 重绘脏 +
    // 通知场景重排（绘制列表里缓存的绝对坐标快照也过期了）。
    MarkStructureChanged();
    if (!sameSize) {
        onSizeChanged();
    }
    if (!sameAnchor) {
        onSelfAnchorChanged();
        onTraceAnchorChanged();
    }
    if (!sameOffset) {
        onOffsetChanged();
    }
    return true;
}

void InkingWidget::Relayout() noexcept {
    // 同步排一次：算完几何立刻生效（"改完内容马上调"就能马上看到正确的位置），
    // 脏由 `ApplyLayoutRect` 内部按"真的变了才标"处理。
    detail::layout::ArrangeSubtree(*this);
}

void InkingWidget::RelayoutParent() noexcept {
    // 往上游找第一个"声明了布局"的祖先。**不含自己**：内容变了要重排的是
    // 装我的那个容器，我自己那棵子树由它替我排。
    for (InkingWidget* node = GetParent(); node != nullptr;
         node = node->GetParent()) {
        if (node->GetLayoutSpec().Valid()) {
            node->Relayout();
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// 渲染树的登记 / 摘除
// ---------------------------------------------------------------------------

void InkingWidget::RegisterToScene(InkingScene* scene) noexcept {
    if (scene == nullptr) {
        return;
    }
    _scene = scene;
    scene->AddDrawNode(this);
}

void InkingWidget::RebindScene(InkingScene* scene) noexcept {
    RegisterToScene(scene);
}

void InkingWidget::UnregisterFromScene() noexcept {
    if (_scene == nullptr) {
        return;
    }
    // 先把指针存下来：摘除之后 _scene 就该是空的。
    InkingScene* scene = _scene;
    _scene = nullptr;
    scene->RemoveDrawNode(this);
}

void InkingWidget::DetachFromScene() noexcept {
    _scene = nullptr;
}

void InkingWidget::MarkAsSceneRoot(InkingScene* scene) noexcept {
    _scene = scene;
}

void InkingWidget::DetachSceneFromSubtree() noexcept {
    // 顺着绘制列表走：场景在析构前先把自己从列表里每个节点的 _scene 上摘掉，
    // 列表本身随场景一起销毁。走列表而不是走子表（T-7 加的 `_children`）是
    // 有意的——列表里的裸指针有可能是**正在析构的兄弟节点**，那一步只能比
    // 指针、不能解引用，而子表遍历同样要解引用。
    if (_scene != nullptr) {
        _scene->DetachSceneFromNodes();
    }
}

// ---------------------------------------------------------------------------
bool InkingWidget::SetVisible(bool visible) noexcept {
    if (_visible == visible) {
        return false;
    }
    _visible = visible;
    // 可见性是要标脏的四件事之一；它同时改变绘制列表的内容，
    // 所以还要通知场景重排（场景在重建时会把隐藏子树整个摘掉）。
    MarkStructureChanged();
    onVisibleChanged(visible);
    return true;
}

bool InkingWidget::ChangeZIndex(int zIndex) noexcept {
    // 夹住下限，理由见 clampZIndex：不然背景板会盖掉一切。
    const int clamped = clampZIndex(zIndex);
    if (_zIndex == clamped) {
        return false;
    }
    _zIndex = clamped;
    // 顺序变了（列表内容没变，但是"谁盖谁"和命中顺序都变了）→ 骨架变化。
    MarkStructureChanged();
    onZIndexChanged(clamped);
    return true;
}

bool InkingWidget::SetParent(InkingWidget* parent) noexcept {
    if (_parent == parent) {
        return false;
    }

    // 先摘掉旧登记——必须在改 _parent 之前做：摘除要按**旧的**场景归属来，
    // 改完 _parent 之后 GetSceneAncestor() 给的已经是新场景了。
    UnregisterFromScene();

    // 子表也要跟着搬家（T-7）：从旧父级摘、往新父级挂。
    DetachFromParentChildren();

    // 再换父级并重新算归属：换父级可能跨场景，所以整条链路重来一遍。
    _parent = parent;
    AttachToParentChildren();
    RegisterToScene(GetSceneAncestor());

    // 换父级可能跨场景，位置也跟着变——骨架变化里最重的一种。
    // （上面两步的摘除 / 登记已经各自通知过场景了，这里再走一次统一出口，
    //  把命中脏与重绘脏也一起标上，免得"只通知了场景、没标脏"。）
    MarkStructureChanged();
    onParentChanged(parent);
    return true;
}

// ---------------------------------------------------------------------------
// 运行期增删子节点（A10）：拥有型子表
// ---------------------------------------------------------------------------
//
// 这一节的实现只有一条核心纪律：**所有权与父子链必须一起动**。
// "谁拥有谁"错了不会崩在当场，而是崩在很久以后（父级先死、子级还在被用），
// 所以每一步的顺序都写在注释里。

InkingWidget* InkingWidget::AddChild(std::unique_ptr<InkingWidget> child) {
    if (child == nullptr) {
        return nullptr;
    }
    InkingWidget* raw = child.get();

    // 先把所有权记下来，再挂父级：`SetParent` 会沿父链把它登记进场景，
    // 而"它归谁"这条信息在登记过程中就该是确定的——顺序反过来的话，
    // 登记里万一出事（跨场景、回调里再 Add），它就成了没人认领的节点。
    _ownedChildren.push_back(std::move(child));

    // 已经是我的子节点时 `SetParent` 是幂等的（返回 false），所有权照样记上了
    // ——"先 make_unique 时传了 parent、再 AddChild"这种写法也支持。
    raw->SetParent(this);
    return raw;
}

std::unique_ptr<InkingWidget> InkingWidget::RemoveChild(InkingWidget* child) {
    if (child == nullptr) {
        return nullptr;
    }
    for (auto it = _ownedChildren.begin(); it != _ownedChildren.end(); ++it) {
        if (it->get() != child) {
            continue;
        }
        std::unique_ptr<InkingWidget> taken = std::move(*it);
        _ownedChildren.erase(it);

        // 脱离渲染树：从本节点的子表摘掉、从场景的绘制列表摘掉、标结构脏。
        // 走 `SetParent(nullptr)` 这条统一的路（而不是靠析构体兜底）：这样
        // "摘下来但还活着"的节点也不会留在树里——既不被画、也不参与命中。
        taken->SetParent(nullptr);
        return taken;
    }
    return nullptr;  // 不在拥有表里（借用型的子节点走不到这儿）
}

std::size_t InkingWidget::ClearChildren() noexcept {
    const std::size_t count = _ownedChildren.size();
    while (!_ownedChildren.empty()) {
        std::unique_ptr<InkingWidget> item = std::move(_ownedChildren.back());
        _ownedChildren.pop_back();
        // 先摘干净再销毁：它的析构体因此不需要回头找父级（`_parent` 已经是
        // nullptr），"脱离渲染树"这件事也就只走了这一条路。
        item->SetParent(nullptr);
        item.reset();
    }
    return count;
}

std::size_t InkingWidget::GetOwnedChildCount() const noexcept {
    return _ownedChildren.size();
}

void InkingWidget::SetMagnification(float magnification) noexcept {
    // 只影响绘制换算，不影响标脏
    if (magnification > 0.0f) {
        _magnification = magnification;
    }
}

// ---------------------------------------------------------------------------
// 运行期显示变换（A13）
// ---------------------------------------------------------------------------

bool InkingWidget::AcceptsExternalTransform() const noexcept {
    // 默认**不认**：框架给的通道一律"默认不要、声明才有"（照
    // `WantsKeyboardFocus()` 那条先例）。默认开启的话，"某块东西莫名被挪走 /
    // 缩放"就会变成难查的问题；拒绝是当场可断言的。
    return false;
}

const TransformSpec& InkingWidget::GetExternalTransform() const noexcept {
    return _displayTransform;
}

TransformSpec InkingWidget::CombineWithExternal(
    const TransformSpec& own) const noexcept {
    if (_displayTransform.IsIdentity()) {
        return own;  // 没开这条通道：原样返回（绝大多数组件走这里，零开销）
    }
    if (own.IsIdentity()) {
        return _displayTransform;
    }

    // 两个变换都绕**同一个中心**（组件中心，见 InkingTransform.h 的语义），
    // 所以复合有闭式解：
    //     Apply(t, p) = c + s·R(θ)·(p − c) + t
    //     外(内(p))   = c + (se·si)·R(θe+θi)·(p − c) + (se·R(θe)·ti + te)
    // 于是：scale = se·si、rotate = θe + θi、
    //       translate = se·R(θe)·内.translate + 外.translate。
    // 自检里用"逐点依次施加两个变换"对账这条公式，别只信这里的推导。
    const float radians = _displayTransform.rotate * detail::kTransformDegToRad;
    const float cosA = std::cos(radians);
    const float sinA = std::sin(radians);

    TransformSpec out;
    out.scale = _displayTransform.scale * own.scale;
    out.rotate = _displayTransform.rotate + own.rotate;
    out.translateX = _displayTransform.scale
                         * (own.translateX * cosA - own.translateY * sinA)
                   + _displayTransform.translateX;
    out.translateY = _displayTransform.scale
                         * (own.translateX * sinA + own.translateY * cosA)
                   + _displayTransform.translateY;
    return out;
}

bool InkingWidget::SetDisplayTransform(const TransformSpec& transform) noexcept {
    if (!AcceptsExternalTransform()) {
        ++_displayTransformRejected;
        INK_LOG_ERROR(std::string("InkingWidget"),
                      "这个组件没有声明 AcceptsExternalTransform()，"
                      "SetDisplayTransform 被拒绝（组件：" + _name + "）");
        return false;
    }
    if (SameTransform(_displayTransform, transform)) {
        return false;  // 幂等：一个脏都不标
    }

    _displayTransform = transform;
    ++_displayTransformRevision;

    // **标结构脏**（不是只标重绘 + 命中脏）。
    //
    // 为什么：命中表的重建挂在 `InkingScene::sortDrawList()` 上，而它只在
    // **结构脏**时重排绘制列表 + 重烘表（`InkingScene.cpp` 里那一段；命中脏
    // 那边只管"还有几个节点在动"的统计）。而任意变换是**无界**的——包络并集
    // 算不出来 ⇒ 想让"拖走的组件仍然点得到"，只能让表跟着重烘一次。
    //
    // 这是框架里**唯一**一条会动表的显示通道。三态过渡与容器开合都只标
    // 重绘 + 命中脏、表保持冻结（`AGENTS.md` §6 第 54 条那条设计没被破）。
    // 代价说清楚：拖动期间**每一帧**都会重排绘制列表 + 重烘命中表，
    // `GetStructureGeneration()` 也会跟着涨——它是"表被重烘过"的如实记账。
    MarkStructureChanged();
    return true;
}

bool InkingWidget::ClearDisplayTransform() noexcept {
    if (_displayTransform.IsIdentity()) {
        return false;  // 本来就是单位：幂等，也不必惊动"拒绝"那条路
    }
    return SetDisplayTransform(TransformSpec{});
}

std::uint64_t InkingWidget::GetDisplayTransformRevision() const noexcept {
    return _displayTransformRevision;
}

std::uint64_t InkingWidget::GetDisplayTransformRejectedCount() const noexcept {
    return _displayTransformRejected;
}

// ---------------------------------------------------------------------------
// [√] 可重写钩子：基类里都是空的，子类按需重写
// ---------------------------------------------------------------------------

void InkingWidget::onSizeChanged() {}
void InkingWidget::onSelfAnchorChanged() {}
void InkingWidget::onTraceAnchorChanged() {}
void InkingWidget::onOffsetChanged() {}
void InkingWidget::onZIndexChanged(int /*zIndex*/) {}
void InkingWidget::onParentChanged(InkingWidget* /*parent*/) {}
void InkingWidget::onVisibleChanged(bool /*visible*/) {}
void InkingWidget::onDirty() {}

// 输入钩子（指针 5 个 + 键盘 5 个）的**默认空实现**在两个接口里：
//   - `include/input/InkingPointerHandler.h`
//   - `include/input/InkingKeyboardTarget.h`
//
// "什么都不做"是**正确的默认值**，不是占位：绝大多数节点（背景板、装饰、纯展示
// 组件）本来就不该对输入有反应。有了它，场景的派发可以无条件地推给任何被命中的
// 节点，不必先问"你是不是按钮"——那正是 A1 消掉的那条
// `static_cast<InkingStaticButton*>`。
//
// 所以本文件**不再实现它们**：接口给默认实现，重写才产生行为。

void InkingWidget::onAnimationTick(float /*deltaSeconds*/) {
    // 基类没有动画：几何变化归动态档的 Tick，颜色过渡归各组件自己。
    // 留这个空实现是为了让"每渲染帧一次"这条通道两档都能走。
}

void InkingWidget::onRender(float /*pixelX*/, float /*pixelY*/) const {
    // 基类不画：具体画什么由两个具体锚点或它们的子类决定。
}

void InkingWidget::SubmitToRenderer(SDL_Renderer* renderer, float absX,
                                    float absY) const {
    if (renderer == nullptr) {
        return;
    }

    // 设计坐标 → 设备像素，然后交给节点自己决定画成什么样。
    // 顺带把渲染器挂到"当前渲染器"上，好让 onRender 不用多接一个参数。
    const float scale = magnify(*this);
    detail::currentRendererSlot() = renderer;

    // 祖先子树变换（T-7）：容器的变换要作用到整棵子树。算出来挂在槽上，
    // 形状 / 文字 / 图片都用它——组件侧依旧一个字不用改。
    // 仿射是**设计坐标**的，这里把平移量乘上展示倍率，换到设备像素。
    {
        Affine ancestor = detail::AncestorAffine(*this);
        ancestor.tx *= scale;
        ancestor.ty *= scale;
        detail::currentAncestorAffineSlot() = ancestor;
    }
    // 顶点缓存槽：懒创建，然后挂成"当前节点"——`detail::fillShape` 靠它知道
    // 这一批顶点是谁的、该复用的还是重算（T-6）。组件侧一个字都不用改。
    if (_vertexCache == nullptr) {
        _vertexCache = std::make_unique<detail::VertexCacheSlot>();
    }
    detail::SetCurrentNode(_vertexCache.get());
    onRender(absX * scale, absY * scale);
    detail::SetCurrentNode(nullptr);
    detail::currentAncestorAffineSlot() = Affine{};
    detail::currentRendererSlot() = nullptr;
}

// ---------------------------------------------------------------------------
// 滚轮归属（B12 的滚动容器）
// ---------------------------------------------------------------------------
//
// 只有"沿父链找"这一步在这里：接口（`InkingPointerHandler`）给的是"我要不要
// 滚轮"那半个契约，而这一半要用树结构。场景拿到结果之后把滚轮**先**交给它。

InkingWidget* InkingWidget::FindSubtreeWheelOwner() noexcept {
    for (InkingWidget* node = this; node != nullptr; node = node->GetParent()) {
        if (node->HandlesSubtreeWheel()) {
            return node;
        }
    }
    return nullptr;
}

}  // namespace inking
