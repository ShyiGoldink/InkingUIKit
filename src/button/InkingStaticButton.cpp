#include <button/InkingStaticButton.h>

#include <button/ButtonPaint.h>

#include <button/ButtonLibrary.h>
#include <detail/InkLog.h>
#include <detail/InkingHit.h>

#include <string>

namespace inking {

namespace {

/**
 * "按名字构造却查不到配置"发生了多少次。
 *
 * 函数内静态量而不是文件级静态量：避开静态初始化顺序问题
 * （AGENTS §6 第 12 条）。只在 UI 线程构造按钮，不需要原子。
 */
int& unknownNameCount() {
    static int count = 0;
    return count;
}

}  // namespace

// ---------------------------------------------------------------------------
// 构造 / 析构
// ---------------------------------------------------------------------------

InkingStaticButton::InkingStaticButton(InkingWidget* parent,
                                       const ButtonData& data)
    : InkingStaticWidget(parent, data.name) {
    applyData(data);
    finishConstruction(/*foundInLibrary=*/true);
}

InkingStaticButton::InkingStaticButton(InkingWidget* parent,
                                       const std::string& name)
    : InkingStaticWidget(parent, name) {
    const ButtonData* registered = FindData(name);
    if (registered != nullptr) {
        applyData(*registered);
    } else {
        // 没登记过：名字拼错，或者这条用法该换成**生成出来的类**。
        //
        // 这里**故意不退回"一份看起来正常的默认外观"**：那样按钮长得不对，
        // 却没有任何报错，只能靠肉眼发现（第一版就是这样）。现在写一条错误
        // 日志、给一个空按钮、并让计数加一——于是它是**可断言**的。
        ++unknownNameCount();
        INK_LOG_ERROR(
            std::string(kButtonModuleName),
            "按名字构造却查不到配置，给了一个空按钮：" + name
                + "（名字拼错了？或者这里该用生成出来的按钮类——"
                  "生成的类是数据烘在类里，不查表）");

        ButtonData empty;
        empty.name = name;
        applyData(empty);
    }
    finishConstruction(/*foundInLibrary=*/registered != nullptr);
}

int InkingStaticButton::UnknownNameCount() noexcept {
    return unknownNameCount();
}

void InkingStaticButton::finishConstruction(bool /*foundInLibrary*/) {
    // 尺寸为 0 的按钮画不出来也点不到，这几乎总是配置漏了 width/height。
    // 文案与判据从 `0.1.1` 起统一到 Widget 层的 `WarnIfZeroSize`（T-02）：
    // 以前每个组件各写一句，措辞不同、漏接一处也没人发现（标签就漏了）。
    WarnIfZeroSize(kButtonModuleName);
}

void InkingStaticButton::applyData(const ButtonData& data) {
    // 拷一份再收尾：调用方给的 ButtonData 可能是"还没补齐三态"的，
    // 而本类要求 _data 永远处于补齐状态（渲染与命中都按它来）。
    _data = data;
    _data.Normalize();

    // 三态外观展开存好（状态机里），渲染时按状态直接取，
    // 不在每帧去判一遍。状态机也一起复位——它是契约基类持有的那一份
    // （`InkingButtonLike`），所以走它的装配置入口，本类不直接碰。
    ResetButtonLook(_data);

    if (!_data.text.path.empty() || _data.text.fontSize > 0.0f) {
        // 配了字号 / 字体时记一笔：文字是**纹理层**画的（`detail::PaintButtonText`
        // → `PaintText`），拿不到字体就退化成占位方块——所以这行日志是排查
        // "字怎么长这样"的第一站（不是"尚未落地"，那条注释是 T-3 之前的旧话）。
        INK_LOG_DEBUG(std::string(kButtonModuleName),
                      "按钮文字：字号 " + std::to_string(_data.text.fontSize)
                          + "，字体 \""
                          + (_data.text.path.empty() ? std::string("(机器默认)")
                                                     : _data.text.path)
                          + "\"（文字由纹理层绘制）");
    }

    // 基类那份 color 跟着**当前状态**走：本类重写了 onRender，用不到它，
    // 但保持一致可以避免"从基类窗口看颜色"时读到另一个答案。
    syncBaseColor();

    // -----------------------------------------------------------------------
    // 几何：写进基类
    //
    // 这里为什么要直接写基类的受保护成员，而不是走某个写入口：
    //   1. 静态组件的几何是**构造即定型**的，那些写入口（Resize /
    //      ChangeSelfAnchor / …）按设计只存在于动态档，加了就破掉
    //      "拿到 InkingAnchor* 也改不了静态几何"这条类型上的保证；
    //   2. 但"按名字构造"这条路，基类构造函数只拿到名字（它不知道 ButtonData
    //      这个类型，也不该知道），尺寸与锚点是**在这之后**才从库里取到的。
    //      不给它写进去，按钮就是个 0×0——画不出来也点不到。
    //
    // 所以这是"构造期一次性写入"，不是运行期几何变化。写完必须通知场景：
    // 绘制列表里存着绝对坐标快照，几何变了不通知，它的静态子孙会停在旧值
    // （AGENTS §6 第 19 条）。
    //
    // 副作用（有意保留）：从构造函数里调一个虚函数（NotifySceneStructureChanged
    // 底下会走到场景）在构造期是安全的——它只标脏、不读派生类的状态。
    // -----------------------------------------------------------------------
    _width = _data.width > 0 ? _data.width : 0;
    _height = _data.height > 0 ? _data.height : 0;
    _selfAnchor = _data.selfAnchor;
    _traceAnchor = _data.traceAnchor;
    _offsetX = _data.offsetX;
    _offsetY = _data.offsetY;
    // 布局数据（A6）也走同一条路：基类不存整份 XxxData，所以新增的字段
    // 必须在这儿显式搬一次（AGENTS §6 第 83 条）。
    _layout = _data.layout;
    _layoutItem = _data.layoutItem;

    // 可见性与层级走现成的写入口：它们会自己夹取、自己判"真的变了没有"，
    // 也就不可能和基类的规则分叉（例如 zindex 的下限）。
    SetVisible(_data.visible);
    ChangeZIndex(_data.zIndex);

    // 几何定下来了 → 这一份"待消费的改动"就没了。静态组件的几何此后不会再变，
    // 所以命中表建好之后，_dirty 应该一直是 false。
    ClearDirty();

    // 几何是构造期写入的，场景列表里的坐标快照要重算一次。
    NotifySceneStructureChanged();
}

// ---------------------------------------------------------------------------
// 查询
// ---------------------------------------------------------------------------

const ButtonData* InkingStaticButton::FindData(const std::string& name) {
    return ButtonLibrary::Find(name);
}

bool InkingStaticButton::HitTest(float worldX, float worldY) const {
    // 世界坐标 → 本地 → 形状判定，三步和绘制共用同一套定义
    // （`detail::HitTestShape`：它会把祖先子树变换与自己的变换各取一次逆）。
    // 变换取的是**当前显示**那份（过渡进行中是插值值），与绘制同源。
    return detail::HitTestShape(*this, GetShape(), GetDisplayTransform(),
                                worldX, worldY);
}

const ShapeSpec& InkingStaticButton::GetShape() const noexcept {
    return _data.shape;
}

int InkingStaticButton::GetWidth() const noexcept {
    // T-5：状态里写了尺寸时，这里给的是**插值出来的显示尺寸**——绘制与命中
    // 都问这个 getter，所以"画多大就点多大"。没写尺寸时它等于配置那份。
    return GetDisplayWidth();
}

int InkingStaticButton::GetHeight() const noexcept {
    return GetDisplayHeight();
}

bool InkingStaticButton::HasLayoutAnimation() const noexcept {
    // 尺寸会随状态变的静态按钮：位置快照仍然有效（约定"从左上角生长"，
    // 锚点与偏移不动），但命中表那边要按三态包络登记（见 ButtonHitEnvelope）。
    return HasButtonLayoutAnimation();
}

TransformSpec InkingStaticButton::GetSubtreeTransform() const noexcept {
    // 三态变换同时作用于子节点（T-7）：既是"这个按钮的样子"，也是
    // "它这棵子树的样子"。取的就是屏幕上正在生效的那份。
    return GetDisplayTransform();
}

ShapeBounds InkingStaticButton::GetHitEnvelope() const {
    // 静态 / 动态共用同一份算法（src/button/ButtonPaint.cpp）：
    // "三态变换的并集"对表条目和洞是同一件事——它可能盖住哪儿。
    return detail::ButtonHitEnvelope(_data, GetShape(), GetWidth(), GetHeight());
}

const ButtonData& InkingStaticButton::GetData() const noexcept {
    return _data;
}

const std::string& InkingStaticButton::GetText() const noexcept {
    return _data.label;
}

void InkingStaticButton::SetText(const std::string& text) {
    // 两种脏分开看（`AGENTS.md` §5 那张表）：
    //   - **命中脏**不标：文字不参与命中，"点到哪儿"没变；
    //   - **重绘脏**要标：文字真的画出来了（T-3 之后走纹理层），画面变了。
    // 以前这里写的是"文字不参与命中，所以不标脏"——那句话把两种脏混成了一种，
    // 等文字真开始画之后就成了"画面变了却不上报"，做顶点 / 绘制缓存时会拿旧画面。
    _data.label = text;
    MarkRepaintDirty();
}

void InkingStaticButton::SetTextColor(std::uint32_t color) {
    if (_data.textColor == color) {
        return;
    }
    _data.textColor = color;
    MarkRepaintDirty();  // 同上：不碰命中，画面变了
}

void InkingStaticButton::syncBaseColor() noexcept {
    // 基类那份 color 跟着**当前显示**的颜色走（过渡中是插值色），
    // 保持"从基类读到的是同一件事"。
    //
    // 为什么本类要管这件事：状态机（含显示色）住在 `InkingButtonLike` 里，
    // 而那里**碰不到** `_color`（它不继承 `InkingWidget`，这正是它不成菱形的
    // 代价）。所以"把显示色同步给基类"这条副作用落在本类的
    // `onButtonStateChanged` / `onButtonDisplayAdvanced` 两个钩子上。
    _color = GetDisplayColor();
}

// ---------------------------------------------------------------------------
// 每渲染帧一次：推进三态之间的颜色过渡与变换（本地几何一动不动）
// ---------------------------------------------------------------------------

void InkingStaticButton::onAnimationTick(float deltaSeconds) {
    // 推进与"这一帧变了什么"都在契约基类里；本类只在它报出来后标脏
    // （见下面两个钩子）。稳态（没有过渡在跑）时基类一个分支就返回。
    AdvanceButtonLook(deltaSeconds);
}

// ---------------------------------------------------------------------------
// 三态契约的两个 [√] 钩子：本类在"状态变了"和"显示值变了"两处挂副作用
// ---------------------------------------------------------------------------
//
// 为什么需要两个钩子而不是一个：标脏的**种类**不同。
//   - 状态变了 → 显示色当场就变了（没配过渡时），同步基类颜色；但**不标脏**
//     ——颜色不属于会标脏的四件事，标了就是每帧白重建绘制列表。
//   - 显示值推进了一帧 → 这一帧画面（可能还有命中）与上一帧不同，要标脏。
//
// 那为什么 Advance 那条要标脏、MouseHover 那条不标？因为标脏要的是
// "**这一帧**画出来跟上一帧不一样"，而帧是渲染帧：`MouseHover` 在事件泵里
// 发生，那时候还没有"这一帧"可言；`Advance` 在帧通道里跑，正好是记账的地方。

void InkingStaticButton::onButtonStateChanged(ButtonState /*from*/,
                                              ButtonState /*to*/) {
    // 只同步基类颜色，**不标脏**：三态切换的画面变化由状态机自己记成
    // "待报告"，下一帧 `onButtonDisplayAdvanced` 会把它报出来。
    // 在这里额外标一次会把它标重，而且"没配过渡"那条路依旧只走一个出口。
    syncBaseColor();
}

void InkingStaticButton::onButtonDisplayAdvanced(bool repaintDirty,
                                                 bool hitDirty) {
    if (repaintDirty) {
        // 基类那份 color 也跟着显示值走（过渡中是插值色）。
        syncBaseColor();
        // 画面变了 → **重绘脏**。注意这里**不能**顺手标成命中脏：
        // 颜色不改命中表，标错了就会每帧白重建一次绘制列表。
        MarkRepaintDirty();
    }
    if (hitDirty) {
        // 变换 / 尺寸变了 → **命中脏**：按钮转过去或长大之后，同一个点可能
        // 落到别的组件上，悬停必须重算。但它仍然**不改表**——表按本地形状
        // 烘，查询时把点反变换回本地即可（`docs/InputDesign.md` §11）。
        MarkDirty();
    }
}

// ---------------------------------------------------------------------------
// [√] 通用指针钩子：把场景推过来的判定结果转发给三态写入口
// ---------------------------------------------------------------------------
//
// 三个钩子只是转发，没有任何额外逻辑——这正是"输入通道与组件逻辑分开"的收益：
// 场景负责"谁收到"，组件负责"收到之后变成什么样"。
//
// 转发目标（`MouseHover` / `MousePress` / `TriggerClick`）现在是**基类**
// `InkingButtonLike` 的公开入口，本类不再自己实现它们。
//
// 返回值（三态变了没有）在这里没用：状态机已经把"画面变了"记成待报告账，
// 下一次 `AdvanceButtonLook` 会把它报成重绘脏（见上面两个钩子）。
// 所以这里不标脏也不会漏（`AGENTS.md` §6 第 39 条踩过的那条）。

void InkingStaticButton::onPointerHover(bool hover) {
    MouseHover(hover);
}

void InkingStaticButton::onPointerPress(PointerButton button, bool press) {
    // **右键不进三态**（0.5.0）：三态是"主键手势"的可视化。让右键点亮按钮，
    // 用户会以为左键生效了——而实际什么都没发生（右键走 contextClicked）。
    if (button != PointerButton::Primary) {
        return;
    }
    MousePress(press);
}

void InkingStaticButton::onPointerClick(PointerButton button, float designX,
                                         float designY) {
    if (button == PointerButton::Primary) {
        TriggerClick();
    } else {
        // 次键：带按下点，业务直接喂 `MenuPopup::OpenAt(x, y)`。
        TriggerContextClick(designX, designY);
    }
}

// ---------------------------------------------------------------------------
// 渲染
// ---------------------------------------------------------------------------

void InkingStaticButton::onRender(float pixelX, float pixelY) const {
    // 画什么由 src/button/ButtonPaint.cpp 提供，静态 / 动态按钮共用同一份：
    // 两份实现各自演化就会出现"静态按钮和动态按钮画得不一样"。
    // 颜色取**当前显示**那份（过渡中是插值色），变换同样取当前那份
    // （命中那边会对它取逆，两边必须同源）。
    detail::PaintButtonAppearance(*this, GetShape(), GetAppearance(),
                                  _data.stroke, _data.shadow,
                                  GetDisplayColor(), GetDisplayTransform(),
                                  pixelX, pixelY);
    if (_data.HasLabel()) {
        renderText(pixelX, pixelY);
    }
}

void InkingStaticButton::renderText(float pixelX, float pixelY) const {
    detail::PaintButtonText(*this, _data, GetDisplayTransform(), pixelX,
                            pixelY);
}

}  // namespace inking
