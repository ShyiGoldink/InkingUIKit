# 预制件（Prefab）

**回到API目录：**
[API index](../API.md)

预制件 = **用现成组件组装出来的复合件**。它们**不新增任何底层能力**：
没有一处新的绘制 / 命中 / 布局代码，全都"白拿"自 Panel / 布局通道 / 事件槽。

为什么需要这一层：`CXXCSS` 只描述**单层**组件，所以"容器 + 子项"这件事一直没有归处。
预制件补的就是这个空——**组装封装一次，以后按数据复用**，用户侧只给数据（项列表 + 外观）。

| 预制件 | 就是 | 头文件 |
| --- | --- | --- |
| `MenuBar` | 顶部横向容器（可当自绘标题栏） | `prefab/MenuBar.h` |
| `OptionBar` | 布局排列的按钮组 | `prefab/OptionBar.h` |
| `ListView` | 滚动容器 + 布局 + 只建可见项 | `prefab/ListView.h` |
| `Select` | 收起按钮 + 就地展开的选项栏（+ 可选过滤框） | `prefab/Select.h` |
| `FilePicker` | 路径框 + 快捷位置 + 盘符栏 + 列表 + 按钮栏 | `prefab/FilePicker.h` |
| `MenuPopup` | 定点弹出的竖排菜单（项可绑局部快捷键） | `prefab/MenuPopup.h` |
| `ListRow` | 带选中色的列表行（`InkingStaticButton`） | `prefab/ListRow.h` |
| `PrefabTheme` | 一套配色 / 圆角 / 字体路径常量 | `prefab/PrefabTheme.h` |

所有预制件都在 `inking::prefab` 命名空间下（`ListRow` 也是）。

## 一、MenuBar

**它就是**窗口顶部一条横向容器，用来替掉系统标题栏 / 菜单。里面不仅能放按钮，
还能放图片和**任意 `InkingWidget`**——所以核心只有一个入口 `AddItem(任意锚点)`。

```C++
class MenuBar : public InkingStaticPanel {
public:
    using ItemFactory    = std::function<std::unique_ptr<InkingWidget>()>;
    using ButtonCallback = std::function<void()>;

    explicit MenuBar(InkingWidget* parent, const MenuBarStyle& style = MenuBarStyle::MakeDefault());
    ~MenuBar() override;

    // 内容项
    InkingWidget* AddItem(std::unique_ptr<InkingWidget> item);
    InkingWidget* AddItem(const ItemFactory& factory);
    InkingStaticButton* AddButton(const ButtonData& look, const std::string& text,
                                  ButtonCallback onActivate = {},
                                  const Shortcut& shortcut = {});   // ← 第 4 参绑全局快捷键

    std::size_t GetBoundShortcutCount() const noexcept;
    std::size_t GetItemCount() const noexcept;
    ShapeBounds GetItemRect(std::size_t index) const;
    InkingWidget* GetItemNode(std::size_t index) const;

    void SetJustify(LayoutJustify justify);
    LayoutJustify GetJustify() const noexcept;

    // 自绘标题栏
    void SetDraggable(bool draggable) noexcept;
    bool IsDraggable() const noexcept;
    bool IsDragging() const noexcept;
    void AddCaptionButtons();                       // 右上角 最小化 / 关闭
    std::size_t GetCaptionButtonCount() const noexcept;

    void SetHideInFullscreen(bool hide) noexcept;
    bool IsHiddenByFullscreen() const noexcept;

    bool HandlesSubtreeWheel() const noexcept override;   // ← 必须：吃滚轮做缩放

    // 事件
    InkingEvent<> closeClicked;
    InkingEvent<> minimizeClicked;
};
```

### 内部怎么让"内容偏左 + 窗口按钮贴右上角"同时成立

一条 `justify` 表达不了这件事：`SpaceBetween` 会把**所有**项往两端推。所以内部插一根
**弹簧**（`InkingStaticWidget` + `layoutItem.fillMain = true`，宽 0）：主轴剩余空间全被它
吃掉，它后面的窗口按钮自然贴到右边——而内容项在它前面，照旧按调用方给的 `justify` 排。

### MenuBarStyle

```C++
struct MenuBarStyle {
    PanelData panel{};            // 高度 / 背景 / 形状 + 排列规则（layout）
    ButtonData captionLook{};     // 最小化 / 关闭 的外观模板
    bool draggable = false;       // 按住空白处拖窗口（无边框标题栏行为）
    bool captionButtons = false;  // 右上角加两个按钮
    float captionWidth = 40.0f;
    std::uint32_t textColor = 0;  // 0 = 用按钮外观自带的那份
    bool hideInFullscreen = true; // 全屏自动隐藏（订阅窗口事件，不轮询）
    bool zoomWheel = true;        // 滚轮做窗口缩放
    int zoomWheelStep = 1;

    static MenuBarStyle MakeDefault();   // 横向、偏左、深色底
};
```

