#pragma once

// 预制件 C5「路径 / 文件选择」（`docs/TaskGuide.md` 的 C 系列）。
//
// **路线已由用户裁定：组装自绘**（C3 列表 + B7 单行文本框 + C1 快捷位置 +
// 一条普通横排容器当按钮栏 + 盘符栏），不动框架、跨平台一致、可断言。理由写在
// `FilePickerData.h` 顶上。
//
// ---------------------------------------------------------------------------
// 三块必须一起守的事（不然就是"能跑但会炸在中文目录里"）
//
//   1. **路径全程 `std::filesystem::path`**（Windows 上就是宽字符）：
//      `AGENTS.md` §6 第 53 条——窄串构造 `path` 在中文目录里会**抛异常**，
//      而且异常发生在构造那一步，`error_code` 重载救不了。
//      往界面 / 事件里递的字符串一律先过 `PathToUtf8`。
//   2. **枚举要带 `error_code`**：权限不足的目录、突然消失的路径都不该让
//      选择器崩掉（`directory_iterator` 的异常版本会抛）。
//   3. **"确定"要分清三种模式**：开文件 / 存文件要的是**选中的文件**；
//      选目录要的是**当前目录**（选中项是次要的）。
//
// **换盘符**（`FilePickerStyle::drives` + `EnumerateDrives()`）：盘符单独占一行，
// 一行放得下十来个；这一栏是"会话内枚举一次"的（`RefreshDrives()` 手动重来），
// 不挂系统通知——U 盘热插拔很常见，但轮询托盘不是本组件的活。
//
// 第一版**不做**：原生对话框、书签 / 最近位置、文件名与目录分开输入
// （SaveFile 也走"选中的文件"）、多列 / 图标视图。

#include <widget/InkingStaticWidget.h>
#include <button/InkingStaticButton.h>
#include <event/InkingEvent.h>
#include <panel/InkingStaticPanel.h>
#include <prefab/FilePickerData.h>
#include <prefab/ListView.h>
#include <prefab/OptionBar.h>
#include <text/InkingTextInput.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace inking::prefab {

/**
 * @brief 当前目录里的一条（目录或文件）。
 *
 * `path` 是**宽字符路径**（`std::filesystem::path` 在 Windows 上就是 `wstring`），
 * `name` 是给界面看的 UTF-8 名字。两者都在这里，是因为"显示"和"用"要的编码不同
 * ——把它们混成一个 `std::string` 正是第 53 条那个坑的入口。
 */
struct FileEntry {
    std::filesystem::path path{};
    std::string name{};      ///< UTF-8
    bool isDirectory = false;
    std::uintmax_t size = 0;
};

/**
 * @brief C5：预制路径 / 文件选择（组装自绘）。
 *
 * ```cpp
 * inking::prefab::FilePickerStyle style = inking::prefab::FilePickerStyle::MakeDefault();
 * style.extensions = {".png", ".jpg"};
 * inking::prefab::FilePicker picker(&scene, style);
 * picker.accepted.Subscribe([](const std::vector<std::string>& paths) { ... });
 * picker.SetDirectory(std::filesystem::path(L"D:\\素材\\背景"));  // 宽字符
 * ```
 */
class FilePicker : public InkingStaticPanel {
public:
    /// 按了"确定"。参数是选中的路径（UTF-8）。开文件时是文件，选目录时是目录。
    InkingEvent<const std::vector<std::string>&> accepted;

    /// 按了"取消"。
    InkingEvent<> canceled;

    /// 当前目录换了（参数是 UTF-8 路径）。
    InkingEvent<const std::string&> directoryChanged;

    /// 选中项变了（参数是选中的路径，UTF-8）。
    InkingEvent<const std::vector<std::string>&> selectionChanged;

    explicit FilePicker(
        InkingWidget* parent,
        const FilePickerStyle& style = FilePickerStyle::MakeDefault());

    // ---------------- 目录 ----------------

    /**
     * 换当前目录并重新枚举。
     *
     * @return 真的是个目录（且枚举没出错）才返回 true；否则写一条错误日志、
     *         什么都不改（不静默：路径打错是最常见的一类输入）。
     */
    bool SetDirectory(const std::filesystem::path& directory);

    const std::filesystem::path& GetDirectory() const noexcept {
        return _directory;
    }

    /// 重新枚举当前目录（幂等；文件系统变了之后喊一声）。
    void Refresh();

    /// 上一层目录（已经在根上时返回 false）。
    bool GoUp();

    /// 进入第 `index` 个条目（它不是目录就返回 false）。
    bool EnterDirectory(int entryIndex);

    /// 用户主目录（快捷位置之一）。取不到时返回空 path。
    static std::filesystem::path HomeDirectory();

