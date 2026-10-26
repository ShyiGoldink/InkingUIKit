// Windows 的系统头要最先包含（`WIN32_LEAN_AND_MEAN` / `NOMINMAX` 必须在它之前
// 定义，否则会被别的头抢先拉进来——那之后这两个宏就无效了）。只为了盘符枚举
// 用到的两个函数，别的一概不从 `windows.h` 取。
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <prefab/FilePicker.h>

#include "PathUtf8.h"

#include <detail/InkLog.h>
#include <prefab/PrefabTheme.h>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <system_error>
#include <utility>

namespace inking::prefab {

namespace {

/// 五块内容在自身里的纵向位置（都从 0 起算，块之间留 `contentGap`）。
struct BlockLayout {
    float pathInputY = 0.0f;
    float shortcutsY = 0.0f;
    float drivesY = 0.0f;
    float listY = 0.0f;
    float buttonsY = 0.0f;
    float totalHeight = 0.0f;
    /// 盘符栏这一块到底占不占高度（平台上枚举不到盘符时它整条不建）。
    bool driveBar = false;
};

/**
 * 盘符栏要不要占位置：`showDrives` 关了就占 0，平台上没有盘符也占 0。
 *
 * 这里每次问都重新枚举一次系统盘符（构造期最多问六次，`GetLogicalDrives` 是
 * 微秒级）；换来的是"布局判定"与"真正建出来的项数"永远读同一个来源，不会出现
 * "说好的 30 高、里面一个盘符都没有"。
 */
bool ShouldShowDrives(const FilePickerStyle& style) {
    return style.showDrives && !FilePicker::EnumerateDrives().empty();
}

BlockLayout ComputeBlockLayout(const FilePickerStyle& style) {
    BlockLayout out;
    const float gap = style.contentGap;
    out.driveBar = ShouldShowDrives(style);
    out.pathInputY = 0.0f;
    out.shortcutsY = out.pathInputY + style.pathInputHeight + gap;
    out.drivesY = out.shortcutsY + style.shortcutHeight + gap;
    out.listY = out.drivesY + (out.driveBar ? style.driveBarHeight + gap : 0.0f);
    out.buttonsY = out.listY + style.listHeight + gap;
    out.totalHeight = out.buttonsY + style.buttonBarHeight;
    return out;
}

/// 盘符按钮上的字：根路径 `C:\` → `C:`（去掉尾部分隔符，一个字母加冒号才像盘符）。
std::string DriveLabel(const std::filesystem::path& root) {
    std::string text = PathToUtf8(root);
    while (!text.empty() && (text.back() == '\\' || text.back() == '/')) {
        text.pop_back();
    }
    return text;
}

PanelData MakePanelData(const FilePickerStyle& style) {
    PanelData panel = style.panel;
    if (panel.name.empty()) {
        panel.name = "filePicker";
    }
    if (panel.width <= 0) {
        panel.width = 640;
    }
    if (panel.height <= 0) {
        panel.height = static_cast<int>(ComputeBlockLayout(style).totalHeight + 0.5f);
    }
    // 路径框是四块里唯一会被键盘改内容的，所以焦点规则照 B7：它是自己声明要焦点的。
    // 这里只确保"块与块之间不互相盖住"——位置全在构造期算好（静态档）。
    return panel;
}

std::string ToLowerAscii(std::string text) {
    for (char& ch : text) {
        const unsigned char value = static_cast<unsigned char>(ch);
        if (value < 0x80u) {  // 只折 ASCII：UTF-8 的多字节部分不能被 toupper 拆坏
            if (value >= 'A' && value <= 'Z') {
                ch = static_cast<char>(value - 'A' + 'a');
            }
        }
    }
    return text;
}

/// 扩展名过滤（**小写比较、按后缀匹配**）。`extensions` 为空 = 全要。
bool MatchesExtension(const std::string& name,
                      const std::vector<std::string>& extensions) {
    if (extensions.empty()) {
        return true;
    }
    const std::string lower = ToLowerAscii(name);
    for (const std::string& extension : extensions) {
        const std::string wanted = ToLowerAscii(extension);
        if (wanted.empty() || lower.size() < wanted.size()) {
            continue;
        }
        if (lower.compare(lower.size() - wanted.size(), wanted.size(), wanted)
            == 0) {
            return true;
        }
    }
    return false;
}

ButtonData PickerButtonLook() { return MakeDefaultPickerButtonLook(); }

}  // namespace

ButtonData MakeDefaultPickerButtonLook() {
    ButtonData look;
    look.name = "filePickerButton";
    look.width = 96;
    look.height = 30;
    look.shape = ShapeSpec::RoundedRect(theme::kRadiusButton);
    // 亮色：白底 + 1px 浅描边；悬停 / 按下用主色浅底。
    look.normal = ButtonAppearance(AppearanceType::Color, theme::kSurface);
    look.hover = ButtonAppearance(AppearanceType::Color, theme::kAccentSoft);
    look.onclicked = ButtonAppearance(AppearanceType::Color,
                                      theme::kAccentSoftPressed);
    look.hoverInheritsNormal = false;
    look.onclickedInheritsNormal = false;
    look.stroke.width = 1.0f;
    look.stroke.color = theme::kOutline;
    look.text.fontSize = 15.0f;
    // 文字**居中**（"确定" / "取消"）：以前靠 leftSpace 手填，比例字体下必然歪。
    look.text.align = TextAlignH::Center;
    look.text.alignV = TextAlignV::Middle;
    look.text.leftSpace = 0.0f;
    look.text.topSpace = 0.0f;
    look.text.path = theme::DefaultFontPath();
    look.textColor = theme::kTextPrimary;
    return look;
}

FilePickerStyle FilePickerStyle::MakeDefault() {
    FilePickerStyle style;
    style.panel.name = "filePicker";
    style.panel.width = 640;
    style.panel.height = 480;
    style.panel.shape = ShapeSpec::RoundedRect(theme::kRadiusCard);
    style.panel.closed.color = theme::kSurface;
    style.panel.open.color = theme::kSurface;
    style.panel.openInheritsClosed = false;

    style.contentGap = 4.0f;
    style.pathInputHeight = 32.0f;
    style.shortcutHeight = 30.0f;
    style.listHeight = 300.0f;
    style.buttonBarHeight = 40.0f;

    style.pathInput.name = "filePickerPath";
    style.pathInput.width = 640;
    style.pathInput.height = 32;
    style.pathInput.style.fontSize = 15.0f;
    style.pathInput.style.padding = LayoutSpacing{8.0f, 6.0f, 8.0f, 6.0f};
    style.pathInput.style.fontPath = theme::DefaultFontPath();
    style.pathInput.color = theme::kTextPrimary;
    style.pathInput.caretColor = theme::kAccentStrong;

    // 快捷位置：横排一列小按钮（C1 的选项栏，横排）。
    style.shortcuts = OptionBarStyle::MakeDefault();
    style.shortcuts.panel.width = 640;
    style.shortcuts.panel.height = 30;
    style.shortcuts.panel.closed.color = 0x02000000u;  // 极淡（不能全透明）
    style.shortcuts.panel.open.color = style.shortcuts.panel.closed.color;
    style.shortcuts.panel.openInheritsClosed = false;
    style.shortcuts.panel.layout.kind = LayoutKind::Stack;
    style.shortcuts.panel.layout.axis = LayoutAxis::Horizontal;
    style.shortcuts.panel.layout.justify = LayoutJustify::Start;
    style.shortcuts.panel.layout.align = LayoutAlign::Stretch;
    style.shortcuts.panel.layout.gap = 4.0f;
    style.shortcuts.panel.layout.padding = LayoutSpacing{0.0f, 0.0f, 0.0f, 0.0f};
    style.shortcuts.itemHeight = 28.0f;
    style.shortcuts.itemLook = MakeDefaultPickerButtonLook();
    style.shortcuts.itemLook.width = 96;
    style.shortcuts.itemLook.height = 28;

    // 盘符栏：跟快捷位置同一套排列，只是**项更窄**（"C:" 只有两个字符）。
    // 52 宽 + 4 缝 = 一行放 11 个盘符，够用且不顶出 640。
    style.drives = style.shortcuts;
    style.drives.panel.height = 30;
    style.drives.itemLook.width = 52;
    style.driveBarHeight = 30.0f;
    style.showDrives = true;

    // 目录内容：默认**窗口化**（一个目录上千项很常见）。
    style.list = ListViewStyle::MakeDefault();
    style.list.panel.width = 640;
    style.list.panel.height = 300;
    style.list.rowHeight = 26.0f;
    style.list.rowLook = MakeDefaultRowLook();
    style.list.rowLook.width = 0;
    style.list.rowLook.height = 26;
    style.list.windowed = true;
    style.list.overscan = 2;

    // 底部按钮栏：横向、靠右（`SpaceBetween` 会把两项推到两端，这里要"偏右"）。
    //
    // **不用 `MenuBar`**（0.5.3）：它自带 `hideInFullscreen` / `zoomWheel` 等窗口语义，
    // 而对话框的按钮行只是"横向排两个按钮"。这里只借它的排列口径（同一套 Stack
    // 词汇），组件换成普通 `PanelData` —— 见 `FilePickerData.h` 的抬头。
    style.buttonBar.name = "filePickerButtonBar";
    style.buttonBar.width = 640;
    style.buttonBar.height = 40;
    style.buttonBar.closed.color = 0x02000000u;
    style.buttonBar.open.color = style.buttonBar.closed.color;
    style.buttonBar.openInheritsClosed = false;
    style.buttonBar.layout.kind = LayoutKind::Stack;
    style.buttonBar.layout.axis = LayoutAxis::Horizontal;
    style.buttonBar.layout.justify = LayoutJustify::End;
    style.buttonBar.layout.align = LayoutAlign::Center;
    style.buttonBar.layout.gap = 8.0f;
    style.buttonBar.layout.padding = LayoutSpacing{0.0f, 4.0f, 8.0f, 4.0f};
    style.buttonLook = MakeDefaultPickerButtonLook();

    style.mode = FilePickMode::OpenFile;
    style.multiSelect = false;
    style.showHidden = false;
    style.pathPlaceholder = "输入目录路径后回车";
    style.acceptText = "确定";
    style.cancelText = "取消";
    return style;
}

FilePicker::FilePicker(InkingWidget* parent, const FilePickerStyle& style)
    : InkingStaticPanel(parent, MakePanelData(style)),
      _style(style),
      _pathInput(this, [&style] {
          TextInputData data = style.pathInput;
          data.width = style.panel.width;
          data.height = static_cast<int>(style.pathInputHeight + 0.5f);
          data.traceAnchor = InkingChangeAnchor::LeftTop;
          data.selfAnchor = InkingChangeAnchor::LeftTop;
          data.offsetX = 0.0f;
          data.offsetY = ComputeBlockLayout(style).pathInputY;
          return data;
      }()),
      _shortcuts(this, [&style] {
          OptionBarStyle options = style.shortcuts;
          options.panel.width = style.panel.width;
          options.panel.height = static_cast<int>(style.shortcutHeight + 0.5f);
          options.panel.traceAnchor = InkingChangeAnchor::LeftTop;
          options.panel.selfAnchor = InkingChangeAnchor::LeftTop;
          options.panel.offsetX = 0.0f;
          options.panel.offsetY = ComputeBlockLayout(style).shortcutsY;
          return options;
      }()),
      _drives(this, [&style] {
          OptionBarStyle options = style.drives;
          options.panel.width = style.panel.width;
          // 枚举不到盘符时这一块给 0 高：它反正会被 `SetVisible(false)`，但布局
          // 也不能给别人留一个洞（见 `ShouldShowDrives`）。
          options.panel.height =
              ShouldShowDrives(style)
                  ? static_cast<int>(style.driveBarHeight + 0.5f)
                  : 0;
          options.panel.traceAnchor = InkingChangeAnchor::LeftTop;
          options.panel.selfAnchor = InkingChangeAnchor::LeftTop;
          options.panel.offsetX = 0.0f;
          options.panel.offsetY = ComputeBlockLayout(style).drivesY;
          return options;
      }()),
      _list(this, [&style] {
          ListViewStyle list = style.list;
          list.panel.width = style.panel.width;
          list.panel.height = static_cast<int>(style.listHeight + 0.5f);
          list.panel.traceAnchor = InkingChangeAnchor::LeftTop;
          list.panel.selfAnchor = InkingChangeAnchor::LeftTop;
          list.panel.offsetX = 0.0f;
          list.panel.offsetY = ComputeBlockLayout(style).listY;
          list.selection = style.multiSelect ? ListSelectionMode::Multiple
                                             : ListSelectionMode::Single;
          return list;
      }()),
      _buttons(this, [&style] {
          PanelData bar = style.buttonBar;
          bar.width = style.panel.width;
          bar.height = static_cast<int>(style.buttonBarHeight + 0.5f);
          bar.traceAnchor = InkingChangeAnchor::LeftTop;
          bar.selfAnchor = InkingChangeAnchor::LeftTop;
          bar.offsetX = 0.0f;
          bar.offsetY = ComputeBlockLayout(style).buttonsY;
          return bar;
      }()) {
    // ---- 路径框：一开始显示提示文字；回车换目录 ----
    _pathInput.SetEditText(_style.pathPlaceholder);
    _pathInput.submitted.Subscribe([this](const std::string& text) {
        const std::filesystem::path wanted = Utf8ToPath(text);
        if (!SetDirectory(wanted)) {
            INK_LOG_ERROR(std::string(kFilePickerModuleName),
                          "路径框里的目录进不去：" + text);
        }
    });

    // ---- 快捷位置：上级 / 根 / 主目录 ----
    std::vector<OptionItem> shortcuts;
    shortcuts.push_back({"up", "上级目录", true, 0.0f});
    shortcuts.push_back({"root", "根目录", true, 0.0f});
    shortcuts.push_back({"home", "主目录", true, 0.0f});
    _shortcuts.SetItems(std::move(shortcuts));
    _shortcuts.optionClicked.Subscribe([this](const std::string& id, std::size_t) {
        if (id == "up") {
            GoUp();
        } else if (id == "root") {
            SetDirectory(_directory.root_path());
        } else if (id == "home") {
            SetDirectory(HomeDirectory());
        }
    });

    // ---- 盘符栏：点一个盘符 = 直接进它的根目录 ----
    //
    // 项的 id 存的是**完整根路径**（`drive:C:\`），不是字母：字母拼回 `C:` 是
    // drive-relative 路径（"当前盘上的当前目录"），不是 `C:\`——那是两回事，
    // 而且在小写 / 大小写混写下还能骗过眼睛。
    _drives.optionClicked.Subscribe([this](const std::string& id, std::size_t) {
        static constexpr char kDrivePrefix[] = "drive:";
        const std::size_t prefixLength = sizeof(kDrivePrefix) - 1;  // 不吃结尾 '\0'
        if (id.size() <= prefixLength
            || id.compare(0, prefixLength, kDrivePrefix) != 0) {
            return;
        }
        const std::string root = id.substr(prefixLength);
        if (!SetDirectory(Utf8ToPath(root))) {
            INK_LOG_ERROR(std::string(kFilePickerModuleName),
                          "这个盘符进不去：" + root);
        }
    });
    RefreshDrives();
    if (!ShouldShowDrives(_style)) {
        // 平台没有盘符这一说（或调用方关掉了）：整条不露面，也不占高度
        // ——高度在 `_drives` 的构造里已经给成 0。
        _drives.SetVisible(false);
    }

    // ---- 列表：点一下 = 进目录（开文件模式）/ 选中；选择变化往外说 ----
    _list.itemActivated.Subscribe([this](const std::string&, int index) {
        if (index < 0 || index >= static_cast<int>(_entries.size())) {
            return;
        }
        if (_entries[static_cast<std::size_t>(index)].isDirectory
            && _style.mode != FilePickMode::PickDirectory) {
            EnterDirectory(index);  // 开 / 存文件时：点目录就进去（编辑器常见手感）
        }
    });
    _list.selectionChanged.Subscribe(
        [this](const std::string&, int) { onListSelectionChanged(); });

    // ---- 底部按钮 ----
    //
    // 两个按钮是按钮栏的**直接子项**，位置由按钮栏的 Stack（横向 / 靠右 / 交叉轴居中）
    // 排出来——不自己算 x。宽度必须显式来自 `buttonLook.width`：`align = Center` 只管
    // 交叉轴，主轴尺寸仍是各自配置，留 0 会变成"两个按钮各占半条"。
    ButtonData buttonLook = _style.buttonLook.width > 0
                                ? _style.buttonLook
                                : PickerButtonLook();
    const auto addButton = [this, &buttonLook](const char* name, const std::string& label,
                                               bool accept) {
        ButtonData look = buttonLook;
        look.name = name;
        look.label = label;
        auto* button = static_cast<InkingStaticButton*>(
            _buttons.AddChild(std::make_unique<InkingStaticButton>(&_buttons, look)));
        if (button != nullptr) {
            button->SetOnClicked([this, accept] {
                if (accept) {
                    Accept();
                } else {
                    Cancel();
                }
            });
        }
    };
    addButton("filePickerButton.accept", _style.acceptText, true);
    addButton("filePickerButton.cancel", _style.cancelText, false);
}

// ---------------------------------------------------------------------------
// 目录
// ---------------------------------------------------------------------------

bool FilePicker::SetDirectory(const std::filesystem::path& directory) {
    if (directory.empty()) {
        INK_LOG_ERROR(std::string(kFilePickerModuleName), "目录为空，忽略");
        return false;
    }
    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec) || ec) {
        INK_LOG_ERROR(std::string(kFilePickerModuleName),
                      "不是目录（或读不到）：" + PathToUtf8(directory));
        return false;
    }

    _directory = directory;
    _pathInput.SetEditText(PathToUtf8(directory));
    rebuildEntries();
    directoryChanged.Emit(PathToUtf8(_directory));
    return true;
}

