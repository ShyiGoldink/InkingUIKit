// ---------------------------------------------------------------------------
// Inking 示例程序：最小可运行的「窗口 + 场景 + 组件」
//
// 它**只链接 inking 动态库**（见 CMakeLists.txt），用到的全部是公开头：
//   <inking.h>                     版本 / 日志 / 消息队列 / 任务队列
//   <window/InkingWindow.h>        窗口（Show() 跑主循环）
//   <scene/InkingScene.h>          场景基类（构造即登记）
//   <scene/InkingSceneLibrary.h>         场景库
//   <button/InkingStaticButton.h>  静态按钮（三态只换颜色 → 进命中表）
//   <button/InkingDynamicButton.h> 动态按钮（几何自己动 → 不进表，每帧 Tick）
//   <text/InkingStaticLabel.h>     只读文字
//   <image/InkingSprite.h>         帧动画（序列帧 / 图集，0.6.x）
//   <image/InkingAnimateGroup.h>    动画组（一个角色多个动作，0.6.6）
//   <image/InkingSpriteGroup.h>    块叠加组（一个 sprite 由多块拼成，0.9.0）
//
// 三件事在这份代码里各演示一条：
//   1. **两种按钮的分工**：静态按钮走点击回调；动态按钮在 `onTick` 里自己
//      挪位置（`ChangeOffset` 是动态档独有的几何写入口）；
//   2. **场景「构造即登记」**：`DemoScene` 一构造就进了 `InkingSceneLibrary`，
//      `Show("demo")` 按名字找到它、激活它、跑主循环；
//   3. **链接库边界**：示例不包含任何 `src/` 下的实现，全部经 DLL 调用；
//   4. **帧动画（0.6.x）**：左上角是序列帧（8 张内联 SVG，12fps 转一圈），
//      左下角是图集（`assets/walk.png` 切 4 格）——后者还挂了帧事件，
//      每迈一步就把计数写进标签。两条素材路共用同一套帧号，只是"换字节"与
//      "换源矩形"的区别。
//
// 注意：文字由纹理层画。机器默认字体若没有对应字形，会**退化成占位方块**
// （那是纹理层有意的兜底，不是本示例的问题）。
// ---------------------------------------------------------------------------

#include <inking.h>

#include <button/InkingDynamicButton.h>
#include <button/InkingStaticButton.h>
#include <config/window_config.h>
#include <image/ImageData.h>
#include <image/InkingSprite.h>
#include <image/InkingAnimateGroup.h>
#include <image/InkingSpriteGroup.h>
#include <image/InkingStaticImage.h>
#include <image/SpriteData.h>
#include <input/Shortcut.h>
#include <panel/InkingDynamicPanel.h>
#include <panel/PanelData.h>
#include <prefab/MenuBar.h>
#include <prefab/MenuPopup.h>
#include <prefab/MenuPopupData.h>
#include <scene/InkingScene.h>
#include <scene/SceneLibrary.h>
#include <text/InkingStaticLabel.h>
#include <text/LabelData.h>
#include <window/InkingWindow.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