**必须重写 `HandlesSubtreeWheel()`**（本类已重写）：否则指针停在菜单按钮上时，滚轮派给
按钮、菜单栏毫无反应。声明之后场景会沿父链找**最近**的声明者，把滚轮先交给它
（即"容器优先"）。

`hideInFullscreen` 的隐藏只记"被这条规则藏起来了"；退出全屏只恢复那一次，
**不覆盖业务自己调过的 `SetVisible(false)`**。

## 二、OptionBar

**它就是** `layout` 排列的按钮组——一个 Panel 容器 + 每个选项一个按钮。
"加一个选项"= 往 `std::vector<OptionItem>` 里加一个对象。

```C++
struct OptionItem {
    std::string id;          // 回调带出去的就是它（空 = 索引的十进制串）
    std::string text;        // 显示文字（空 = 用 id）
    bool enabled = true;
    float height = 0.0f;     // 0 = 用 style.itemHeight
    bool operator==(const OptionItem&) const noexcept;   // SetItems 靠它判要不要重建
};

class OptionBar : public InkingStaticPanel {
public:
    explicit OptionBar(InkingWidget* parent, const OptionBarStyle& style = OptionBarStyle::MakeDefault());

    void SetItems(std::vector<OptionItem> items);        // 幂等：内容没变不重建
    const std::vector<OptionItem>& GetItems() const noexcept;
    std::size_t GetItemCount() const noexcept;
    ShapeBounds GetItemRect(std::size_t index) const;
    InkingStaticButton* GetItemNode(std::size_t index) const;
    std::string ItemId(std::size_t index) const;
    std::size_t GetBuiltItemCount() const noexcept;

    LayoutSize MeasureLayout(const LayoutConstraints&) const override;
    bool Activate(std::size_t index);        // 程序化"点"某一项
    const OptionBarStyle& GetStyle() const noexcept;

    void SetLayerZ(int zIndex);              // 整块提到某一层（Select 用）
    int GetLayerZ() const noexcept;

    InkingEvent<const std::string&, std::size_t> optionClicked;
};
```

```C++
struct OptionBarStyle {
    PanelData panel{};           // 尺寸 / 背景 / 裁剪 / 滚动 / 排列规则（layout）
    ButtonData itemLook{};       // 每项外观模板
    float itemHeight = 40.0f;
    std::uint32_t textColor = 0; // 0 = 用 itemLook 自带的那份
    static OptionBarStyle MakeDefault();
};
```

**排列规则写在 `panel.layout` 里**，不在本结构里再抄一份 axis / gap / padding
（两份真相迟早分叉）。默认：竖向 `Stack` + `gap = 6` + `padding = 6` + 交叉轴拉伸。

**"禁用"是怎么落的**：静态按钮没有禁用态这个词汇（三态只有常 / 悬 / 按），
所以 `enabled = false` 落成两件事——① 回调**不发**；② 文字调暗（借用 `SetTextColor`）。
背景仍会随悬停变化：这是"用现有配件组装"的边界，写在明处。

## 三、ListView

**它就是** `ScrollView`（Panel 的 `scroll` 段）+ 布局 + 动态项组装。

```C++
struct ListItem {
    std::string id;      // 空 = 索引的十进制串
    std::string text;    // 空 = 用 id
    bool enabled = true;
};

enum class ListSelectionMode : std::uint8_t { None = 0, Single, Multiple };

class ListView : public InkingStaticPanel {
public:
    explicit ListView(InkingWidget* parent, const ListViewStyle& style = ListViewStyle::MakeDefault());

    void SetItems(std::vector<ListItem> items);
    const std::vector<ListItem>& GetItems() const noexcept;
    std::size_t GetItemCount() const noexcept;
    std::string ItemId(int index) const;

    const std::vector<int>& GetSelection() const noexcept;
    bool IsSelected(int index) const;
    void SetSelected(int index, bool selected);
    void ClearSelection();

    void SyncVisibleWindow();                 // 窗口化：按当前滚动量重建可见行
    std::size_t GetBuiltRowCount() const noexcept;
    int GetBuiltFirstIndex() / GetBuiltLastIndex() const noexcept;

    ShapeBounds GetRowRect(int index) const;
    ListRow* GetRowNode(int index) const;

    void ScrollTo(float x, float y);
    void ScrollToItem(int index);

    float GetRowHeight() const noexcept;
    float GetContentHeight() const noexcept;
    const ListViewStyle& GetStyle() const noexcept;
    bool IsWindowed() const noexcept;

    InkingEvent<const std::string&, int> selectionChanged;
    InkingEvent<const std::string&, int> itemActivated;
    InkingEvent<> selectionSetChanged;
};
```