    /**
     * 当前能换的盘符（根路径，**带尾部分隔符**：`C:\`、`D:\` …）。
     *
     * - Windows：`GetLogicalDrives` + `GetDriveType`，跳过"没有根目录"的那几位
     *   （空软驱、断开的映射盘）——列出来也只会点出一个错误；
     * - 其它平台：**空**。盘符是 Windows 的词汇，POSIX 只有 `/`（快捷位置里的
     *   "根目录"就是它），所以那些平台上盘符栏整条不建。
     */
    static std::vector<std::filesystem::path> EnumerateDrives();

    /**
     * 重新枚举盘符并重建盘符栏（插了 U 盘 / 拔了移动硬盘之后喊一声）。
     *
     * 幂等：盘符没变时 `SetItems` 什么都不做（不重建节点）。
     *
     * ⚠️ 只重建**项**，不重排布局：静态档的几何在构造期就算死了，所以"运行期
     * 从 0 个盘符变成 1 个"不会把这一块撑出来。真要那种效果，请重新构造选择器。
     */
    void RefreshDrives();

    /// 盘符栏现在列了几项（0 = 这个平台没有盘符 / 被 `showDrives` 关掉了）。
    std::size_t GetDriveCount() const noexcept;

    /// 当前目录所在的盘符（`C:` 这样的显示名）；`_directory` 为空时是空串。
    std::string GetCurrentDrive() const;

    // ---------------- 按钮栏（确定 / 取消） ----------------

    /**
     * 按钮栏里建了几个按钮（正常是 2：确定 + 取消）。
     *
     * 观测口：它是**普通容器**，不是菜单栏——所以全屏 / 滚轮这些窗口语义碰不到它。
     * 自检靠这两条断言"窗口策略没有被继承回来"。
     */
    int GetButtonCount() const noexcept;

    /// 按钮栏现在可见吗（跟父级走；全屏不该动它）。
    bool IsButtonBarVisible() const noexcept;

    /// 拿第 `index` 个按钮（0 = 确定，1 = 取消）；越界返回 nullptr。
    InkingStaticButton* GetButton(int index) const noexcept;

    // ---------------- 条目与选择 ----------------

    const std::vector<FileEntry>& GetEntries() const noexcept { return _entries; }

    std::size_t GetEntryCount() const noexcept { return _entries.size(); }

    /// 当前选中的条目索引（升序）。
    const std::vector<int>& GetSelectedIndices() const noexcept;

    /// 选中了 0 个 / 1 个 / n 个。
    int GetSelectedCount() const;

    /// 选中的路径（UTF-8，按条目顺序）。
    std::vector<std::string> GetSelectedPaths() const;

    /// 路径框里现在显示的文本（UTF-8）。
    std::string GetPathText() const;

    /// 直接给路径框塞一段文本（等价于"在框里打完字"；**不会**立刻换目录）。
    void SetPathText(const std::string& text);

    // ---------------- 接受 / 取消 ----------------

    /**
     * 等于点"确定"。
     *
     * - `PickDirectory`：报**当前目录**（这就是"选目录"的语义）；
     * - `OpenFile` / `SaveFile`：报**选中的文件**（没选 / 只选了目录 → 返回 false
     *   并写一条错误日志）。
     */
    bool Accept();

    /// 等于点"取消"。
    void Cancel();

    // ---------------- 内部件（要细调就用它们） ----------------

    const FilePickerStyle& GetStyle() const noexcept { return _style; }

    ListView& List() noexcept { return _list; }
    OptionBar& Shortcuts() noexcept { return _shortcuts; }
    OptionBar& Drives() noexcept { return _drives; }

    /**
     * 底部按钮那一栏（确定 / 取消）。
     *
     * **类型是 `InkingStaticPanel`**（0.5.3 起）：早先返回 `MenuBar&`，于是对话框的
     * 按钮行继承了菜单栏的窗口语义（全屏自隐 / 滚轮缩放 / 快捷键表）。现在它只是一条
     * 横排容器，想细调就用 `GetStyle().buttonBar` + 这两个按钮。
     */
    InkingStaticPanel& Buttons() noexcept { return _buttons; }
    InkingTextInput& PathInput() noexcept { return _pathInput; }

private:
    /// 重新枚举 + 把结果交给列表（`Refresh` 的实现）。
    void rebuildEntries();

    /// 把"选中项变了"往外说（列表的选择事件转成路径）。
    void onListSelectionChanged();

    FilePickerStyle _style{};
    std::filesystem::path _directory{};
    std::vector<FileEntry> _entries{};

    InkingTextInput _pathInput;
    OptionBar _shortcuts;
    OptionBar _drives;
    ListView _list;
    InkingStaticPanel _buttons;
};

}  // namespace inking::prefab