void FilePicker::Refresh() { rebuildEntries(); }

void FilePicker::rebuildEntries() {
    _entries.clear();

    if (_directory.empty()) {
        _list.SetItems({});
        return;
    }

    // 带 error_code 的枚举：权限不足 / 目录刚被删掉都不该让选择器崩掉
    // （异常版本会抛——那种"点一下就崩"比"显示空目录"糟得多）。
    std::error_code ec;
    std::filesystem::directory_iterator iterator(_directory, ec);
    if (ec) {
        INK_LOG_ERROR(std::string(kFilePickerModuleName),
                      "枚举目录失败：" + PathToUtf8(_directory) + "（"
                          + ec.message() + "）");
        _list.SetItems({});
        return;
    }

    for (const std::filesystem::directory_entry& entry : iterator) {
        std::error_code entryEc;
        const bool isDirectory = entry.is_directory(entryEc);
        if (entryEc) {
            continue;  // 单个条目读不到就跳过它（别为它放弃整个目录）
        }

        const std::filesystem::path& path = entry.path();
        std::string name = PathToUtf8(path.filename());
        if (name.empty()) {
            name = PathToUtf8(path);
        }
        if (!_style.showHidden && !name.empty() && name[0] == '.') {
            continue;
        }
        if (!isDirectory) {
            if (_style.mode == FilePickMode::PickDirectory) {
                continue;  // 选目录时只列目录
            }
            if (!MatchesExtension(name, _style.extensions)) {
                continue;
            }
        }

        FileEntry item;
        item.path = path;
        item.name = std::move(name);
        item.isDirectory = isDirectory;
        if (!isDirectory) {
            std::error_code sizeEc;
            item.size = entry.file_size(sizeEc);
            if (sizeEc) {
                item.size = 0;
            }
        }
        _entries.push_back(std::move(item));
    }

    // 目录在前，各自按名字（UTF-8 字节序；中文按码点排，够用）。
    std::sort(_entries.begin(), _entries.end(),
              [](const FileEntry& a, const FileEntry& b) {
                  if (a.isDirectory != b.isDirectory) {
                      return a.isDirectory;
                  }
                  return a.name < b.name;
              });

    std::vector<ListItem> rows;
    rows.reserve(_entries.size());
    for (std::size_t i = 0; i < _entries.size(); ++i) {
        const FileEntry& entry = _entries[i];
        // id 用索引：路径本身可能带中文，而这里只需要一个能对回来的键。
        rows.push_back(ListItem{std::to_string(i),
                                entry.isDirectory ? entry.name + "/" : entry.name,
                                true});
    }
    _list.SetItems(std::move(rows));
    _list.ClearSelection();
    _list.ScrollTo(0.0f, 0.0f);  // 换目录之后回到顶部
}