```C++
struct ListViewStyle {
    PanelData panel{};
    ButtonData rowLook{};
    float rowHeight = 32.0f;               // 必须 > 0（窗口化靠它算位置）
    std::uint32_t selectedColor = 0xFF3E6E9Eu;
    ListSelectionMode selection = ListSelectionMode::Single;
    bool windowed = false;                 // 只建可见项
    int overscan = 2;                      // 窗口化的上下余量行
    std::uint32_t textColor = 0;
    static ListViewStyle MakeDefault();
};
```

### "只建可见项"是怎么做的

框架**不动**：`ListView` 按当前滚动量算出可见区间 `[first, last]`，只把这几行建成子节点，
再在两头各放一个**撑位置的占位块**（`InkingStaticWidget`）——前面那个把第一行顶到
`first × 行高`，后面那个把内容总高补到 `项数 × 行高`。于是：

- 滚动范围仍然是"全部项"（后面那块撑出来的）；
- 可见的行落在**正确的位置**上（前面那块顶出来的）；
- 节点数只跟"看得见几行"有关，跟总项数无关。

硬判据：1000 项时 `GetBuiltRowCount()` 仍然只有十几二十个，`GetItemCount()` 才是 1000。

占位块承担两个"框架没给写入口"的地方：静态档改不了自己的几何，布局规则也是构造期定死的
——所以"把第一行顶下去"只能靠**在它前面放一个已知高度的兄弟**。这是"用现有配件组装"
那条路的解法，不是绕过。

**滚动与裁剪由预制件强制打开**（列表不能滚就不是列表）；`panel.scroll` 的其余字段
（步长 / 条宽 / 颜色）照用户配的用。

第一版**不做**：拖拽重排、行内编辑、多列 / 排序、树形缩进。

## 四、Select

**它就是**收起态一个按钮（当前值 + 占位提示 + 展开指示），展开态**就地**显示一个
`OptionBar`——不引入浮层、不引入输入消费。

```C++
class Select : public InkingStaticPanel {
public:
    explicit Select(InkingWidget* parent, const SelectStyle& style = SelectStyle::MakeDefault());

    void SetOptions(std::vector<OptionItem> options);
    const std::vector<OptionItem>& GetOptions() const noexcept;
    std::size_t GetOptionCount() const noexcept;

    void SetSelected(int index);
    int GetSelectedIndex() const noexcept;
    std::string GetSelectedId() const;
    std::string GetSelectedText() const;

    void SetExpanded(bool expanded);
    bool IsExpanded() const noexcept;
    void ToggleExpanded();

    std::size_t GetOptionNodeCount() const noexcept;
    std::size_t GetVisibleOptionNodeCount() const noexcept;

    void SetFilterText(const std::string& text);      // 子串匹配，大小写不敏感
    const std::string& GetFilterText() const noexcept;
    std::size_t GetVisibleOptionCount() const noexcept;
    int RawIndexForVisible(int visibleIndex) const noexcept;
    bool ActivateVisible(int visibleIndex);
    bool ActivateOption(int index);

    const SelectStyle& GetStyle() const noexcept;
    OptionBar& Options() noexcept;
    InkingStaticButton& ClosedButton() noexcept;
    InkingTextInput& FilterInput() noexcept;

    InkingEvent<const std::string&, int> selectionChanged;
    InkingEvent<bool> expandedChanged;
};
```

```C++
struct SelectStyle {
    ButtonData closedLook{};
    OptionBarStyle options{};
    SelectExpandDirection direction = SelectExpandDirection::Down;
    float expandWidth = 0.0f;          // 0 = 跟收起按钮一样宽
    float expandHeight = 0.0f;         // 0 = 用 options.panel.height
    float expandGap = 2.0f;
    bool filterable = false;
    TextInputData filterLook{};
    float filterHeight = 30.0f;
    std::string placeholder = "请选择";
    int zIndex = 0;
    int expandedZIndex = 200;          // 展开时整块提到这一层
    static SelectStyle MakeDefault();
};
```

**展开/收起只改可见性**（`SetVisible`），那是骨架变化 ⇒ 命中表跟着重烘，
所以"展开的那一列点得到、收起之后点不到"是白拿的。

三条边界：

