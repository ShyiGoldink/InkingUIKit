#include <button/InkingDynamicButton.h>

#include <button/ButtonPaint.h>

#include <button/ButtonLibrary.h>
#include <detail/InkLog.h>
#include <detail/InkingHit.h>
#include <scene/InkingScene.h>

#include <string>

namespace inking {

namespace {

/**
 * "按名字构造却查不到配置"发生了多少次。
 *
 * 和静态版**各记各的**：一份计数被两条路共用的话，"静态按钮名字拼错了"
 * 会被"动态按钮名字拼错了"顶掉，断言就指不到具体是哪一个出的问题。
 * 函数内静态量，避开静态初始化顺序问题（AGENTS §6 第 12 条）。
 */
int& unknownNameCount() {
    static int count = 0;
    return count;
}

}  // namespace

// ---------------------------------------------------------------------------
// 构造
// ---------------------------------------------------------------------------

InkingDynamicButton::InkingDynamicButton(InkingWidget* parent,
                                         const ButtonData& data)
    : InkingDynamicWidget(parent, data.name) {
    applyData(data);
    finishConstruction();
}

InkingDynamicButton::InkingDynamicButton(InkingWidget* parent,
                                         const std::string& name)
    : InkingDynamicWidget(parent, name) {
    const ButtonData* registered = FindData(name);
    if (registered != nullptr) {
        applyData(*registered);
    } else {
        // 和静态版同一条规矩：**故意不退回"一份看起来正常的默认外观"**——
        // 那样按钮长得不对却没有任何报错，只能靠肉眼发现。
        ++unknownNameCount();
        INK_LOG_ERROR(
            std::string(kButtonModuleName),
            "按名字构造动态按钮却查不到配置，给了一个空按钮：" + name
                + "（名字拼错了？或者这里该用生成出来的按钮类——"
                  "生成的类是数据烘在类里，不查表）");

        ButtonData empty;
        empty.name = name;
        applyData(empty);
    }
    finishConstruction();
}

int InkingDynamicButton::UnknownNameCount() noexcept {
    return unknownNameCount();
}

void InkingDynamicButton::finishConstruction() {
    // 与静态按钮同一条护栏（`0.1.1` T-02：判据与文案统一在 Widget 层）。
    WarnIfZeroSize(kButtonModuleName);
}

void InkingDynamicButton::applyData(const ButtonData& data) {
    // 拷一份再收尾：调用方给的 ButtonData 可能是"还没补齐三态"的。
    _data = data;
    _data.Normalize();

    // 三态外观展开存进**契约基类**持有的状态机（`InkingButtonLike`），
    // 渲染时按状态直接取；本类不直接碰它，走它的装配置入口。
    ResetButtonLook(_data);

    // -----------------------------------------------------------------------
    // 几何：走**动态档的写入口**，而不是像静态版那样直接写基类的受保护成员
    //
    // 这正是两档最本质的差别：写入口自己会 `MarkDirty()`、自己会
    // `NotifySceneStructureChanged()`（绘制列表里存着绝对坐标快照，不通知的话
    // 静态子孙会停在旧值——AGENTS §6 第 19 条），所以"改了几何忘了通知"
    // 这条路在动态档上根本不存在。
    //
    // 静态版相反：它没有写入口（那是有意的类型保证），只能在构造期一次性
    // 把几何写进基类，然后自己补一次通知。
    //
    // 在构造函数体里调它们是安全的：写入口只标脏、只通知场景，不读派生类状态。
    // -----------------------------------------------------------------------
    Resize(_data.width > 0 ? _data.width : 0,
           _data.height > 0 ? _data.height : 0);
    ChangeSelfAnchor(_data.selfAnchor);
    ChangeTraceAnchor(_data.traceAnchor);
    ChangeOffset(_data.offsetX, _data.offsetY);
    // 布局数据（A6）：基类不存整份 XxxData，新增字段要显式搬一次
    // （AGENTS §6 第 83 条）。
    _layout = _data.layout;
    _layoutItem = _data.layoutItem;

    // 可见性与层级走现成的写入口：它们会自己夹取、自己判"真的变了没有"。
    SetVisible(_data.visible);
    ChangeZIndex(_data.zIndex);

    syncBaseColor();

    // 构造期这批几何写入已经通知过场景了，没有"等着被消费"的改动。
    // 之后每次 Resize / ChangeOffset 都会自己标脏——那份脏标记是给将来的
    // 命中表 / 重绘机制用的（T-3），本类不消费它。
    ClearDirty();
}

// ---------------------------------------------------------------------------
// 查询
// ---------------------------------------------------------------------------

const ButtonData* InkingDynamicButton::FindData(const std::string& name) {
    return ButtonLibrary::Find(name);
}

bool InkingDynamicButton::HitTest(float worldX, float worldY) const {
    // 世界坐标 → 本地 → 形状判定，和静态版共用同一份实现
    // （含"撤祖先变换 + 用当前变换取逆"那两步）。动态按钮的几何每帧可能变，
    // 所以 `GetAbsX/Y` 与 `GetDisplayTransform` 都是**现算**的，正好对。
    return detail::HitTestShape(*this, GetShape(), GetDisplayTransform(),
                                worldX, worldY);
}

const ShapeSpec& InkingDynamicButton::GetShape() const noexcept {
    return _data.shape;
}

const ButtonData& InkingDynamicButton::GetData() const noexcept {
    return _data;
}

const std::string& InkingDynamicButton::GetText() const noexcept {
    return _data.label;
}

void InkingDynamicButton::SetText(const std::string& text) {
    // 和静态版同一条：标重绘脏（画面变了），不标命中脏（文字不参与命中）。
    _data.label = text;
    MarkRepaintDirty();
}

void InkingDynamicButton::SetTextColor(std::uint32_t color) {
    if (_data.textColor == color) {
        return;
    }
    _data.textColor = color;
    MarkRepaintDirty();
}

int InkingDynamicButton::GetWidth() const noexcept {
    // 配了逐态尺寸就按显示值（三态之间插值）；没配就听基类——`Resize()` 那套
    // 写入口照旧。两者别混用：同时配了逐态尺寸又调 Resize，看到的会是逐态那份。
    return HasButtonLayoutAnimation() ? GetDisplayWidth()
                                      : InkingAnchor::GetWidth();
}

int InkingDynamicButton::GetHeight() const noexcept {
    return HasButtonLayoutAnimation() ? GetDisplayHeight()
                                      : InkingAnchor::GetHeight();
}

bool InkingDynamicButton::HasLayoutAnimation() const noexcept {
    return HasButtonLayoutAnimation();
}

TransformSpec InkingDynamicButton::GetSubtreeTransform() const noexcept {
    return GetDisplayTransform();
}

ShapeBounds InkingDynamicButton::GetHitEnvelope() const {
    // 只用于登记动态洞（动态组件不进表）。和静态版共用同一份算法。
    return detail::ButtonHitEnvelope(_data, GetShape(), GetWidth(), GetHeight());
}

void InkingDynamicButton::syncBaseColor() noexcept {
    // 基类那份 color 跟着**当前显示**的颜色走（过渡中是插值色）。状态机住在
    // `InkingButtonLike` 里，它碰不到 `_color`（不继承 `InkingWidget`，这正是
    // 它不成菱形的代价），所以同步这条副作用落在本类的两个钩子上。
    _color = GetDisplayColor();
}

bool InkingDynamicButton::IsPointerOver() const noexcept {
    return _probe.IsHovered();
}

bool InkingDynamicButton::IsPressCaptured() const noexcept {
    // 三态只跟主键（右键不进 Pressed）：问"按住没"就是问主键捕获。
    return _probe.IsCaptured(PointerButton::Primary);
}

// ---------------------------------------------------------------------------
// 输入：自己从场景拉（静态版是场景推过来）
// ---------------------------------------------------------------------------

bool InkingDynamicButton::JudgePointer() noexcept {
    InkingScene* scene = GetScene();
    if (scene == nullptr) {
        // 不在任何场景下就没有指针可读：动态按钮的输入只有这一条来源。
        // 这时把它退回"没被指到"的样子，而不是留着上一次的判定——不然
        // 它被移出场景、过一会儿又登记回来时，会带着一份过期的悬停状态。
        // 三态退回走继承来的喂入口（它们自己会经钩子同步基类颜色）。
        const bool hoverChanged = MouseHover(false);
        const bool pressChanged = MousePress(false);
        _probe.Reset();
        return hoverChanged || pressChanged;
    }

    const bool inside = scene->IsPointerInside();
    // 按下用的是**原始按键状态**，不折进 inside：和静态版一致——按着不放把指针
    // 拖出窗口，仍然是"按着"（`DispatchPointer` 那边也是这么用 `_pointerDown` 的）。
    // 0.5.0 起是**整包**两个键，`PointerProbe` 按槽各判各的。
    const PointerDownState down = scene->GetPointerDown();

    // 命中自判：世界坐标 → 本地坐标，问自己的形状。
    // 指针不在窗口里时一律算"不在按钮上"（窗口外不该继续悬停）。
    const bool hit = inside
                  && HitTest(scene->GetPointerX(), scene->GetPointerY());

    // 边沿、捕获、点击判定都在 `PointerProbe` 里（0.4.1 T-03）：
    // 那三条语义与静态档是同一套，抄在组件里就会走形。
    const detail::PointerProbeResult r = _probe.Update(hit, down);

    // 三态：按下 > 悬停 > 通常。按下用的是"这次按下算在按钮上"而不是"指针
    // 此刻在按钮上"——所以按着滑出去仍然是按下，和静态版的手感一致。
    // 喂入口是继承来的 `MouseHover` / `MousePress`（静态版由场景调它们，
    // 本类自己调），三态一变就会经 `onButtonStateChanged` 钩子同步基类颜色。
    //
    // **只喂主键**（0.5.0）：右键按下不该让按钮进 Pressed 态——那样"右键菜单"
    // 看起来像"左键点了一下"。
    const bool hoverChanged = MouseHover(r.hovered);
    const bool pressChanged =
        MousePress(_probe.IsCaptured(PointerButton::Primary));

    if (r.primary.clicked) {
        TriggerClick();
    }
    if (r.secondary.clicked) {
        // 次键：带**按下点**（不是此刻的指针）。长按后手指微动时，菜单不该跟着挪。
        const MousePoint origin =
            scene->GetPressOrigin(PointerButton::Secondary);
        TriggerContextClick(origin.x, origin.y);
    }
    return hoverChanged || pressChanged;
}

void InkingDynamicButton::onPointerStateChanged() {
    // **输入在这里发生**（0.4.1 T-02/T-03）：场景只在指针真的变了的那一帧
    // 推一次信号（鼠标不动就不推），比以前的"每个逻辑步自己拉"更准——
    // 50Hz 的逻辑步会白算（没动也在判）、也会晚判（最多差一个逻辑步，
    // 短于 20ms 的点击直接漏掉）。
    JudgePointer();
}

void InkingDynamicButton::onTick(float /*deltaSeconds*/) {
    // 这里**不再判输入**：输入已经改由 `onPointerStateChanged` 唤醒（见上）。
    // 这个钩子留给"几何 / 计时"那类每逻辑步推进的事——本类自己没有，
    // 但派生类（自检里的 `Mover`）重写它去改 offset，所以别把它删掉。
}

void InkingDynamicButton::onAnimationTick(float deltaSeconds) {
    // 三态之间的颜色过渡与变换走**渲染帧**这条通道（和静态版共用 ButtonLook）。
    // 它和上面的 onTick 各管各的：输入 / 几何在逻辑步，颜色/变换过渡在渲染帧。
    // 推进与"这一帧变了什么"都在契约基类里，本类只在它报出来后标脏
    // （见下面 `onButtonDisplayAdvanced`）。
    AdvanceButtonLook(deltaSeconds);
}

// ---------------------------------------------------------------------------
// 三态契约的两个 [√] 钩子：本类在"状态变了"和"显示值变了"两处挂副作用
// ---------------------------------------------------------------------------

void InkingDynamicButton::onButtonStateChanged(ButtonState /*from*/,
                                               ButtonState /*to*/) {
    // 只同步基类颜色（和静态版同一条），**不标脏**：三态切换的画面变化由
    // 状态机自己记成"待报告"，下一帧 `onButtonDisplayAdvanced` 会把它报出来。
    syncBaseColor();
}

void InkingDynamicButton::onButtonDisplayAdvanced(bool repaintDirty,
                                                  bool hitDirty) {
    if (repaintDirty) {
        syncBaseColor();
        // 画面变了 → 重绘脏（不是命中脏：颜色/变换不改表）。
        MarkRepaintDirty();
    }
    if (hitDirty) {
        // 变换变了 → 命中脏：它改变"点在不在形状里"的答案（反变换跟着变）。
        MarkDirty();
    }
}

// ---------------------------------------------------------------------------
// 渲染
// ---------------------------------------------------------------------------

void InkingDynamicButton::onRender(float pixelX, float pixelY) const {
    // 和静态版同一份实现（src/button/ButtonPaint.cpp）：动态按钮差的是
    // 几何与输入，不是"画出来长什么样"。
    // 颜色与变换都取**当前显示**那一份（命中那边对同一个变换取逆）。
    detail::PaintButtonAppearance(*this, GetShape(), GetAppearance(),
                                  _data.stroke, _data.shadow,
                                  GetDisplayColor(), GetDisplayTransform(),
                                  pixelX, pixelY);
    if (_data.HasLabel()) {
        detail::PaintButtonText(*this, _data, GetDisplayTransform(), pixelX,
                                pixelY);
    }
}

}  // namespace inking