bool FilePicker::GoUp() {
    if (_directory.empty()) {
        return false;
    }
    const std::filesystem::path parent = _directory.parent_path();
    if (parent.empty() || parent == _directory) {
        return false;  // 已经在根上
    }
    return SetDirectory(parent);
}

bool FilePicker::EnterDirectory(int entryIndex) {
    if (entryIndex < 0 || entryIndex >= static_cast<int>(_entries.size())) {
        return false;
    }
    const FileEntry& entry = _entries[static_cast<std::size_t>(entryIndex)];
    if (!entry.isDirectory) {
        return false;
    }
    return SetDirectory(entry.path);
}

std::filesystem::path FilePicker::HomeDirectory() {
#if defined(_WIN32)
    // 必须走宽字符版本：`getenv("USERPROFILE")` 拿到的是 ANSI/GBK，
    // 而中文用户名用窄串构造 `path` 会抛（`AGENTS.md` §6 第 53 条）。
    const wchar_t* home = _wgetenv(L"USERPROFILE");
    if (home != nullptr && *home != L'\0') {
        return std::filesystem::path(std::wstring(home));
    }
    return {};
#else
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        return std::filesystem::path(home);
    }
    return {};
#endif
}

// ---------------------------------------------------------------------------
// 盘符（换盘符 = C5 的增补能力）
// ---------------------------------------------------------------------------