namespace {

// ---------------- 一组配色（0xAARRGGBB） ----------------
constexpr std::uint32_t kBg        = 0xFF1B1B24u;  // 窗口底色
constexpr std::uint32_t kAccent    = 0xFF3F7FD6u;  // 按钮通常态
constexpr std::uint32_t kAccentHi  = 0xFF5A9BF0u;  // 悬停
constexpr std::uint32_t kAccentPr  = 0xFF2F63AAu;  // 按下
constexpr std::uint32_t kGhost     = 0xFF32323Fu;  // 会飘的按钮
constexpr std::uint32_t kGhostHi   = 0xFF3F3F52u;
constexpr std::uint32_t kGhostPr   = 0xFF26262Fu;
constexpr std::uint32_t kText      = 0xFFE8E8F2u;
constexpr std::uint32_t kTextDim   = 0xFF9A9AB0u;

/// 造一份按钮配置。三态都显式写，并把两条 `*InheritsNormal` 关掉
/// ——否则 `Normalize()` 会把 hover / onclicked 又拉回 normal 那一份。
InkingButtonData MakeButton(const std::string& name, int width, int height,
                              const std::string& label, std::uint32_t normal,
                              std::uint32_t hover, std::uint32_t pressed,
                              float radius) {
    InkingButtonData d;
    d.name = name;
    d.width = width;
    d.height = height;

    d.normal = InkingButtonAppearance(InkingAppearanceType::Color, normal);
    d.hover = InkingButtonAppearance(InkingAppearanceType::Color, hover);
    d.onclicked = InkingButtonAppearance(InkingAppearanceType::Color, pressed);
    d.hoverInheritsNormal = false;
    d.onclickedInheritsNormal = false;

    // 一段很短的过渡：让"三态之间是插值过去的"这件事看得见。
    d.normal.transitionSeconds = 0.14f;
    d.hover.transitionSeconds = 0.14f;
    d.onclicked.transitionSeconds = 0.06f;

    d.label = label;
    d.text.fontSize = 26.0f;
    d.text.align = InkingTextAlignH::Center;
    d.text.alignV = InkingTextAlignV::Middle;

    // 锚点都由父级中心算：`traceAnchor` 是"落在父级的哪个百分比点"，
    // `selfAnchor` 是"自己的哪个点对上它"。两个都取 Center 就是正中对齐。
    d.selfAnchor = InkingChangeAnchor::Center;
    d.traceAnchor = InkingChangeAnchor::Center;

    d.shape = InkingShapeSpec::RoundedRect(radius);
    return d;
}

/// 造一份标签配置（居中、给定字号与颜色）。
InkingLabelData MakeLabel(const std::string& name, int width, int height,
                            const std::string& text, float fontSize,
                            std::uint32_t color) {
    InkingLabelData d;
    d.name = name;
    d.width = width;
    d.height = height;
    d.text = text;
    d.style.fontSize = fontSize;
    d.style.align = InkingTextAlignH::Center;
    d.style.alignV = InkingTextAlignV::Middle;
    d.color = color;
    d.selfAnchor = InkingChangeAnchor::Center;
    d.traceAnchor = InkingChangeAnchor::Center;
    return d;
}

/**
 * 会飘的动态按钮：**演示动态档与静态档的唯一差别——几何能自己改**。
 *
 * 它只做一件事：每逻辑步把自己的 `offsetX` 按正弦挪一点。`ChangeOffset` 是
 * `InkingDynamicWidget` 的几何写入口，它自己会标脏、自己会通知场景
 * （静态档没有这个入口——那是有意的类型保证，`src/inking.cpp` 里有断言钉着）。
 *
 * 输入照常：`InkingDynamicButton::onTick` 里那一句 `JudgePointer()` 是"自己拉
 * 指针"的实现，所以这里必须先调基类，再动几何。
 */
class FloatingButton : public InkingDynamicButton {
public:
    FloatingButton(InkingWidget* parent, const InkingButtonData& data,
                   float amplitude)
        : InkingDynamicButton(parent, data),
          _baseX(data.offsetX),
          _baseY(data.offsetY),
          _amplitude(amplitude) {}

protected:
    void onTick(float deltaSeconds) override {
        InkingDynamicButton::onTick(deltaSeconds);  // 先收输入

        _phase += deltaSeconds;
        const float dx = _amplitude * std::sin(_phase * 1.7f);
        ChangeOffset(_baseX + dx, _baseY);
    }

private:
    float _phase = 0.0f;
    float _baseX = 0.0f;
    float _baseY = 0.0f;
    float _amplitude = 0.0f;
};

/**
 * 序列帧素材：8 张内联 SVG，每张把指针转 45°。
 *
 * 为什么内联而不是带 8 个文件：序列帧的**每帧就是一份字节**，演示这个机制不需要
 * 外部资源；`static` 的生命周期是整个进程，正好对上"帧只借视图不拷贝字节"。
 */
const std::array<std::string, 8>& SpinnerFrames() {
    static const std::array<std::string, 8> frames = [] {
        std::array<std::string, 8> out;
        for (int i = 0; i < 8; ++i) {
            char rotate[48];
            std::snprintf(rotate, sizeof(rotate), "rotate(%d 42 42)", i * 45);
            out[static_cast<std::size_t>(i)] =
                std::string(
                    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"84\" "
                    "height=\"84\">"
                    "<circle cx=\"42\" cy=\"42\" r=\"30\" fill=\"#232A3C\"/>"
                    "<rect x=\"40\" y=\"14\" width=\"4\" height=\"28\" rx=\"2\" "
                    "fill=\"#5A9BF0\" transform=\"") + rotate
                + "\"/>"
                  "<circle cx=\"42\" cy=\"42\" r=\"5\" fill=\"#7FB0FF\"/></svg>";
        }
        return out;
    }();
    return frames;
}

/**
 * 示例场景。
 *
 * **构造即登记**：`InkingScene("demo")` 这一步就把自己挂进 `InkingSceneLibrary`
 * ——名字被占时会进入"停放态"（`IsRegistered()` 为 false），所以下面 `main`
 * 里那份断言不是多余的。
 *
 * 组件的挂法用 `AddChild`（**拥有型**）：场景持有 `unique_ptr`，场景销毁时
 * 子树跟着销毁。构造参数里那个 `this` 是"父级"（非拥有、可后续换父），
 * 传它只是让组件在构造期就能沿父链找到场景与自己的绝对坐标。
 */
class DemoScene : public InkingScene {
public:
    DemoScene() : InkingScene("demo") {
        BuildUi();
        // 文件拖放（T-10）：一次拖 N 个文件 = 一条事件带 N 个路径。
        // 示例只留一行日志——"拖进来没反应"时，先看 Log.html 里有没有这一条，
        // 就知道是没收到、还是收到了没人管。
        filesDropped.Subscribe([](const std::vector<std::string>& paths) {
            INK_LOG_INFO("DemoScene", "filesDropped：" + std::to_string(paths.size()) +
                                          " 个文件");
        });
    }

private:
    void BuildUi() {
        // 0) 整张界面设计稿当背景（0.5.1）：
        //    一份 SVG（assets/design.svg）在运行期按目标像素光栅化成一张纹理，
        //    `runtimeAsset = true` 表示字节在运行期从 exe 旁的 assets/ 读——
        //    换稿不用重编。`hitPassThrough = true` 让空白点击穿到下面（否则
        //    这块铺满全屏的图会把所有按钮都挡住，看起来像"按钮点不动"）。
        InkingImageData backdrop;
        backdrop.name = "design_backdrop";
        backdrop.width = InkingDesignWidth();
        backdrop.height = InkingDesignHeight();
        backdrop.runtimeAsset = true;
        backdrop.hitPassThrough = true;
        backdrop.normal.type = InkingAppearanceType::Image;
        backdrop.normal.source = "design.svg";
        backdrop.normal.imageFit = InkingImageFit::Fill;
        backdrop.selfAnchor = InkingChangeAnchor::LeftTop;
        backdrop.traceAnchor = InkingChangeAnchor::LeftTop;
        backdrop.offsetX = 0.0f;
        backdrop.offsetY = 0.0f;
        backdrop.zIndex = InkingZIndex::background;
        AddChild(std::make_unique<InkingStaticImage>(this, backdrop));

        // 1) 菜单栏：无边框窗口的**自绘标题栏**（拖动 + 最小化 / 关闭），
        //    同时承担两件事——
        //      · 滚轮缩放：指针在菜单栏上滚一下就按档位缩放窗口（无边框没有系统
        //        边缘可拖，这是改尺寸的唯一入口）；
        //      · 全局快捷键：菜单项绑的组合不依赖焦点，全屏隐藏后照样能用。
        //    全屏由 `InkingWindow` 内置的 F11 切换；菜单栏在全屏时会自动隐藏
        //    （`MenuBarStyle::hideInFullscreen` 默认开）。
        inking_prefab::MenuBarStyle barStyle =
            inking_prefab::MenuBarStyle::MakeDefault();
        barStyle.draggable = true;       // 按住空白处拖窗口
        barStyle.captionButtons = true;  // 右上角最小化 / 关闭
        inking_prefab::MenuBar* bar = static_cast<inking_prefab::MenuBar*>(
            AddChild(std::make_unique<inking_prefab::MenuBar>(this, barStyle)));
        _menuBar = bar;

        InkingButtonData item = MakeButton("menu_item", 92, 28, "", kGhost,
                                             kGhostHi, kGhostPr, 8.0f);
        item.text.fontSize = 20.0f;
        bar->AddButton(item, "放大",
                       [] { InkingWindow::Instance().ZoomBy(1); },
                       InkingShortcut::Parse("Ctrl+="));
        bar->AddButton(item, "缩小",
                       [] { InkingWindow::Instance().ZoomBy(-1); },
                       InkingShortcut::Parse("Ctrl+-"));
        bar->AddButton(item, "全屏",
                       [] { InkingWindow::Instance().ToggleFullscreen(); },
                       InkingShortcut::Parse("Ctrl+Enter"));
        bar->AddButton(item, "切换动作", [this] { ToggleWalkerGroup(); },
                       InkingShortcut::Parse("F2"));
        bar->AddButton(item, "叠块", [this] { ToggleSpriteGroup(); },
                       InkingShortcut::Parse("F3"));
        bar->AddButton(item, "退出",
                       [] { InkingWindow::Instance().RequestExit(); },
                       InkingShortcut::Parse("Ctrl+Q"));

        // 1) 标题
        _title = AddLabel("title", "Inking 动态库示例", 900, 70, 44.0f, kText,
                          -260.0f);
        // 2) 一行说明（点会飘的按钮也能点）
        _hint = AddLabel("hint", "下面那个按钮每帧都在自己改变位置（动态档）",
                         900, 50, 22.0f, kTextDim, 10.0f);

        // 3) 静态按钮：点击计数
        InkingButtonData clickData = MakeButton("click_me", 260, 84, "点我",
                                                 kAccent, kAccentHi, kAccentPr,
                                                 14.0f);
        clickData.offsetY = -120.0f;
        InkingStaticButton* button =
            static_cast<InkingStaticButton*>(
                AddChild(std::make_unique<InkingStaticButton>(
                    this, clickData)));
        // 语义是"替换"：再设一次就是换掉上一个回调（要挂多个用 clicked.Subscribe）。
        button->SetOnClicked([this] {
            ++_clicks;
            _status->SetText("点击次数：" + std::to_string(_clicks));
        });
        _button = button;

        // 4) 状态标签
        _status = AddLabel("status", "点击次数：0", 700, 60, 28.0f, kText,
                           90.0f);

        // 5) 动态按钮：自己飘
        InkingButtonData floatData = MakeButton("floating", 240, 76, "我会飘",
                                                 kGhost, kGhostHi, kGhostPr,
                                                 38.0f);
        floatData.shape = InkingShapeSpec::Capsule();
        floatData.offsetY = 220.0f;
        AddChild(std::make_unique<FloatingButton>(this, floatData, 220.0f));

        // 6) 菜单浮窗（0.4.1）：定点弹出，内容自己组织
        //
        // 0.5.0 起有两个定点来源，都是"同一次点击"的自然结果：
        //   · 左键点"打开菜单"按钮 → 定点算在按钮下方（原有演示，`OpenAt` 的
        //     "调用方给点"这条能力）；
        //   · 右键 / 触摸长按 → 定点是**按下点**，这才是真正的上下文菜单。
        // 两条路都只是 `OpenAt(x, y)`：浮窗自己不关心定点从哪来。
        _menu = std::make_unique<inking_prefab::MenuPopup>(this);

        InkingButtonData menuItem;
        menuItem.normal = InkingButtonAppearance(InkingAppearanceType::Color,
                                                   0xFFFFFFFFu);
        menuItem.hover = InkingButtonAppearance(InkingAppearanceType::Color,
                                                  0xFFEDE7F8u);
        menuItem.onclicked = menuItem.hover;
        menuItem.textColor = 0xFF33303Cu;
        menuItem.text.fontSize = 18.0f;

        // 第一项绑**局部**快捷键：菜单开着且焦点在菜单里才生效（关着时不存在）。
        _menu->AddButton(
            menuItem, "重置计数",
            [this] {
                _clicks = 0;
                _status->SetText("点击次数：0");
            },
            InkingShortcut::Parse("Ctrl+R"));
        _menu->AddButton(menuItem, "切换全屏", [] {
            InkingWindow::Instance().ToggleFullscreen();
        });
        _menu->AddButton(menuItem, "退出", [] {
            InkingWindow::Instance().RequestExit();
        });

        InkingButtonData openData = MakeButton("open_menu", 220, 64, "打开菜单",
                                                 kGhost, kGhostHi, kGhostPr,
                                                 12.0f);
        openData.offsetY = 320.0f;
        auto* opener = static_cast<InkingStaticButton*>(
            AddChild(std::make_unique<InkingStaticButton>(this,
                                                                 openData)));
        opener->SetOnClicked([this, opener] {
            // 定点 = 按钮左下角再往下 4 像素；靠近窗口右下时浮窗自己翻到对侧。
            _menu->OpenAt(opener->GetAbsX(),
                          opener->GetAbsY()
                              + static_cast<float>(opener->GetHeight()) + 4.0f);
        });

        // **右键菜单**（0.5.0）：坐标是按下点，场景已经按按钮分槽记好了。
        //
        // 触摸屏上按住这个按钮 0.5 秒会走到同一个回调——按钮不需要知道那是
        // 手指，这正是"统一输入"的意思（组件只见 Primary / Secondary）。
        opener->SetOnContextClicked([this](float x, float y) {
            _menu->OpenAt(x, y);
        });

        // 7) 帧动画（0.6.x）：两种素材各一个 + 一个动画组（0.6.6）
        AddSpinner();
        AddWalker();
        AddWalkerGroup();
        AddSpriteGroup();

        // 8) 动态面板 + 滚轮（0.9.1）：少数几处"必须上手滚一下"才看得出来的东西
        AddScrollCard();
    }

