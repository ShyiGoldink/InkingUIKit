// 设计画布尺寸的**唯一一份状态**。
//
// 为什么它必须落在 .cpp 里：`WINDOW_EXPORT_ALL_SYMBOLS` 导出的是**编译单元里
// 的符号**，而头文件里的 `inline` 函数（尤其是"函数内静态量"那种写法）每个 TU
// 各有一份——DLL 与 exe 会各持一个自己的副本，于是"外面设了、里面读不到"，
// 且**不报任何错**。本仓库已经有三个同族槽位踩过这个坑
// （`currentRendererSlot` / `currentAncestorAffineSlot` / `TaskQueue::instance`），
// 这里是第四个：状态与读写它的三个函数全部落在本文件里，跨 DLL 边界只有函数。

#include <config/window_config.h>

#include <detail/InkLog.h>

#include <string>

namespace inking {

namespace {

constexpr const char* kModuleName = "Config";

/**
 * 设计尺寸 + "定型了没有"。
 *
 * 放在匿名命名空间的函数内静态量里：**同一份 TU 只有一份**，
 * 而这个 TU 只编进 libinking.dll，所以全进程（含 exe 侧调用）也就这一份。
 */
struct DesignState {
    int width  = kDefaultDesignWidth;
    int height = kDefaultDesignHeight;

    /// 已经有人按当前这对值算过几何（窗口 / 场景构造）。之后不许再改。
    bool taken = false;
};

DesignState& state() {
    static DesignState instance;
    return instance;
}

}  // namespace

int DesignWidth() noexcept {
    return state().width;
}

int DesignHeight() noexcept {
    return state().height;
}

void GetDesignSize(int& width, int& height) noexcept {
    const DesignState& s = state();
    width  = s.width;
    height = s.height;
}

bool SetDesignSize(int width, int height) {
    DesignState& s = state();

    if (width < 1 || height < 1 || width > kMaxDesignSize
        || height > kMaxDesignSize) {
        INK_LOG_ERROR(kModuleName,
                      "设计尺寸 " + std::to_string(width) + "x"
                          + std::to_string(height) + " 不合法（允许 1.."
                          + std::to_string(kMaxDesignSize) + "）");
        return false;
    }

    if (s.width == width && s.height == height) {
        return true;  // 幂等：重复设同一个值是正常用法（多份配置合并时常见）
    }

    if (s.taken) {
        // 这条是**故意报错而不是悄悄改**：根锚点、letterbox 逻辑尺寸、缩放档位
        // 都按定型那一刻的值算好了，事后改只会得到"一半新一半旧"的画面。
        // 用户看到这条日志，唯一正确的动作是把调用挪到 main() 第一行。
        INK_LOG_ERROR(kModuleName,
                      "设计尺寸已经定型为 " + std::to_string(s.width) + "x"
                          + std::to_string(s.height) + "（窗口或场景已经构造），"
                            "现在改成 " + std::to_string(width) + "x"
                          + std::to_string(height) + " 来不及了——"
                            "请把设定挪到构造任何场景 / 窗口之前");
        return false;
    }

    s.width  = width;
    s.height = height;
    INK_LOG_INFO(kModuleName,
                 "设计尺寸设为 " + std::to_string(width) + "x"
                     + std::to_string(height));
    return true;
}

namespace detail {

void NoteDesignSizeTaken() noexcept {
    state().taken = true;
}

bool DesignSizeTaken() noexcept {
    return state().taken;
}

}  // namespace detail

}  // namespace inking
