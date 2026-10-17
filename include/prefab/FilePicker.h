#pragma once

// 预制件 C5「路径 / 文件选择」（`docs/TaskGuide.md` 的 C 系列）。
//
// **路线已由用户裁定：组装自绘**（C3 列表 + B7 单行文本框 + C1 快捷位置 +
// C2 按钮栏），不动框架、跨平台一致、可断言。理由写在 `FilePickerData.h` 顶上。
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
// 第一版**不做**：原生对话框、磁盘驱动器列表、书签 / 最近位置、文件名与目录
// 分开输入（SaveFile 也走"选中的文件"）、多列 / 图标视图。

#include <widget/InkingStaticWidget.h>
#include <event/InkingEvent.h>
#include <panel/InkingStaticPanel.h>
#include <prefab/FilePickerData.h>
#include <prefab/ListView.h>
#include <prefab/MenuBar.h>
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
    MenuBar& Buttons() noexcept { return _buttons; }
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
    ListView _list;
    MenuBar _buttons;
};

}  // namespace inking::prefab