    /**
     * **能滚的动态面板**（0.9.1 T-03）：一张 200×300 的卡片，里面 12 个按钮。
     *
     * 为什么示例里得有它：滚轮这件事**自检只能证明数值与归属**（一格 = 48、
     * 不双滚、只有最上面那个动），证明不了"手感对不对"。而这一版最容易返工
     * 的恰恰是手感——所以留一个能上手滚的入口。
     *
     * 三条要试的：
     *   1. 指针在**卡片空白区**滚 ⇒ 整列跟着走（拉通道：`onAnimationTick` 里读）；
     *   2. 指针停在**某个按钮上**滚 ⇒ 照样滚卡片，不是"滚到按钮就停"
     *      （推通道：场景沿父链找到本面板，`HandlesSubtreeWheel`）；
     *   3. 滚到底再滚 ⇒ 停在底（夹紧在 `[0, range]`，不累加）。
     *
     * 内容故意比视口高很多（772 > 300），否则滚动条不出现、也滚不动。
     */
    void AddScrollCard() {
        AddLabel("scroll_hint",
                 "滚轮试试：这张卡片是动态面板（指针在按钮上也照样滚）", 400, 34,
                 18.0f, kTextDim, -190.0f, 430.0f);

        InkingPanelData data;
        data.name = "scroll_card";
        data.width = 200;
        data.height = 300;
        data.offsetX = 430.0f;
        data.offsetY = 40.0f;
        data.closed.color = 0xFF26262Fu;
        data.shape = InkingShapeSpec::RoundedRect(18.0f);
        data.clipChildren = true;   // 滚出去的内容要剪掉，否则画到卡片外面
        data.scroll.enabled = true;
        data.scroll.vertical = true;
        data.scroll.horizontal = false;
        data.scroll.barBackgroundColor = 0xFF26262Fu;  // 条带不铺色时内容会从缝里透出来
        auto* card = static_cast<InkingDynamicPanel*>(
            AddChild(std::make_unique<InkingDynamicPanel>(this, data)));

        for (int i = 0; i < 12; ++i) {
            InkingButtonData item =
                MakeButton("scroll_item_" + std::to_string(i), 160, 56,
                           "第 " + std::to_string(i + 1) + " 项", kGhost,
                           kGhostHi, kGhostPr, 10.0f);
            item.text.fontSize = 20.0f;
            // **LeftTop 锚点**：滚动量是按"子节点矩形相对容器左上角"量的
            // （`UpdateScrollRange`），用 Center 锚点量出来的内容高度会随
            // 容器高度漂——那正是"列表末尾永远滚不到"的经典成因。
            item.selfAnchor = InkingChangeAnchor::LeftTop;
            item.traceAnchor = InkingChangeAnchor::LeftTop;
            item.offsetX = 12.0f;
            item.offsetY = 12.0f + static_cast<float>(i) * 64.0f;
            card->AddChild(std::make_unique<InkingStaticButton>(card, item));
        }
        // 显式量一次（框架不做隐式扫描）：不量的话可滚范围是 0，滚轮一格都不动。
        card->UpdateScrollRange();
    }