1. **点别处收起由调用方约定**：本类只提供 `SetExpanded(false)`。真做"点外部自动关"
   要浮层 / 全局输入那套词汇，第一版不做；
2. **展开容器的溢出不被剪**：它挂在收起按钮**外面**，而 `Select` 自己不能 `clipChildren`
   （否则展开的那一列会被自己剪掉）。容器里的东西盖不住容器外的兄弟，那是局部层级
   （暂缓）；
3. **第一版不做**：浮出式展开、多选、可直接输入（除过滤框外）、首字母定位。

**过滤的实现是"每渲染帧比一次文本"**：`InkingTextInput` 没有"文本变了"的事件槽，
而为一个可选特性去改那个组件不值当。轮询是 O(1) 的字符串比较，且只在展开时做。

## 五、FilePicker

**它就是**五块拼起来：路径框（`InkingTextInput`）→ 快捷位置（`OptionBar`）→
盘符栏（`OptionBar`）→ 目录内容（`ListView`）→ 按钮栏（**普通横排容器**，`PanelData`）。

> **按钮栏不是 `MenuBar`**（0.5.3 起）：`MenuBar` 是"贴着窗口的横条"，数据面捆着
> `hideInFullscreen` / `zoomWheel`（默认都 true）等窗口语义，实现里还订阅窗口的
> `fullscreenChanged`。对话框的一行按钮只要"横向排好"，所以只用 `PanelData` 的
> `layout`，不参与全屏隐藏 / 滚轮缩放。**布局复用、策略不复用。**

```C++
struct FileEntry {
    std::filesystem::path path{};
    std::string name{};      // UTF-8
    bool isDirectory = false;
    std::uintmax_t size = 0;
};

enum class FilePickMode : std::uint8_t { OpenFile = 0, SaveFile, PickDirectory };

class FilePicker : public InkingStaticPanel {
public:
    explicit FilePicker(InkingWidget* parent,
                        const FilePickerStyle& style = FilePickerStyle::MakeDefault());

    bool SetDirectory(const std::filesystem::path& directory);   // ← path，不是 string
    const std::filesystem::path& GetDirectory() const noexcept;
    void Refresh();
    bool GoUp();
    bool EnterDirectory(int entryIndex);
    static std::filesystem::path HomeDirectory();

    static std::vector<std::filesystem::path> EnumerateDrives();  // C:\ D:\ …
    void RefreshDrives();                                        // U 盘热插拔后喊一声
    std::size_t GetDriveCount() const noexcept;
    std::string GetCurrentDrive() const;                         // "C:"

    const std::vector<FileEntry>& GetEntries() const noexcept;
    std::size_t GetEntryCount() const noexcept;
    const std::vector<int>& GetSelectedIndices() const noexcept;
    int GetSelectedCount() const;
    std::vector<std::string> GetSelectedPaths() const;   // UTF-8
    std::string GetPathText() const;
    void SetPathText(const std::string& text);

    bool Accept();     // 按 mode 决定"选中的文件"还是"当前目录"
    void Cancel();

    const FilePickerStyle& GetStyle() const noexcept;
    ListView& List() noexcept;
    OptionBar& Shortcuts() noexcept;
    OptionBar& Drives() noexcept;
    InkingStaticPanel& Buttons() noexcept;   // 0.5.3 起不再是 MenuBar
    InkingTextInput& PathInput() noexcept;
    int GetButtonCount() const noexcept;     // 正常是 2（确定 / 取消）
    bool IsButtonBarVisible() const noexcept;// 全屏不该影响它
    InkingStaticButton* GetButton(int index) const noexcept;

    InkingEvent<const std::vector<std::string>&> accepted;
    InkingEvent<> canceled;
    InkingEvent<const std::string&> directoryChanged;
    InkingEvent<const std::vector<std::string>&> selectionChanged;
};
```

```C++
struct FilePickerStyle {
    PanelData panel{};
    TextInputData pathInput{};
    OptionBarStyle shortcuts{};
    OptionBarStyle drives{};                 // 盘符栏（每项 52 宽："C:"）
    ListViewStyle list{};
    PanelData buttonBar{};                   // 底部按钮栏（普通横排容器，不是 MenuBar）
    ButtonData buttonLook{};                 // 宽度就是按钮宽度（Stack 不拉伸主轴）

    std::vector<std::string> extensions;     // 小写比较；空 = 全要（目录永远显示）
    FilePickMode mode = FilePickMode::OpenFile;
    bool multiSelect = false;
    bool showHidden = false;
    bool showDrives = true;                  // 枚举不到盘符时这一栏整条不建

    float contentGap = 4.0f;
    float pathInputHeight = 32.0f;
    float shortcutHeight = 30.0f;
    float driveBarHeight = 30.0f;
    float listHeight = 300.0f;
    float buttonBarHeight = 40.0f;

    std::string pathPlaceholder = "输入目录路径后回车";
    std::string acceptText = "确定";
    std::string cancelText = "取消";

    static FilePickerStyle MakeDefault();     // 640×480、竖向五块、无过滤、单文件
};
```

