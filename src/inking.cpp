#include <inking.h>

// 几个组件的公开头在这里被 include 一次：**新公开头如果不被任何 .cpp 或自检
// include，它就是"永远编译不过的死文件"**，而构建照样全绿（这个坑踩过——
// include/scene/InkingScene.h 曾经藏了很久）。
#include <asset/InkingAsset.h>
#include <button/ButtonLook.h>
#include <button/ButtonPaint.h>
#include <button/InkingDynamicButton.h>
#include <button/InkingStaticButton.h>
#include <canvas/CanvasContext.h>
#include <canvas/InkingCanvasPainter.h>
#include <canvas/InkingStaticCanvas.h>
#include <image/InkingSprite.h>
#include <image/InkingStaticImage.h>
#include <image/SpriteData.h>
#include <video/InkingVideo.h>
#include <event/InkingEvent.h>
#include <input/KeyboardInput.h>
#include <panel/InkingStaticPanel.h>
#include <prefab/FilePicker.h>
#include <prefab/ListView.h>
#include <prefab/MenuBar.h>
#include <prefab/OptionBar.h>
#include <prefab/Select.h>
#include <text/InkingStaticLabel.h>

#include <SDL3/SDL.h>

#include <type_traits>
#include <utility>

namespace inking {

// ---------------------------------------------------------------------------
// 编译期把两条设计约束钉住，别让后来的改动悄悄把它们拆掉
//
// 1. **静态组件没有几何写入口**（InkingWidget.h 末尾那段）。这不是"约定没写"，
//    是类型上的硬保证：连命中表里存的 InkingWidget* 都改不了几何。
// 2. **按钮是静态组件**。三态只换颜色 → 不标脏 → 能进命中表；哪天有人给
//    Button 加了 Resize，下面那条断言会当场把构建打红，而不是等到
//    "画的是圆角、点到的是直角"才发现。
//
// 检测写法说明：不能直接写 `requires(Button& b) { b.Resize(1,1); }`。
// 那个表达式里没有模板参数，是**非依赖**的，成员不存在时不会走"约束不满足"
// 这条替换失败路径，而是当场报"no member named Resize"——编译照样红，
// 但报错信息指向断言本身而不是"有人加了写入口"。所以按标准 detection idiom
// 做成模板：不满足约束时重载被丢弃（替换失败不是错误），
// HasResize<Button> 直接是 false，断言才真的在测"没有"。
// ---------------------------------------------------------------------------

template <typename T>
concept HasResize = requires(T& node) { node.Resize(1, 1); };

template <typename T>
concept HasChangeSelfAnchor =
    requires(T& node) { node.ChangeSelfAnchor(AnchorPoint{0.5f, 0.5f}); };

template <typename T>
concept HasChangeTraceAnchor =
    requires(T& node) { node.ChangeTraceAnchor(AnchorPoint{0.5f, 0.5f}); };

template <typename T>
concept HasChangeOffset =
    requires(T& node) { node.ChangeOffset(1.0f, 1.0f); };

template <typename T>
concept HasSetVisible = requires(T& node) { node.SetVisible(true); };

template <typename T>
concept HasChangeZIndex = requires(T& node) { node.ChangeZIndex(1); };

/// A13：运行期显示变换的写入口（在**基类**上，所有组件都能调）。
template <typename T>
concept HasSetDisplayTransform =
    requires(T& node) { node.SetDisplayTransform(TransformSpec{}); };

static_assert(std::is_base_of_v<InkingStaticWidget, InkingStaticButton>,
              "Button 是静态组件：三态只换颜色，几何构造即定型");
static_assert(!std::is_base_of_v<InkingDynamicWidget, InkingStaticButton>,
              "Button 不该同时是动态组件：静态 / 动态是二选一，不能回头改");

static_assert(!HasResize<InkingStaticButton>, "静态组件不该有 Resize");
static_assert(!HasChangeSelfAnchor<InkingStaticButton>,
              "静态组件不该有 ChangeSelfAnchor");
static_assert(!HasChangeTraceAnchor<InkingStaticButton>,
              "静态组件不该有 ChangeTraceAnchor");
static_assert(!HasChangeOffset<InkingStaticButton>,
              "静态组件不该有 ChangeOffset");

// 反面：骨架变化两版共用，静态组件必须有，别在"去掉几何写入口"时连它们一起删了。
static_assert(HasSetVisible<InkingStaticButton>,
              "可见性是骨架变化，两版都该有");
static_assert(HasChangeZIndex<InkingStaticButton>,
              "层级是骨架变化，两版都该有");

// ---------------------------------------------------------------------------
// 动态档：同一套断言镜像一份
//
// 动态按钮（InkingDynamicButton）与静态按钮是**同一个组件的两档**，
// 判据反过来：它必须有几何写入口（几何会自己变，那是它存在的理由），
// 而且两个档位二选一、不能同时是。
//
// 这几条不只是"描述现状"：哪天有人为了让静态按钮"按下时缩一下"而把
// InkingDynamicButton 的几何写入口搬进公共基类，上面的
// `!HasResize<InkingStaticButton>` 会当场把构建打红，而这里会说明
// 那条路本来该走哪个类型。
// ---------------------------------------------------------------------------

static_assert(std::is_base_of_v<InkingDynamicWidget, InkingDynamicButton>,
              "动态按钮属于动态档：几何会自己变，所以不进命中表");
static_assert(!std::is_base_of_v<InkingStaticWidget, InkingDynamicButton>,
              "静态 / 动态是二选一，不能既进表又不进表");

static_assert(HasResize<InkingDynamicButton>,
              "动态组件必须有 Resize（几何写入口是这一档存在的理由）");
static_assert(HasChangeSelfAnchor<InkingDynamicButton>,
              "动态组件必须有 ChangeSelfAnchor");
static_assert(HasChangeTraceAnchor<InkingDynamicButton>,
              "动态组件必须有 ChangeTraceAnchor");
static_assert(HasChangeOffset<InkingDynamicButton>,
              "动态组件必须有 ChangeOffset");
static_assert(HasSetVisible<InkingDynamicButton>,
              "可见性是骨架变化，两版都该有");
static_assert(HasChangeZIndex<InkingDynamicButton>,
              "层级是骨架变化，两版都该有");

// 两块按钮共用同一份三态外观与状态机（ButtonLook.h），不能各写一份：
// 两份实现各自演化就会出现"静态按钮按下去和动态按钮长得不一样"。
// 这里钉住"两边问出来的三态是同一个类型"——谁另起一套 ButtonState 都会红。
static_assert(std::is_same_v<decltype(std::declval<const InkingStaticButton&>()
                                          .GetState()),
                             ButtonState>,
              "静态按钮的三态来自共用的 ButtonState");
static_assert(std::is_same_v<decltype(std::declval<const InkingDynamicButton&>()
                                          .GetState()),
                             ButtonState>,
              "动态按钮的三态也来自共用的 ButtonState");

// ---------------------------------------------------------------------------
// Panel（A1）：第一个"整块在动"的容器
//
// 和按钮同一套镜像断言：它也是**静态档**（开合只改变换，几何构造即定型），
// 所以不许出现几何写入口。哪天有人为了"面板开合顺便改个尺寸"把 Resize 塞进来，
// 下面这几条会当场把构建打红——那条路要走的类型是动态档。
// ---------------------------------------------------------------------------

static_assert(std::is_base_of_v<InkingStaticWidget, InkingStaticPanel>,
              "Panel 是静态组件：开合只改变换与颜色，几何构造即定型");
static_assert(!std::is_base_of_v<InkingDynamicWidget, InkingStaticPanel>,
              "静态 / 动态是二选一，不能既进表又不进表");

static_assert(!HasResize<InkingStaticPanel>, "静态组件不该有 Resize");
static_assert(!HasChangeSelfAnchor<InkingStaticPanel>,
              "静态组件不该有 ChangeSelfAnchor");
static_assert(!HasChangeTraceAnchor<InkingStaticPanel>,
              "静态组件不该有 ChangeTraceAnchor");
static_assert(!HasChangeOffset<InkingStaticPanel>,
              "静态组件不该有 ChangeOffset");

static_assert(HasSetVisible<InkingStaticPanel>,
              "可见性是骨架变化，两版都该有");
static_assert(HasChangeZIndex<InkingStaticPanel>,
              "层级是骨架变化，两版都该有");

// 容器的两个关键重写是**虚函数**，静态断言查不到"有没有重写"（基类也有同名
// 成员）。它们的正确性由运行期自检钉住（源项目 tests/test_panel.cpp，
// 该套件未搬入本仓库 ⇒ 这一项目前**没有**等价断言）：
//   - `GetSubtreeTransform` 返回非单位变换时，子节点绘制要跟着动；
//   - `OwnsSubtreeHit` 为 true 时，子孙不进命中表、而容器仍能被点到。

// ---------------------------------------------------------------------------
// 预制件（C 系列）：**用现有配件组装**出来的可复用件
//
// 它们不是"新组件"——没有新的绘制 / 命中 / 布局代码，全是 Panel 的派生类：
// 容器的五条重写一条都不用再写（子树变换 / 子树命中 / 命中深挖 / 包络 / 裁剪）。
// 所以这里钉的是"它们确实是静态档、确实没有几何写入口"——和 Panel 同一条铁律，
// 哪天有人为了"选项多了顺便改个高"把 Resize 塞进来，构建当场打红。
// ---------------------------------------------------------------------------

static_assert(std::is_base_of_v<InkingStaticWidget, prefab::OptionBar>,
              "选项栏是预制件（静态档）：项的变化走重建 + 布局，不走几何写入口");
static_assert(!std::is_base_of_v<InkingDynamicWidget, prefab::OptionBar>,
              "静态 / 动态是二选一，不能既进表又不进表");
static_assert(!HasResize<prefab::OptionBar>, "静态档不该有 Resize");
static_assert(!HasChangeSelfAnchor<prefab::OptionBar>,
              "静态档不该有 ChangeSelfAnchor");
static_assert(!HasChangeTraceAnchor<prefab::OptionBar>,
              "静态档不该有 ChangeTraceAnchor");
static_assert(!HasChangeOffset<prefab::OptionBar>,
              "静态档不该有 ChangeOffset");

static_assert(std::is_base_of_v<InkingStaticWidget, prefab::MenuBar>,
              "菜单栏是预制件（静态档）：拖动是**窗口**的位移，不是自己的几何");
static_assert(!std::is_base_of_v<InkingDynamicWidget, prefab::MenuBar>,
              "静态 / 动态是二选一，不能既进表又不进表");
static_assert(!HasResize<prefab::MenuBar>, "静态档不该有 Resize");
static_assert(!HasChangeOffset<prefab::MenuBar>,
              "静态档不该有 ChangeOffset——窗口拖动走 MoveWindowBy");

static_assert(std::is_base_of_v<InkingStaticWidget, prefab::ListView>,
              "列表是预制件（静态档）：行由重建 + 布局安置，几何不自己动");
static_assert(!std::is_base_of_v<InkingDynamicWidget, prefab::ListView>,
              "静态 / 动态是二选一，不能既进表又不进表");
static_assert(!HasResize<prefab::ListView>, "静态档不该有 Resize");
static_assert(!HasChangeOffset<prefab::ListView>,
              "静态档不该有 ChangeOffset——滚动走 Panel 的 scroll 段");

static_assert(std::is_base_of_v<InkingStaticWidget, prefab::Select>,
              "下拉是预制件（静态档）：展开收起只改可见性，几何构造即定型");
static_assert(!std::is_base_of_v<InkingDynamicWidget, prefab::Select>,
              "静态 / 动态是二选一，不能既进表又不进表");
static_assert(!HasResize<prefab::Select>, "静态档不该有 Resize");
static_assert(!HasChangeOffset<prefab::Select>,
              "静态档不该有 ChangeOffset——展开态的位移写在配置里");

static_assert(std::is_base_of_v<InkingStaticWidget, prefab::FilePicker>,
              "文件选择器是预制件（静态档）：四块内容的位置构造期定死");
static_assert(!std::is_base_of_v<InkingDynamicWidget, prefab::FilePicker>,
              "静态 / 动态是二选一，不能既进表又不进表");
static_assert(!HasResize<prefab::FilePicker>, "静态档不该有 Resize");
static_assert(!HasChangeOffset<prefab::FilePicker>,
              "静态档不该有 ChangeOffset——换目录是换列表内容，不是挪自己");

// ---------------------------------------------------------------------------
// Label（A7）：只读文字，按内容撑高
//
// 它也是**静态档**：文字变了只改"排版结果 + 自己的高度"，没有任何
// "用户改几何"的口子——撑高走 A6 的测量契约（`MeasureLayout`）。
// ---------------------------------------------------------------------------

static_assert(std::is_base_of_v<InkingStaticWidget, InkingStaticLabel>,
              "Label 是静态组件：文字变了重排一次，几何本身不随便动");
static_assert(!std::is_base_of_v<InkingDynamicWidget, InkingStaticLabel>,
              "静态 / 动态是二选一，不能既进表又不进表");

static_assert(!HasResize<InkingStaticLabel>, "静态组件不该有 Resize");
static_assert(!HasChangeSelfAnchor<InkingStaticLabel>,
              "静态组件不该有 ChangeSelfAnchor");
static_assert(!HasChangeTraceAnchor<InkingStaticLabel>,
              "静态组件不该有 ChangeTraceAnchor");
static_assert(!HasChangeOffset<InkingStaticLabel>,
              "静态组件不该有 ChangeOffset");
static_assert(HasSetVisible<InkingStaticLabel>,
              "可见性是骨架变化，两版都该有");
static_assert(HasChangeZIndex<InkingStaticLabel>,
              "层级是骨架变化，两版都该有");

// ---------------------------------------------------------------------------
// 通用事件槽（A9）：一个**类型**，不是基类上的一份清单
//
// 这条设计是有意的：事件带什么参数，只有组件自己知道（值 / 位置 / 旧值…）。
// 若把它做成基类上的 `std::vector<std::function<void()>>`，参数就得靠
// `std::any` 或"事件 ID + void*"绕，类型安全和"回调拿到需要的上下文"两件事
// 一起丢掉。所以落点是**模板类型**：组件声明自己的成员，框架不认识它。
//
// 断言钉住两条：它不能被拷来拷去（订阅者只该跟着一个组件走），以及回调签名
// 就是声明的参数列表（没有中间层）。
// ---------------------------------------------------------------------------

static_assert(!std::is_copy_constructible_v<InkingEvent<>>,
              "事件槽不可拷贝：它属于所在的那个组件（组件本身也不可拷贝）");
static_assert(!std::is_copy_assignable_v<InkingEvent<float>>,
              "事件槽不可赋值：同上");
static_assert(std::is_same_v<InkingEvent<float, float>::Handler,
                             std::function<void(float, float)>>,
              "事件槽的回调签名就是它声明的参数列表");

// ---------------------------------------------------------------------------
// 静态图片（A11）：不带点击回调的按钮
//
// 它和按钮 / 面板 / 标签同一档：几何构造即定型（图片的"内容变了"走的是
// `LoadAsset` / `SetImageBytes`，那只标重绘脏，不动几何）。所以断言和上面
// 那几个组件完全同构——哪天有人给图片加个 Resize，这几条会当场把构建打红。
// ---------------------------------------------------------------------------

static_assert(std::is_base_of_v<InkingStaticWidget, InkingStaticImage>,
              "图片是静态组件：几何构造即定型，换图只标重绘脏");
static_assert(!std::is_base_of_v<InkingDynamicWidget, InkingStaticImage>,
              "静态 / 动态是二选一，不能既进表又不进表");

static_assert(!HasResize<InkingStaticImage>, "静态组件不该有 Resize");
static_assert(!HasChangeSelfAnchor<InkingStaticImage>,
              "静态组件不该有 ChangeSelfAnchor");
static_assert(!HasChangeTraceAnchor<InkingStaticImage>,
              "静态组件不该有 ChangeTraceAnchor");
static_assert(!HasChangeOffset<InkingStaticImage>,
              "静态组件不该有 ChangeOffset");
static_assert(HasSetVisible<InkingStaticImage>, "可见性是骨架变化，两版都该有");
static_assert(HasChangeZIndex<InkingStaticImage>, "层级是骨架变化，两版都该有");

// 图片的"数据"是按钮那份（A11：img 就是没有 onClicked 的 button）。
// 钉住这条继承关系：哪天有人把 ImageData 改成"再抄一份字段"，这里会红——
// 那时要一起想清楚"AnchorData 加字段要同步几处"（AGENTS §6 第 83 条）。
static_assert(std::is_base_of_v<ButtonData, ImageData>,
              "ImageData 复用 ButtonData：img 是没有 onClicked 的 button");

// ---------------------------------------------------------------------------
// 帧动画（0.6.0 T-03）：**静态档**——换帧只换"用哪份字节"
//
// 钉住这条：帧与帧同尺寸，换帧不碰几何 ⇒ 命中表不用重烘。哪天有人把"换帧"
// 实现成"改几何"（那就拿到了 Resize / ChangeOffset），下面这几条会当场红——
// 而不是等"画面在动、点到的却是老位置"才发现。
static_assert(std::is_base_of_v<InkingStaticImage, InkingSprite>,
              "帧动画复用图片那条绘制：每一帧都是一张图");
static_assert(std::is_base_of_v<InkingStaticWidget, InkingSprite>,
              "帧动画是静态组件：换帧只标重绘脏");
static_assert(!std::is_base_of_v<InkingDynamicWidget, InkingSprite>,
              "静态 / 动态是二选一，不能既进表又不进表");
static_assert(!HasResize<InkingSprite>, "静态组件不该有 Resize");
static_assert(!HasChangeSelfAnchor<InkingSprite>,
              "静态组件不该有 ChangeSelfAnchor");
static_assert(!HasChangeTraceAnchor<InkingSprite>,
              "静态组件不该有 ChangeTraceAnchor");
static_assert(!HasChangeOffset<InkingSprite>, "静态组件不该有 ChangeOffset");
static_assert(HasSetVisible<InkingSprite>, "可见性是骨架变化，两版都该有");
static_assert(HasChangeZIndex<InkingSprite>, "层级是骨架变化，两版都该有");

// ---------------------------------------------------------------------------
// Canvas（A12）：一块"谁都能画"的表面
//
// 它也是**静态档**：几何构造即定型，内容变了只标重绘脏（`Invalidate`）——
// 所以断言和上面几个组件完全同构。
//
// 另外钉住两条 A12 特有的契约：
//   1. **绘制器是纯接口、虚析构必须有**（`SKILL.md` §3.4）：业务代码派生它、
//      经基类指针持有；缺虚析构就是 UB，而那种错只在销毁时才现形；
//   2. **公开绘制词汇不拖 SDL 进来**（`AGENTS.md` §3 第 2 条）：`CanvasContext`
//      里出现 SDL 的地方只有前向声明的 `SDL_Renderer*`。这条最容易被"顺手
//      include 一下"破坏，所以连"它是纯接口"一起钉在这里。
// ---------------------------------------------------------------------------

static_assert(std::is_base_of_v<InkingStaticWidget, InkingStaticCanvas>,
              "画布是静态组件：几何构造即定型，内容变了只标重绘脏");
static_assert(!std::is_base_of_v<InkingDynamicWidget, InkingStaticCanvas>,
              "静态 / 动态是二选一，不能既进表又不进表");

static_assert(!HasResize<InkingStaticCanvas>, "静态组件不该有 Resize");
static_assert(!HasChangeSelfAnchor<InkingStaticCanvas>,
              "静态组件不该有 ChangeSelfAnchor");
static_assert(!HasChangeTraceAnchor<InkingStaticCanvas>,
              "静态组件不该有 ChangeTraceAnchor");
static_assert(!HasChangeOffset<InkingStaticCanvas>,
              "静态组件不该有 ChangeOffset");
static_assert(HasSetVisible<InkingStaticCanvas>, "可见性是骨架变化，两版都该有");
static_assert(HasChangeZIndex<InkingStaticCanvas>, "层级是骨架变化，两版都该有");

static_assert(std::is_abstract_v<ICanvasPainter>,
              "绘制器是纯接口：它自己不该能实例化");
static_assert(std::has_virtual_destructor_v<ICanvasPainter>,
              "虚析构必须有：否则经基类指针销毁派生绘制器是 UB（SKILL §3.4）");
static_assert(std::is_constructible_v<CanvasPaintFn, void (*)(CanvasContext&)>,
              "lambda 那条路要真的能构造（CanvasPaintFn 就是 std::function）");

// ---------------------------------------------------------------------------
// 运行期显示变换（A13）：图片拖拽 / 缩放靠它
//
// 它**不是几何写入口**——本地几何一个字节都不动，所以静态档那条铁律
// （没有 Resize / ChangeOffset）不受影响。这里把这两件相反的事一起钉住：
//   - **反面**：静态组件仍然没有几何写入口（谁想"顺手加一个"就会红）；
//   - **正面**：显示变换这条通道在**基类**上，人人可调；至于"认不认"由
//     `AcceptsExternalTransform()` 声明（默认 false，跑起来才判、还会计数）。
// ---------------------------------------------------------------------------

static_assert(!HasResize<InkingStaticImage>,
              "静态组件不该有 Resize——显示变换不是几何写入口");
static_assert(!HasChangeOffset<InkingStaticImage>,
              "静态组件不该有 ChangeOffset");
static_assert(HasSetDisplayTransform<InkingStaticImage>,
              "显示变换在基类上：图片必须有（拖动 / 缩放靠它）");
static_assert(HasSetDisplayTransform<InkingStaticPanel>,
              "显示变换在基类上：不认的组件也有这个入口（调用时被拒绝并计数）");

// ---------------------------------------------------------------------------
// 视频（A14）：会动的画面，但几何照样定型
//
// 它同样是**静态档**：画面每帧都在变，可变的只有**内容**——`onRender` 里推进
// 解码与上传，几何一个字节不动。所以断言与图片 / 画布完全同构。
//
// 另外钉住两条 A14 特有的契约：
//   1. **公开头不碰 FFmpeg**：解码器只在 `.cpp` 里（pimpl）。FFmpeg 是**可选**
//      依赖，公开头一旦 include 它的头，"没装 FFmpeg 的机器连包含这个头都编译
//      不过"——那就把可选依赖变成了硬依赖（`AGENTS.md` §3 第 2 条讲的是 SDL，
//      同一条理由）。而"这两行能被编到"本身就证明了头是干净的（真接了 FFmpeg
//      时，这个编译单元里 INK_HAS_FFMPEG=1，头要是拖了 FFmpeg 就会露）；
//   2. **数据复用按钮那份**（和 `ImageData` 一样）：形状 / 描边阴影 / 锚点 /
//      命中全同构，只有"画面从哪来"不同 ⇒ 往 `AnchorData` 加字段时不用再同步。
// ---------------------------------------------------------------------------

static_assert(std::is_base_of_v<InkingStaticWidget, InkingStaticVideo>,
              "视频是静态组件：几何构造即定型，变的是内容不是几何");
static_assert(!std::is_base_of_v<InkingDynamicWidget, InkingStaticVideo>,
              "静态 / 动态是二选一，不能既进表又不进表");

static_assert(!HasResize<InkingStaticVideo>, "静态组件不该有 Resize");
static_assert(!HasChangeSelfAnchor<InkingStaticVideo>,
              "静态组件不该有 ChangeSelfAnchor");
static_assert(!HasChangeTraceAnchor<InkingStaticVideo>,
              "静态组件不该有 ChangeTraceAnchor");
static_assert(!HasChangeOffset<InkingStaticVideo>,
              "静态组件不该有 ChangeOffset");
static_assert(HasSetVisible<InkingStaticVideo>, "可见性是骨架变化，两版都该有");
static_assert(HasChangeZIndex<InkingStaticVideo>, "层级是骨架变化，两版都该有");

static_assert(std::is_base_of_v<ButtonData, VideoData>,
              "VideoData 复用 ButtonData：形状 / 描边阴影 / 锚点全同构");
static_assert(std::has_virtual_destructor_v<InkingStaticVideo>,
              "视频持有解码状态（pimpl），析构必须虚");
static_assert(!std::is_copy_constructible_v<InkingStaticVideo>,
              "解码状态与音频流句柄不能跟着对象被拷贝走");

// ---------------------------------------------------------------------------
// 资源模块（A11）：外置资源 + 轻量混淆
//
// 它**不是组件**：没有父级、不画东西、也不进命中表——调用方在构造组件之前拿
// 字节（或者组件在构造时读一次）。所以这里能钉的是"值语义"：配置与统计都是
// 可以按值拷来拷去的小结构，读资源是**自由函数**（进程级那份状态藏在 .cpp
// 里，而且**故意不析构**——`AGENTS.md` §6 第 88 条）。
// ---------------------------------------------------------------------------

static_assert(std::is_copy_constructible_v<AssetConfig>,
              "资源配置是值语义：按值传进去、按值读回来");
static_assert(std::is_copy_constructible_v<AssetStats>,
              "统计是值语义：观测口返回一份快照");

std::string sdl3_version() {
    // SDL3 把版本编码成一个整数返回（SDL2 是出参形式的 SDL_version）。
    const int version = SDL_GetVersion();
    return std::to_string(SDL_VERSIONNUM_MAJOR(version)) + "."
         + std::to_string(SDL_VERSIONNUM_MINOR(version)) + "."
         + std::to_string(SDL_VERSIONNUM_MICRO(version));
}

}  // namespace inking