    /**
     * **序列帧**：每帧一份独立字节，换帧 = 换字节视图。
     *
     * 12fps、8 帧 ⇒ 1.5 秒转一圈。帧号是**时间的纯函数**（场景动画时钟），
     * 所以它和同屏其它 sprite 永远同步，不会各自漂。
     */
    void AddSpinner() {
        const auto& frames = SpinnerFrames();
        InkingSpriteData data;
        data.name = "spinner";
        data.width = 84;
        data.height = 84;
        data.fps = 12.0;
        data.loop = true;
        data.autoplay = true;
        data.selfAnchor = InkingChangeAnchor::Center;
        data.traceAnchor = InkingChangeAnchor::Center;
        data.offsetX = -520.0f;
        data.offsetY = -250.0f;
        for (std::size_t i = 0; i < frames.size(); ++i) {
            InkingButtonAppearance frame(InkingAppearanceType::Image,
                                           0xFF000000u);
            // 缓存键 = 帧名：同名字才会复用纹理（8 帧各一份，所以各一个键）。
            frame.source = "spinner#" + std::to_string(i);
            frame.imageBytes =
                reinterpret_cast<const std::uint8_t*>(frames[i].data());
            frame.imageSize = frames[i].size();
            data.frames.push_back(frame);
        }
        _spinner = static_cast<InkingSprite*>(
            AddChild(std::make_unique<InkingSprite>(this, data)));
    }