std::vector<std::filesystem::path> FilePicker::EnumerateDrives() {
    std::vector<std::filesystem::path> roots;
#if defined(_WIN32)
    // 位图：第 0 位 = A:，第 25 位 = Z:。
    const DWORD mask = ::GetLogicalDrives();
    if (mask == 0) {
        return roots;
    }
    for (int index = 0; index < 26; ++index) {
        if ((mask & (static_cast<DWORD>(1) << index)) == 0) {
            continue;
        }
        const wchar_t letter = static_cast<wchar_t>(L'A' + index);
        const wchar_t text[4] = {letter, L':', L'\\', L'\0'};
        // 空软驱 / 断开的映射盘也会占一位：点进去只会得到一条错误日志
        // （`SetDirectory` 不静默），不如不列。
        if (::GetDriveTypeW(text) == DRIVE_NO_ROOT_DIR) {
            continue;
        }
        roots.emplace_back(std::filesystem::path(text));
    }
#else
    // 盘符是 Windows 的词汇：POSIX 的"根"只有一个 `/`，快捷位置里的"根目录"
    // 已经覆盖它，所以这里返回空——盘符栏整条不建（而不是画一条空栏）。
#endif
    return roots;
}

void FilePicker::RefreshDrives() {
    std::vector<OptionItem> items;
    for (const std::filesystem::path& root : EnumerateDrives()) {
        // id 存**完整根路径**（见构造里那段注释：字母拼 `C:` 是 drive-relative）。
        items.push_back(OptionItem{"drive:" + PathToUtf8(root), DriveLabel(root),
                                   true, 0.0f});
    }
    _drives.SetItems(std::move(items));
}