### 三块必须一起守的事（不然就是"能跑但会炸在中文目录里"）

1. **路径全程 `std::filesystem::path`**（Windows 上就是宽字符）：窄串构造 `path` 在中文
   目录里会**抛异常**，而且异常发生在构造那一步，`error_code` 重载救不了。
   往界面 / 事件里递的字符串一律先过 `PathToUtf8`（内部工具）。
2. **枚举要带 `error_code`**：权限不足的目录、突然消失的路径都不该让选择器崩掉。
3. **"确定"要分清三种模式**：开文件 / 存文件要的是**选中的文件**；
   选目录要的是**当前目录**（选中项是次要的）。

### 盘符栏（换盘符）

- **枚举**：`EnumerateDrives()` → `C:\`、`D:\` …（Windows 用 `GetLogicalDrives` +
  `GetDriveType`，跳过"没有根目录"的空软驱 / 断开的映射盘——列出来也只会点出一条错误）；
  其它平台**返回空**，盘符栏整条不建（不是画一条空栏：布局判定和项数读同一个来源）。
- **项的 id 是 `drive:<完整根路径>`**（`drive:C:\`），不是字母。字母拼回 `C:` 是
  drive-relative 路径（"当前盘上的当前目录"），那是另一个位置。
- **盘符单独占一块**（`FilePickerStyle::drives`，每项 52 宽）：选项栏每项同宽，
  塞进快捷位置那三项（96 宽）后面就会顶出 640。盘符很短，52 宽一行放 11 个。
- `RefreshDrives()` 重枚举（U 盘热插拔后喊一声）；幂等——盘符没变就不重建节点。
- ⚠️ **只重建项，不重排布局**：静态档的几何在构造期就算死了，运行期从 0 个盘符
  变成 1 个不会把这一块撑出来；要那种效果请重新构造选择器。

第一版**不做**：原生对话框、书签 / 最近位置、文件名与目录分开输入、
多列 / 图标视图。

## 六、ListRow

```C++
class ListRow : public InkingStaticButton {
public:
    bool SetSelected(bool selected);      // 变了才 true
    bool IsSelected() const noexcept;
    std::uint32_t GetSelectedColor() const noexcept;
};
```

选中态的实现是**一层覆盖**：把外观那一笔的颜色换成 `selectedColor`（文字照旧）。
真要做"选中态是外观的第四态"得改按钮的三态模型，那是底层的事。

## 七、PrefabTheme

命名空间 `inking::prefab::theme`，一组 `inline constexpr` 常量 + 一个函数：

```C++
kSceneBackground / kSurface / kSurfaceAlt / kSurfaceSunken / kOutline
kTextPrimary / kTextMuted / kTextDisabled
kAccent / kAccentStrong / kAccentSoft / kAccentSoftPressed / kHoverNeutral
kScrollTrack / kScrollThumb / kScrollThumbHover
kSuccess / kWarning / kDanger

kRadiusPanel = 16 / kRadiusCard = 18 / kRadiusItem = 12 / kRadiusButton = 10 / kRadiusCaption = 8

std::string DefaultFontPath();
```

默认是一套**浅色**主题（浅底 + 白卡 + 紫调强调色）。它只是"一套能看的默认值"，
业务应当用自己那份配置覆盖。

## 常见坑

1. **给菜单栏 / 列表的 `rowLook` 三态继承没关**：`ButtonData::Normalize()` 会拿
   `normal` 覆盖 `hover` / `onclicked`，你给的那两份被悄悄盖掉。想不同就必须显式把
   `hoverInheritsNormal` / `onclickedInheritsNormal` 置 false。
2. **`rowHeight <= 0`**：窗口化靠它算位置，直接坏掉。
3. **`ListView` 不 `SyncVisibleWindow()`**：窗口化之后画面停在旧区间。
4. **`Select` 自己开 `clipChildren`**：展开的那一列被自己剪掉。
5. **`FilePicker::SetDirectory` 传窄串**：中文目录抛异常。传 `std::filesystem::path`。
6. **菜单栏不重写 `HandlesSubtreeWheel`**：滚轮被指针下的按钮吃掉（本类已重写，别删）。

---

> 本文档对应版本：**0.5.3（0.x 发布版）**。
