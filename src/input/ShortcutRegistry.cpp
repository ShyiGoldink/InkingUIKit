#include <input/ShortcutRegistry.h>

#include <detail/InkLog.h>
#include <input/KeyboardInput.h>

#include <utility>

namespace inking {

namespace {
constexpr const char* kModuleName = "Shortcut";
}  // namespace

ShortcutRegistry::Id ShortcutRegistry::Bind(const Shortcut& shortcut,
                                            Action action, std::string label) {
    if (shortcut.Empty() || !action) {
        return 0;
    }
    if (const Id existing = Find(shortcut); existing != 0) {
        INK_LOG_WARN(std::string(kModuleName),
                     "快捷键 " + shortcut.ToString() + " 已被占用（"
                         + GetLabel(existing) + "），本次绑定被拒绝："
                         + (label.empty() ? std::string("<未命名>") : label));
        return 0;
    }

    Entry entry;
    entry.id = _nextId++;
    entry.shortcut = shortcut;
    entry.action = std::move(action);
    entry.label = std::move(label);
    _entries.push_back(std::move(entry));

    INK_LOG_DEBUG(std::string(kModuleName),
                  "绑定 " + shortcut.ToString() + " → " + _entries.back().label);
    return _entries.back().id;
}

bool ShortcutRegistry::Unbind(Id id) {
    if (id == 0) {
        return false;
    }
    for (auto it = _entries.begin(); it != _entries.end(); ++it) {
        if (it->id == id) {
            _entries.erase(it);
            return true;
        }
    }
    return false;
}

bool ShortcutRegistry::IsTaken(const Shortcut& shortcut) const noexcept {
    return Find(shortcut) != 0;
}

ShortcutRegistry::Id ShortcutRegistry::Find(const Shortcut& shortcut)
    const noexcept {
    if (shortcut.Empty()) {
        return 0;
    }
    for (const Entry& entry : _entries) {
        if (entry.shortcut == shortcut) {
            return entry.id;
        }
    }
    return 0;
}

std::string ShortcutRegistry::GetLabel(Id id) const {
    for (const Entry& entry : _entries) {
        if (entry.id == id) {
            return entry.label;
        }
    }
    return {};
}

std::size_t ShortcutRegistry::Dispatch(KeyboardInput& input) {
    if (_entries.empty()) {
        return 0;
    }

    const std::vector<KeyInputEvent>& events = input.GetFrameEvents();
    std::vector<std::size_t> consumed;
    std::vector<Action> fired;

    for (std::size_t index = 0; index < events.size(); ++index) {
        const KeyInputEvent& event = events[index];
        if (event.kind != KeyInputEvent::Kind::Down) {
            continue;  // 抬起与文本输入不参与快捷键
        }
        for (const Entry& entry : _entries) {
            if (entry.shortcut.Matches(event.key)) {
                consumed.push_back(index);
                fired.push_back(entry.action);  // 拷一份：回调可能改表
                break;  // 一个事件只触发一条（重复组合在 Bind 时就被挡住了）
            }
        }
    }

    if (fired.empty()) {
        return 0;
    }

    // **先消费、再执行**。执行回调时注册者（MenuBar）可能被析构，那时再回头
    // 动 `input` 的事件表就是"遍历中被改"；顺序反了会偶发越界。
    for (auto it = consumed.rbegin(); it != consumed.rend(); ++it) {
        input.ConsumeFrameEvent(*it);
    }
    for (const Action& action : fired) {
        if (action) {
            action();
        }
    }
    return fired.size();
}

}  // namespace inking