    /**
     * **图集**：一张 `assets/walk.png` 切 4 格，换帧只改**源矩形**
     * （零解码、零新建纹理）。8fps 走两步，再挂两个帧事件把"迈步"数出来。
     *
     * `runtimeAsset = true`：字节在运行期从 exe 旁的 `assets/` 读——换美术
     * 不用重编（`CXXCSS/Image/walker.json` 是同一个配置的"生成器"版本）。
     */
    void AddWalker() {
        InkingSpriteData data;
        data.name = "walker";
        data.width = 96;   // 显示放大一倍（每格 48）
        data.height = 96;
        data.runtimeAsset = true;
        data.normal.type = InkingAppearanceType::Image;
        data.normal.source = "walk.png";
        data.sheet.frameWidth = 48;
        data.sheet.frameHeight = 48;
        data.sheet.columns = 4;
        data.sheet.rows = 1;
        data.fps = 8.0;
        // 第 0 / 2 帧是"脚落地"的两帧——帧事件由**帧号变化**驱动，
        // 掉帧也不会少算（这一点和"按时间到点触发"是两回事）。
        data.events = {{0, "step"}, {2, "step"}};
        data.selfAnchor = InkingChangeAnchor::Center;
        data.traceAnchor = InkingChangeAnchor::Center;
        data.offsetX = -520.0f;
        data.offsetY = 40.0f;
        auto* walker = static_cast<InkingSprite*>(
            AddChild(std::make_unique<InkingSprite>(this, data)));
        walker->frameEvent.Subscribe([this](const std::string& name,
                                            std::size_t /*frame*/) {
            if (name != "step" || _stepsLabel == nullptr) {
                return;
            }
            ++_steps;
            _stepsLabel->SetText("脚步 " + std::to_string(_steps));
        });

        _stepsLabel = AddLabel("walker_steps", "脚步 0", 320, 40, 20.0f, kTextDim,
                               110.0f, -520.0f);
    }