std::size_t FilePicker::GetDriveCount() const noexcept {
    return _drives.GetItemCount();
}

std::string FilePicker::GetCurrentDrive() const {
    if (_directory.empty()) {
        return {};
    }
    return DriveLabel(_directory.root_path());
}

// ---------------------------------------------------------------------------
// 按钮栏（确定 / 取消）：普通容器，窗口语义碰不到它
// ---------------------------------------------------------------------------

int FilePicker::GetButtonCount() const noexcept {
    return static_cast<int>(_buttons.GetOwnedChildCount());
}

bool FilePicker::IsButtonBarVisible() const noexcept {
    // 按**整棵树**回答：自检问的是"全屏之后还看得见吗"，而全屏藏的是祖先或它自己。
    return _buttons.IsVisibleInTree();
}

InkingStaticButton* FilePicker::GetButton(int index) const noexcept {
    if (index < 0 || index >= GetButtonCount()) {
        return nullptr;
    }
    return static_cast<InkingStaticButton*>(_buttons.GetChildren()[static_cast<std::size_t>(index)]);
}

// ---------------------------------------------------------------------------
// 条目与选择
// ---------------------------------------------------------------------------

const std::vector<int>& FilePicker::GetSelectedIndices() const noexcept {
    return _list.GetSelection();
}

