#pragma once

// 快捷键绑定表：**进程级的一张表**，由 `InkingWindow` 持有。
//
// ---------------------------------------------------------------------------
// 为什么这张表必须住在窗口层（而不是场景 / 菜单栏）
//
// 场景的键盘派发是"推给**焦点**节点"（`InkingScene::DispatchKeyboard`）——
// 它天生**不全局**：焦点在输入框里时，`Ctrl+S` 会被输入框拿走。
// 而快捷键的语义恰恰是"不管焦点在哪都要响应"，所以它必须住在焦点**之上**。
// 窗口层是唯一合适的位置（它同时持有键盘输入与主循环）。
//
// 菜单栏只是**最常用的注册者**，不是表的所有者：这样将来"命令面板"
// "全局搜索"能用同一张表，不必再发明一套。
// ---------------------------------------------------------------------------
//
// 生命周期：表活在进程级的 `InkingWindow` 里，注册者（例如 MenuBar）会先死。
// 所以**注册者必须在析构时 Unbind**（`Bind` 返回的 id 就是干这个的）。
// 这条和 `InkingEvent` 的令牌退订是同一个约定：谁挂的回调，谁负责摘。

#include <input/Shortcut.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace inking {

class KeyboardInput;

/**
 * @brief 快捷键 → 回调的绑定表。
 *
 * ```cpp
 * auto id = registry.Bind(inking::Shortcut::Parse("Ctrl+S"), Save, "保存");
 * registry.Unbind(id);
 * ```
 */
class ShortcutRegistry {
public:
    using Action = std::function<void()>;

    /// 绑定 id（**0 永远不是合法 id**，`Bind` 失败时返回它）。
    using Id = std::uint64_t;

    /**
     * 绑一个快捷键。
     *
     * **冲突策略：先到先得。** 已经有人占了同一个组合时返回 0 并记一条 WARN，
     * 不静默覆盖——"后来者悄悄干掉前者"是最难查的一类 bug（表现是某个菜单项
     * 突然不响应，而代码里看不出谁动了它）。要改绑就先 `Unbind` 再 `Bind`。
     *
     * @param shortcut 组合；空的（`Empty()`）直接返回 0，不报错。
     * @param action   回调；空的也返回 0。
     * @param label    人读的名字（冲突日志与自检提示用）。
     */
    Id Bind(const Shortcut& shortcut, Action action, std::string label = {});

    /// 按 id 退订。@return 真的退掉了才返回 true。
    bool Unbind(Id id);

    /// 这个组合被占了吗（注册前的预检 / 冲突提示）。
    bool IsTaken(const Shortcut& shortcut) const noexcept;

    /// 占着这个组合的 id（没人占返回 0）。
    Id Find(const Shortcut& shortcut) const noexcept;

    /// 现在绑了几条。
    std::size_t Count() const noexcept { return _entries.size(); }

    /// 某个 id 的 label（日志用；不存在返回空串）。
    std::string GetLabel(Id id) const;

    /**
     * 每帧派发一次：遍历**本帧的按键事件**，命中就执行回调并**消费掉那条事件**。
     *
     * 消费的语义很关键：焦点组件**不该**再收到这一下 `Ctrl+S`（否则输入框里
     * 会冒出一个控制字符）。但**跨帧的"按着"状态不动**——`IsKeyDown(Ctrl)`
     * 仍然为真，那是游戏 / 拖拽逻辑要的。
     *
     * 实现上分两步（先收集命中、再消费、最后执行回调）：回调里可能改这张表
     * （甚至析构注册者），那时再去动事件表就是"遍历中被改"。
     *
     * @return 这一帧真的触发了几个快捷键。
     */
    std::size_t Dispatch(KeyboardInput& input);

    /// 全部退订（窗口重建 / 自检复位用）。
    void Clear() noexcept { _entries.clear(); }

private:
    struct Entry {
        Id id = 0;
        Shortcut shortcut{};
        Action action{};
        std::string label{};
    };

    std::vector<Entry> _entries{};

    /// 只增不减：退订过的 id 不会被复用，避免"旧 id 打到新条目"。
    Id _nextId = 1;
};

}  // namespace inking