    /**
     * **动画组**（0.6.6 T-18）：一个角色两个动作，`SwitchTo()` 换的是**配置**不是节点。
     *
     * 两个动作都用同一张 `walk.png`（只有速率与显示尺寸不同）：素材不够四个方向时，
     * 这足以说明"组"的形状——切过去帧率 / 尺寸一起变，而**节点数仍是 1**
     * （没有 N−1 个隐藏节点参与绘制遍历与命中）。
     *
     * 切换入口挂在菜单栏的「切换动作」（F2）上：那是全局快捷键，正好演示
     * "一个入口改一组状态"。
     */
    void AddWalkerGroup() {
        const auto makeAnimation = [](const char* name, double fps, int size) {
            InkingSpriteData data;
            data.name = name;
            data.width = size;
            data.height = size;
            data.runtimeAsset = true;
            data.normal.type = InkingAppearanceType::Image;
            data.normal.source = "walk.png";
            data.sheet.frameWidth = 48;
            data.sheet.frameHeight = 48;
            data.sheet.columns = 4;
            data.sheet.rows = 1;
            data.fps = fps;
            data.selfAnchor = InkingChangeAnchor::Center;
            data.traceAnchor = InkingChangeAnchor::Center;
            data.offsetX = -300.0f;
            data.offsetY = 180.0f;
            return data;
        };

        InkingAnimateGroupData group;
        group.animations.emplace_back("walk", makeAnimation("walk", 8.0, 96));
        group.animations.emplace_back("run", makeAnimation("run", 20.0, 120));

        _walkerGroup = static_cast<InkingAnimateGroup*>(AddChild(
            std::make_unique<InkingAnimateGroup>(this, group, "walk")));
        _actionLabel = AddLabel("walker_action", "动作：walk（F2 切换）", 360, 40,
                                20.0f, kTextDim, 300.0f, -300.0f);
    }