int FilePicker::GetSelectedCount() const {
    return static_cast<int>(_list.GetSelection().size());
}

std::vector<std::string> FilePicker::GetSelectedPaths() const {
    std::vector<std::string> paths;
    for (int index : _list.GetSelection()) {
        if (index < 0 || index >= static_cast<int>(_entries.size())) {
            continue;
        }
        paths.push_back(PathToUtf8(_entries[static_cast<std::size_t>(index)].path));
    }
    return paths;
}

void FilePicker::onListSelectionChanged() {
    selectionChanged.Emit(GetSelectedPaths());
}

std::string FilePicker::GetPathText() const { return _pathInput.GetEditText(); }

void FilePicker::SetPathText(const std::string& text) {
    _pathInput.SetEditText(text);
}

// ---------------------------------------------------------------------------
// 接受 / 取消
// ---------------------------------------------------------------------------

bool FilePicker::Accept() {
    std::vector<std::string> paths;

    if (_style.mode == FilePickMode::PickDirectory) {
        // 选目录：要的就是**当前目录**（列表里点目录是"进去"，不是"选中"）。
        if (_directory.empty()) {
            INK_LOG_ERROR(std::string(kFilePickerModuleName),
                          "还没有进入任何目录，无法确定");
            return false;
        }
        paths.push_back(PathToUtf8(_directory));
    } else {
        for (int index : _list.GetSelection()) {
            if (index < 0 || index >= static_cast<int>(_entries.size())) {
                continue;
            }
            const FileEntry& entry = _entries[static_cast<std::size_t>(index)];
            if (!entry.isDirectory) {
                paths.push_back(PathToUtf8(entry.path));
            }
        }
        if (paths.empty()) {
            INK_LOG_ERROR(std::string(kFilePickerModuleName),
                          "没有选中文件（目录不算）——先点一个文件再确定");
            return false;
        }
    }

    accepted.Emit(paths);
    return true;
}

void FilePicker::Cancel() { canceled.Emit(); }

}  // namespace inking::prefab
