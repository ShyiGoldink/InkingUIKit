#pragma once

#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace inking {

/**
 * @brief 本项目的任务模式。
 *
 * 属性：deps 依赖任务 / next 后继任务 / action 动作。
 *
 * 方法：
 *   dependOn(fn)         添加一个依赖，它的返回值会成为本节点 action 的入参之一
 *   dependOn(vector<fn>) 添加多个依赖，按传入顺序依次进入 action 的参数
 *   then(fn)             追加一个后继，本节点结果作为后继的唯一入参
 *   then(vector<fn>)     追加一串后继，前一个的输出进入后一个的入参
 *   execute(input)       按"左（依赖）→ 中（action）→ 右（后继）"执行
 */
template <typename T>
struct Task {
    /** 执行动作：入参是上游传来的值序列，返回本节点的结果。 */
    using Action = std::function<T(const std::vector<T>&)>;

    Action action;                            /** 动作（中） */
    std::vector<std::unique_ptr<Task>> deps;  /** 依赖（左，可有多个） */
    std::unique_ptr<Task> next;               /** 后继（右，最多一个） */

    /** 添加一个依赖：该依赖的返回值会成为本节点 action 的入参之一。 */
    Task& dependOn(Action fn) {
        return dependOn(std::vector<Action>{std::move(fn)});
    }

    /** 添加多个依赖：按传入顺序，各自的返回值依次进入本节点 action 的参数。 */
    Task& dependOn(std::vector<Action> fns) {
        for (auto& fn : fns) {
            auto dep = std::make_unique<Task>();
            dep->action = std::move(fn);
            deps.push_back(std::move(dep));
        }
        return *this;
    }

    /** 追加一个后继：本节点执行完后，把结果作为后继 action 的唯一入参。 */
    Task& then(Action fn) {
        return then(std::vector<Action>{std::move(fn)});
    }

    /** 追加多个后继：按传入顺序串成一条链，前一个的输出进入后一个的入参。 */
    Task& then(std::vector<Action> fns) {
        Task* tail = this;
        while (tail->next) {
            tail = tail->next.get();
        }

        for (auto& fn : fns) {
            auto step = std::make_unique<Task>();
            step->action = std::move(fn);
            tail->next = std::move(step);
            tail = tail->next.get();
        }
        return *this;
    }

    /**
     * @brief 执行本节点：按"左（依赖）→ 中（action）→ 右（后继）"的中序遍历完成。
     *
     * 没有依赖时，上游传入的 input 就是唯一入参；有依赖时，每个依赖独立执行
     * 并把结果按顺序聚合。action 的返回值再作为后继节点的入参继续传递。
     */
    T execute(const T& input) {
        std::vector<T> params;
        if (deps.empty()) {
            params.push_back(input);
        } else {
            params.reserve(deps.size());
            for (auto& dep : deps) {
                params.push_back(dep->execute(input));
            }
        }

        // 没有 action 的节点视为透传，原样传递 input
        T result = action ? action(params) : input;

        if (next) {
            result = next->execute(result);
        }
        return result;
    }
};

}  // namespace inking