    /// 菜单「切换动作」用：在 walk / run 之间来回（真正的多动作角色有 4 个以上，
    /// 这里只演示切换本身）。
    void ToggleWalkerGroup() {
        if (_walkerGroup == nullptr || _actionLabel == nullptr) {
            return;
        }
        const std::string next =
            _walkerGroup->GetCurrentAnimation() == "walk" ? "run" : "walk";
        if (_walkerGroup->SwitchTo(next)) {
            _actionLabel->SetText("动作：" + next + "（F2 切换）");
        }
    }

    /**
     * **块叠加组**（0.9.0 T-05）：一个 sprite 由**多块**拼成。
     *
     * 跟上面的动画组不是一件事：动画组是"一个角色的多个动作、一帧一块"，块叠加组是
     * "同一个 sprite 在一帧里画几块"（每块取图集里不同的格，同框叠图）。
     *
     * 两个动作用同一张 `walk.png`（4 格 / 48×48），区别只有每帧画几块：
     *   - `idle`：每帧 **1 块**（正常角色）；
     *   - `ghost`：每帧 **2 块**（相邻两格叠在一起 ⇒ 看起来是重影）。
     *
     * 这一页想让人看见的是：**2 块一帧仍然只有 1 次提交**——不是"两块各画一次"。
     * 判据在自检里（`CheckSpriteGroup` / `CheckSpriteBatching` 的 K=1/2/4）。
     */
    void AddSpriteGroup() {
        InkingSpriteGroupData group;
        group.source = "walk.png";
        group.frameWidth = 48;
        group.frameHeight = 48;
        group.columns = 4;
        group.rows = 1;
        // 静态档**几何构造即定型**（没有公开的几何写入口），所以位置只能配在这里。
        group.selfAnchor = InkingChangeAnchor::Center;
        group.traceAnchor = InkingChangeAnchor::Center;
        group.offsetX = -520.0f;
        group.offsetY = 260.0f;

        InkingSpriteGroupAnimation idle;
        idle.name = "idle";
        idle.fps = 8.0;
        idle.loop = true;
        idle.frames = {{0}, {1}, {2}, {3}};

        InkingSpriteGroupAnimation ghost;
        ghost.name = "ghost";
        ghost.fps = 8.0;
        ghost.loop = true;
        // 每帧两块：相邻两格叠一起。**顺序就是叠加顺序**（后面的盖在前面上）。
        ghost.frames = {{0, 1}, {1, 2}, {2, 3}, {3, 0}};

        group.animations.push_back(std::move(idle));
        group.animations.push_back(std::move(ghost));

        _spriteGroup = static_cast<InkingSpriteGroup*>(AddChild(
            std::make_unique<InkingSpriteGroup>(this, group, "idle")));
        _blockLabel = AddLabel("block_action", "叠块：idle 1 块（F3 切换）", 380,
                               40, 20.0f, kTextDim, 330.0f, -520.0f);
    }

