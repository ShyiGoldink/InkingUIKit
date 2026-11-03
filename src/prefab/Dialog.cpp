#include <prefab/Dialog.h>

#include <config/window_config.h>
#include <detail/InkLog.h>
#include <input/KeyboardInput.h>
#include <panel/PanelData.h>
#include <prefab/PrefabTheme.h>
#include <scene/InkingScene.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <string>
#include <utility>

namespace inking_prefab {

namespace {

constexpr const char* kModuleName = "Dialog";

/// 遮罩那份面板数据：铺满、直角、半透明黑。
InkingPanelData MakeScrimData(const DialogData& data) {
    InkingPanelData scrim;
    scrim.name = data.name.empty() ? "dialog" : data.name;
    // 设计画布可能还没定尺寸（`ApplyDesignConfig()` 之前构造）——给 1 而不是 0，
    // 免得触发零尺寸护栏；真正打开时尺寸已经是定型的那一份。
    scrim.width = std::max(1, InkingDesignWidth());
    scrim.height = std::max(1, InkingDesignHeight());
    scrim.shape = InkingShapeSpec::Rect();
    scrim.closed.color = data.scrim.color;
    scrim.open.color = data.scrim.color;
    scrim.openInheritsClosed = false;
    return scrim;
}

/// 中间那块面板：居中（两个锚点都给 `Center`，于是位置完全由父级尺寸决定，
/// 运行期不用算一次）。
InkingPanelData MakeBoxData(const DialogData& data) {
    InkingPanelData box;
    box.name = (data.name.empty() ? "dialog" : data.name) + ".panel";
    box.width = std::max(1, data.width);
    box.height = std::max(1, data.height);
    box.shape = InkingShapeSpec::RoundedRect(data.radius);
    box.closed.color = data.panel.color;
    box.open.color = data.panel.color;
    box.openInheritsClosed = false;
    box.selfAnchor = InkingChangeAnchor::Center;
    box.traceAnchor = InkingChangeAnchor::Center;
    return box;
}

}  // namespace

DialogData DialogData::MakeDefault() {
    DialogData data;
    // 遮罩四成黑：既能看出"底下被压住了"，又不至于看不见底下的内容。
    // **不能写全透明**——`Normalize()` 会把两态全透明兜成不透明黑（见头文件）。
    data.scrim = InkingButtonAppearance(InkingAppearanceType::Color, 0x66000000u);
    data.panel = InkingButtonAppearance(InkingAppearanceType::Color, theme::kSurface);
    return data;
}

Dialog::Dialog(InkingWidget* parent, const DialogData& data)
    : InkingStaticPanel(parent, MakeScrimData(data)), _data(data) {
    // 中间那块面板：由本节点拥有（`AddChild`），所以不用管析构。
    auto box = std::make_unique<InkingStaticPanel>(nullptr, MakeBoxData(data));
    _box = static_cast<InkingStaticPanel*>(AddChild(std::move(box)));
    if (_box == nullptr) {
        // `AddChild` 总是成功；真失败就是"对话框里没有框"，那必须喊出来
        // （不静默：否则现象是"点了打开什么都没有"，而遮罩是在的）。
        INK_LOG_ERROR(std::string(kModuleName),
                      "对话框面板没能挂上（AddChild 返回空）：" + _data.name);
    }
    // 构造出来是关着的：不可见 ⇒ 不画、不命中（`IsVisibleInTree` 那条规则）。
    SetVisible(false);
}

void Dialog::Open() {
    if (_open) {
        return;  // 幂等
    }
    _open = true;
    SetVisible(true);
    if (InkingScene* scene = GetScene()) {
        // 焦点自己收：没有焦点，Esc / Enter 就没有地方落。
        scene->SetKeyboardFocus(this);
    }
}

void Dialog::Close() {
    if (!_open) {
        return;  // 幂等
    }
    _open = false;
    SetVisible(false);
    if (InkingScene* scene = GetScene()) {
        // 焦点若在本子树里就**当场释放**——焦点是场景级唯一归属，
        // 留着会指向一个看不见的节点。
        if (isInSubtree(scene->GetKeyboardFocus())) {
            scene->ClearKeyboardFocus();
        }
    }
}

bool Dialog::Accept() {
    if (!_open) {
        return false;
    }
    const int index = _defaultAction;
    // 顺序：**先关、再发事件**——订阅者在回调里读到的 `IsOpen()` 是假，
    // 想再开一次也不会和"正在关"打架（见头文件）。
    Close();
    accepted.Emit();
    action.Emit(index);
    return true;
}

bool Dialog::Cancel() {
    if (!_open) {
        return false;
    }
    Close();
    cancelled.Emit();
    return true;
}

InkingWidget* Dialog::AddItem(std::unique_ptr<InkingWidget> item) {
    if (_box == nullptr || item == nullptr) {
        return nullptr;
    }
    return _box->AddChild(std::move(item));
}

bool Dialog::isInSubtree(const InkingWidget* node) const noexcept {
    // 沿父链往上走：框架没给 `IsInSubtreeOf`，而这条链最多几层。
    for (const InkingWidget* cursor = node; cursor != nullptr;
         cursor = cursor->GetParent()) {
        if (cursor == this) {
            return true;
        }
    }
    return false;
}

void Dialog::onAnimationTick(float /*deltaSeconds*/) {
    if (!_open) {
        return;  // 关着的时候一个字节都不动（本钩子每渲染帧都会来）
    }
    InkingScene* scene = GetScene();
    if (scene == nullptr) {
        return;
    }
    if (isInSubtree(scene->GetKeyboardFocus())) {
        return;  // 焦点已经在子树里（例如点进了内容里的输入框）：不抢
    }
    // 点面板 / 点遮罩都会把焦点清掉（场景的规则），这里把它收回自己，
    // 于是 Esc / Enter 在整个"打开期间"都有落点（见头文件那段）。
    scene->SetKeyboardFocus(this);
}

bool Dialog::isInsidePanel(float designX, float designY) const {
    if (_box == nullptr) {
        return false;
    }
    const float x = _box->GetAbsX();
    const float y = _box->GetAbsY();
    return designX >= x && designX < x + static_cast<float>(_box->GetWidth()) &&
           designY >= y && designY < y + static_cast<float>(_box->GetHeight());
}

void Dialog::onPointerClick(InkingPointerButton button, float designX,
                            float designY) {
    if (button != InkingPointerButton::Primary) {
        return;  // 右键不当"点遮罩"
    }
    if (isInsidePanel(designX, designY)) {
        // 理论上到不了：面板是子节点，命中会先被它接住。留着是防"面板不可见
        // 但对话框还开着"那种中间态把面板内的点击当成遮罩点击。
        return;
    }
    if (_closeOnScrim) {
        Cancel();  // 语义：点遮罩 = 取消（不是"确认"）
    }
    // 默认什么都不做：防误触丢数据（任务书 §6 T-06 的第一行决定）。
}

void Dialog::onKeyDown(const InkingKeyEvent& key) {
    switch (key.key) {
        case SDLK_ESCAPE:
            Cancel();
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            Accept();
            break;
        default:
            break;  // 别的键不管（对话框不吞打字）
    }
}

}  // namespace inking_prefab