    /// 菜单「叠块」用：在 idle（1 块）与 ghost（2 块）之间来回。
    void ToggleSpriteGroup() {
        if (_spriteGroup == nullptr || _blockLabel == nullptr) {
            return;
        }
        const std::string next =
            _spriteGroup->GetCurrentAnimation() == "idle" ? "ghost" : "idle";
        if (_spriteGroup->SwitchTo(next)) {
            _blockLabel->SetText(next == "ghost"
                                     ? "叠块：ghost 2 块（F3 切换）"
                                     : "叠块：idle 1 块（F3 切换）");
        }
    }

    InkingStaticLabel* AddLabel(const std::string& name,
                                        const std::string& text, int width,
                                        int height, float fontSize,
                                        std::uint32_t color, float offsetY,
                                        float offsetX = 0.0f) {
        InkingLabelData data =
            MakeLabel(name, width, height, text, fontSize, color);
        data.offsetX = offsetX;
        data.offsetY = offsetY;
        return static_cast<InkingStaticLabel*>(
            AddChild(std::make_unique<InkingStaticLabel>(this, data)));
    }

    InkingStaticLabel* _title = nullptr;
    InkingStaticLabel* _hint = nullptr;
    InkingStaticLabel* _status = nullptr;
    InkingStaticLabel* _stepsLabel = nullptr;
    InkingStaticButton* _button = nullptr;
    InkingSprite* _spinner = nullptr;
    InkingAnimateGroup* _walkerGroup = nullptr;
    InkingStaticLabel* _actionLabel = nullptr;
    InkingSpriteGroup* _spriteGroup = nullptr;
    InkingStaticLabel* _blockLabel = nullptr;
    int _steps = 0;
    inking_prefab::MenuBar* _menuBar = nullptr;
    std::unique_ptr<inking_prefab::MenuPopup> _menu;
    int _clicks = 0;
};

}  // namespace

int main(int /*argc*/, char** /*argv*/) {
    std::printf("[demo] Inking %s-%s / 链接到的 SDL3 %s\n",
                InkingVersion, InkingVersionStage,
                InkingSdl3_version().c_str());
    std::printf("[demo] 消息队列编译期启用：%s\n",
                InkingMessageEnabled ? "是" : "否");

    DemoScene scene;
    if (!scene.IsRegistered()) {
        // 名字被占（例如同一进程里已经建过一个 "demo"）。
        std::printf("[demo] 场景 \"demo\" 登记被驳回了——名字被占，退出。\n");
        return 1;
    }

    // Show() 是**同步**的：里面建窗口、跑主循环，直到关窗 / ESC 才返回。
    // 它按名字从 InkingSceneLibrary 找场景并设为活跃（不需要自己 SetActiveScene）。
    //
    // 无边框：必须在 `Show()` **之前**设——那时它还只是记账，建窗口时直接带上
    // `SDL_WINDOW_BORDERLESS`，不会有一次"先带边框再拆掉"的窗口重建。
    InkingWindow& window = InkingWindow::Instance();
    window.SetBorderless(true);
    // ESC 的语义是"退一层"：全屏时先退全屏，再按一次才请求退出。退出前会过
    // 这道闸门——返回 false 就是"先别退"（业务可以在这里弹确认框）。
    window.SetExitConfirmHandler([] {
        std::printf("[demo] 退出确认闸门：直接放行（真实业务可在这里弹确认框）\n");
        return true;
    });

    window.Show("demo");

    std::printf("[demo] 主循环结束，退出。\n");
    return 0;
}
