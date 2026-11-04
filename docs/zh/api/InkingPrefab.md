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
| `Slider` | 轨道 + 已填充 + 圆盘的滑块（点 / 拖 / 键盘） | `prefab/Slider.h` |
| `ProgressBar` | 确定进度条（只读） | `prefab/ProgressBar.h` |
| `Checkbox` / `Radio` / `Switch` | 勾选三件套（+ 显式 `RadioGroup`） | `prefab/ToggleLike.h` |
| `Tooltip` | 悬停提示（延迟弹出、不挡点击） | `prefab/Tooltip.h` |
| `Dialog` | 模态对话框（遮罩 + 居中面板 + Esc/Enter） | `prefab/Dialog.h` |
| `ToastHost` | 通知条宿主（连发堆叠、到期自消失） | `prefab/ToastHost.h` |
| `TabBar` | 标签页（顶 / 底 / 左三档、切页改可见性） | `prefab/TabBar.h` |
| `PrefabTheme` | 一套配色 / 圆角 / 字体路径常量 | `prefab/PrefabTheme.h` |

所有预制件都在 `prefab` 命名空间下（`ListRow` 也是）。

上一段那句"不新增底层能力"对**复合件**（MenuBar / OptionBar / ListView / Select /
FilePicker / MenuPopup / ListRow）严格成立；`Slider` / `ProgressBar` / 三件套 / 浮层三件套 / `TabBar` 是 0.9.0 新增的
**单件**（叶子控件），它们确实有自己的 `onRender`——但绘制只调
`inking_detail::fillShape`（全仓库唯一的填充入口），命中与绘制读同一份 `GetParts()`，
没有第二份坐标计算。

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
    InkingStaticButton* AddButton(const InkingButtonData& look, const std::string& text,
                                  ButtonCallback onActivate = {},
                                  const InkingShortcut& shortcut = {});   // ← 第 4 参绑全局快捷键

    std::size_t GetBoundShortcutCount() const noexcept;
    std::size_t GetItemCount() const noexcept;
    InkingShapeBounds GetItemRect(std::size_t index) const;
    InkingWidget* GetItemNode(std::size_t index) const;

    void SetJustify(InkingLayoutJustify justify);
    InkingLayoutJustify GetJustify() const noexcept;

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
    InkingPanelData panel{};            // 高度 / 背景 / 形状 + 排列规则（layout）
    InkingButtonData captionLook{};     // 最小化 / 关闭 的外观模板
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
    InkingShapeBounds GetItemRect(std::size_t index) const;
    InkingStaticButton* GetItemNode(std::size_t index) const;
    std::string ItemId(std::size_t index) const;
    std::size_t GetBuiltItemCount() const noexcept;

    InkingLayoutSize MeasureLayout(const InkingLayoutConstraints&) const override;
    bool Activate(std::size_t index);        // 程序化"点"某一项
    const OptionBarStyle& GetStyle() const noexcept;

    void SetLayerZ(int zIndex);              // 整块提到某一层（Select 用）
    int GetLayerZ() const noexcept;

    InkingEvent<const std::string&, std::size_t> optionClicked;
};
```

```C++
struct OptionBarStyle {
    InkingPanelData panel{};           // 尺寸 / 背景 / 裁剪 / 滚动 / 排列规则（layout）
    InkingButtonData itemLook{};       // 每项外观模板
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

    InkingShapeBounds GetRowRect(int index) const;
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
    InkingPanelData panel{};
    InkingButtonData rowLook{};
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
    InkingButtonData closedLook{};
    OptionBarStyle options{};
    SelectExpandDirection direction = SelectExpandDirection::Down;
    float expandWidth = 0.0f;          // 0 = 跟收起按钮一样宽
    float expandHeight = 0.0f;         // 0 = 用 options.panel.height
    float expandGap = 2.0f;
    bool filterable = false;
    InkingTextInputData filterLook{};
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
盘符栏（`OptionBar`）→ 目录内容（`ListView`）→ 按钮栏（**普通横排容器**，`InkingPanelData`）。

> **按钮栏不是 `MenuBar`**（0.5.3 起）：`MenuBar` 是"贴着窗口的横条"，数据面捆着
> `hideInFullscreen` / `zoomWheel`（默认都 true）等窗口语义，实现里还订阅窗口的
> `fullscreenChanged`。对话框的一行按钮只要"横向排好"，所以只用 `InkingPanelData` 的
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
    InkingPanelData panel{};
    InkingTextInputData pathInput{};
    OptionBarStyle shortcuts{};
    OptionBarStyle drives{};                 // 盘符栏（每项 52 宽："C:"）
    ListViewStyle list{};
    InkingPanelData buttonBar{};                   // 底部按钮栏（普通横排容器，不是 MenuBar）
    InkingButtonData buttonLook{};                 // 宽度就是按钮宽度（Stack 不拉伸主轴）

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

## 七、Slider

```C++
struct SliderData : public InkingAnchorData {   // 定位 / 尺寸 / 层级复用锚点那一份
    std::string name = "slider";
    float min = 0, max = 1, value = 0, step = 0;  // step <= 0 = 连续
    float trackHeight = 6, thumbDiameter = 18;
    InkingButtonAppearance track, fill, thumb;    // 三份外观（§2.5.2）
    InkingStrokeSpec thumbStroke;
    InkingShapeSpec trackShape = InkingShapeSpec::Capsule();
    InkingShapeSpec thumbShape = InkingShapeSpec::Circle();
    static SliderData MakeDefault();             // 主题色那一套
};

struct SliderParts { InkingShapeBounds track, fill, thumb; };

class Slider : public InkingStaticWidget {
public:
    InkingEvent<float, float> changed;   // (现在, 之前)，**只在真变时**发
    InkingEvent<> released;              // 主键抬起一次；业务提交用它
    bool SetValue(float v);              // 夹取 + 量化；没变返回 false
    float GetValue() const noexcept;
    float GetRatio() const noexcept;     // 0~1（max == min 时恒 0，不给 NaN）
    const SliderParts& GetParts() const noexcept;
    bool HitTest(float worldX, float worldY) const override;
    bool WantsKeyboardFocus() const noexcept override;  // 要（←/→/Home/End）
};
```

```C++
inking_prefab::Slider slider(&panel);            // 默认 200×28 / 0~1 / 连续
slider.changed.Subscribe([](float now, float before) { SetVolume(now); });
slider.released.Subscribe([] { SaveSettings(); });   // 拖完才落盘
```

四条会咬人的：

1. **热区是整块组件框**，不是那 6 像素高的轨道条（6px 在 1080p 下点不中）。
   "圆盘不单独进命中"这条由"没有第二个热区"满足。
2. **圆盘比轨道高是设计**：默认盘 18、轨 6，所以"盘落在轨道内"只在**水平方向**
   成立，纵向同心、垂直溢出。硬把盘裁进轨道会变成"半个圆"。
3. **值没变就不发事件**：同一位置连点两次，第二次 `changed` 一次都不来
   （`SetValue` 返回 false 且一个脏都不标）。
4. **量化相对 `min` 取整**，不是相对 0——否则 `[-1, 1]` 步长 `0.5` 取不到 `min`。

第一版没有 `SetPainter`（见"常见坑"第 7 条），也不做竖向 / 双滑块。

## 八、ProgressBar

```C++
struct ProgressBarData : public InkingAnchorData {
    std::string name = "progressBar";
    float progress = 0;                            // 0~1，构造时夹
    InkingButtonAppearance track, fill;
    InkingShapeSpec shape = InkingShapeSpec::Capsule();
    static ProgressBarData MakeDefault();
};

struct ProgressBarParts { InkingShapeBounds track, fill; };

class ProgressBar : public InkingStaticWidget {
public:
    bool SetProgress(float p);                     // 夹到 [0,1]；没变返回 false
    float GetProgress() const noexcept;
    const ProgressBarParts& GetParts() const noexcept;
};
```

**只读**：没有事件槽、不吃点击、不进键盘焦点。进度 0 时**不画**填充块（不是画一个
0 宽块——0 宽的三角化没有意义，还会多一条提交记录）。每帧喂同一个值零额外重绘。

填充宽与 `Slider` 共用 `prefab/PrefabParts.h` 的 `FillBoundsInTrack`（一份实现，
两个调用方）——抄两份就会出现"滑块 74.0 像素、进度条 73.6 像素"那种并排才看得出的差。

## 九、勾选三件套（Checkbox / Radio / Switch）

一个基类 + 三个子类 + 一个显式的组（都在 `prefab/ToggleLike.h`）：

```C++
struct ToggleData : public InkingAnchorData {
    std::string name = "toggle";
    bool checked = false;
    InkingButtonAppearance base, mark;       // 框底/圆底/轨道 + 勾/圆点/滑块
    InkingStrokeSpec baseStroke;
    InkingShapeSpec baseShape, markShape;    // markShape 默认 Rect() 是"用内置的"
    float markInset = 0.18f;                 // Checkbox/Radio 的内缩比例
    float knobInset = 2.0f;                  // Switch 滑块与轨道边的间距
};

struct ToggleParts { InkingShapeBounds base, mark; };

ToggleData MakeCheckboxData();   // 18×18 白底 + 主色勾
ToggleData MakeRadioData();      // 18×18 白底 + 主色圆点
ToggleData MakeSwitchData();     // 40×22 沉色轨道 + 白滑块

class ToggleLike : public InkingStaticWidget {
public:
    InkingEvent<> clicked;       // 变成选中 / 开（**程序调 SetChecked 也会发**）
    InkingEvent<> cancelled;     // 变成未选中 / 关
    bool IsChecked() const noexcept;
    bool SetChecked(bool checked);           // 同值返回 false、零事件
    const ToggleParts& GetParts() const noexcept;
};

class Checkbox : public ToggleLike { /* 内置勾：两段圆头线 */ };
class Radio : public ToggleLike {
public:
    void SetGroup(RadioGroup* group) noexcept;   // 不接管所有权
};

class RadioGroup {                 // 只管互斥，不拥有成员
    void Add(Radio*); void Remove(Radio*);
    void Select(Radio*);           // 先清其它、再选它
    Radio* GetChecked() const noexcept;
};

class Switch : public ToggleLike {
    void SetAnimated(bool animated) noexcept;   // 关掉后立刻到位
    float GetKnobRatio() const noexcept;        // 0 = 关端，1 = 开端
};
```

```C++
inking_prefab::Checkbox agree(&panel);
agree.clicked.Subscribe([]{ Enable(); });
agree.cancelled.Subscribe([]{ Disable(); });

inking_prefab::RadioGroup mode;            // 互斥由它负责
mode.Add(&radioA); mode.Add(&radioB);      // 用 radio.SetGroup(&mode) 也一样
radioA.SetGroup(&mode); radioB.SetGroup(&mode);
```

五条会咬人的：

1. **事件只有一对槽**（`clicked` / `cancelled`），没有 `SetOnClicked`。三个控件差别只在
   **谁触发**：Checkbox / Switch 是用户点它；**Radio 是"同组另一个被选中"**。
2. **`RadioGroup::Select` 一次发 1 + N 条**：目标 `clicked`、组内其它各一条 `cancelled`。
   这是设计（"一组变化各发各的"）——业务上"从谁换到谁"一个 bool 表达不了。
3. **点已选中的 Radio 不翻转、不发事件**（单选不可反选）；没挂组的 `Radio` 单用也安全，
   只是没有互斥。
4. **`SetChecked` 只标重绘脏**：几何没变，所以命中表不重烘、父容器不重排。
5. **Switch 的状态只由滑块位置表达**：轨道颜色不随状态变。要变色就自己订阅两个槽换
   `base` 的外观（这一版没有 `SetData`，所以那意味着重建组件）——写在这里免得当成 bug。

`markShape` 默认 `Rect()` 是一个**哨兵**：表示"用内置的那个 mark"（Checkbox 画勾、
其余画圆）。给它别的形状（`Circle` / `PolygonPath`）就按那个画——"换勾的样式"于是只是
换一个形状。内置勾是**两段圆头线**（不是 `PolygonPath`：路径顶点是组件本地坐标、不随框
缩放，换个尺寸就得换一份点表）。

## 十、Tooltip

```C++
struct TooltipData {
    std::string fontPath; float fontSize = 14.0f;
    float delay = 0.4f;                        // 停留多久才弹
    float paddingX = 10, paddingY = 6, maxWidth = 320, radius = 8, gap = 6;
    InkingButtonAppearance panel;              // 深色底
    std::uint32_t textColor = 0xFFFFFFFF;
    static TooltipData MakeDefault();
};

class Tooltip : public InkingDynamicPanel {
public:
    bool ShowFor(InkingWidget* anchor, const std::string& text);  // 开始计时
    void Hide();                                                  // 复位 + 收起
    bool IsShowing() const noexcept;    // **真的画出来了**
    bool IsPending() const noexcept;    // 还在计时
    bool HitTest(float, float) const override { return false; }   // 不参与命中
    InkingWidget* HitTestNode(float, float) noexcept override;    // 恒 nullptr
};
```

四条：

1. **它不挡点击**：`HitTest` 恒 false、`HitTestNode` 恒 nullptr（子树里有什么都不算）。
   所以"提示盖在按钮上、点下去还是按钮"是构造上成立的，不靠调用方配。
2. **不基于 `InkingFixedPopup`**：那个类 `OwnsSubtreeHit()` 为真 = 吃掉子树命中，
   和 Tooltip 要的"穿透"正好相反。
3. **计时挂在 `onAnimationTick`**（每渲染帧、带 delta、非 const）。所以探针能按帧
   精确喂时间，不依赖墙上时间。⚠️ **计时期间节点必须可见**——不可见的节点收不到
   任何每帧钩子，计时就永远不走。因此 `IsVisible()` 在计时期间已是真，
   "用户看得到"要看 `IsShowing()`。
4. 落位复用 `inking_detail::SolvePopupPlacement`：定点 = 锚点矩形底部中点、
   期望 `(0.5, 0)` 向下展开；下方放不下就翻到上方再夹紧。

## 十一、Dialog

```C++
struct DialogData {
    int width = 420, height = 240; float radius = 16;
    InkingButtonAppearance panel, scrim;       // scrim 默认 0x66000000
    static DialogData MakeDefault();
};

class Dialog : public InkingStaticPanel {
public:
    InkingEvent<> accepted, cancelled;
    InkingEvent<int> action;                   // 确认时带"默认动作"索引
    void Open(); void Close();                 // Close 不发任何事件
    bool Accept(); bool Cancel();              // 关 → 再发事件
    bool IsOpen() const noexcept;
    InkingWidget* AddItem(std::unique_ptr<InkingWidget>);
    void SetDefaultAction(int index) noexcept;
    void SetCloseOnScrimClick(bool on) noexcept;   // 默认 false
};
```

**它不是 `InkingFixedPopup`**（与任务书初稿不同，理由见任务书 §9.3）：

- `FixedPopup` 是**点锚定**的（`OpenAt(x,y)` 贴着指针展开），而对话框是**居中**的；
- 模态要挡住的是**整块画布**，所以必须有一个铺满的实体去命中。于是本类**自己就是
  遮罩**（静态面板、几何 = 设计画布），中间那块框是它的子面板——命中、绘制顺序、
  "关掉就完全不存在"三件事一起成立，不需要两个节点各抬一次 z。

模态的四条行为定死，不留给调用方猜：

| 项 | 行为 |
| - | - |
| 点遮罩 | 默认**不关**（防误触丢数据）；`SetCloseOnScrimClick(true)` 之后 = 取消 |
| Esc | 关 + `cancelled` |
| Enter | 关 + `accepted` + `action(默认索引)` |
| 焦点 | 打开即夺取；**每次点面板被清掉后下一帧自己收回来**（不然 Esc 从此没落点）；关掉即释放 |

事件顺序固定"**先关、再发**"：订阅者在回调里读到 `IsOpen()` 为假。

## 十二、ToastHost

```C++
struct ToastData {
    float width = 260, marginRight = 24, marginBottom = 24, gap = 8;
    float paddingX = 12, paddingY = 8, radius = 10, fontSize = 14;
    InkingButtonAppearance panel; std::uint32_t textColor;
    static ToastData MakeDefault();
};

class ToastHost : public InkingDynamicPanel {
public:
    InkingEvent<int> dismissed;               // 带 Show() 返回的 id
    int Show(const std::string& text, float seconds = 2.0f);   // 返回 id（从 1 起）
    std::size_t GetToastCount() const noexcept;
    void Clear();                             // 不发 dismissed
};
```

四条：

1. **对指针透明**（同 Tooltip）——通知不该挡点击。
2. **位置自己算**：栈的增删是运行期的，而布局求解器那套是构造期定型。
   从下往上堆，新的贴最下面。
3. **到期在 `onAnimationTick` 里"先收集 → 循环外统一删 → 再重排"**，不在遍历中途删。
4. `dismissed` 带的是**id**不是下标：前面那条消失之后下标会整体前移。
   `Clear()` 不发 `dismissed`——那是"到期"的通知，不是"被清掉"。

## 十三、TabBar

```C++
enum class TabBarSide : std::uint8_t { Top, Bottom, Left };

struct TabBarData : public InkingAnchorData {   // 定位 / 尺寸复用锚点那一份
    std::string name = "tabBar";
    TabBarSide side = TabBarSide::Top;
    float stripThickness = 36.0f;    // 标签条厚度
    float tabLength = 96.0f;         // 单个标签沿条方向的长度（固定值，不均分）
    float gap = 0.0f;                // 标签条与内容区之间
    InkingButtonData tab;            // 标签按钮的三态外观
    std::uint32_t selectedTextColor = theme::kAccentStrong;
    std::uint32_t textColor = theme::kTextPrimary;
    InkingButtonAppearance content, background;
    std::uint32_t indicatorColor = theme::kAccent;
    float indicatorHeight = 3.0f;
    static TabBarData MakeDefault();
};

class TabBar : public InkingStaticPanel {
public:
    InkingEvent<int, int> pageChanged;              // (现在, 之前)
    int AddPage(const std::string& title, std::unique_ptr<InkingWidget> page);
    bool Select(int index);                         // 换页才 true
    int GetPageCount() const noexcept;
    int GetVisiblePageIndex() const noexcept;       // 空时 −1
    InkingWidget* GetPage(int index) const noexcept;
    int GetContentWidth() const noexcept;
    int GetContentHeight() const noexcept;
};
```

```C++
inking_prefab::TabBar tabs(&panel);
tabs.AddPage("常规", std::make_unique<GeneralPage>(nullptr));
tabs.AddPage("音量", std::make_unique<AudioPage>(nullptr));
tabs.pageChanged.Subscribe([](int now, int before) { ... });
```

五条：

1. **切页 = 改可见性**，不是挪坐标、更不是调 alpha。可见性是骨架变化，命中表跟着重烘
   ——所以"切走的那页点不到"是白拿的；用 alpha 藏会出现"看不见但点得到"。
2. **顺序是先藏后显**：反过来的话中间那一帧两页同显。
3. **第一页自动成为当前页，但那次不发 `pageChanged`**——那是初始状态，不是"用户换了页"。
   否则订阅者每个回调都得先写 `if (before < 0) return;`。
4. **页内容的原点是内容区左上角**：页是自己的锚点 / 偏移说了算，TabBar 不重排
   （静态组件没有几何写入口）。要铺满就照 `GetContentWidth()/GetContentHeight()` 配。
5. **标签宽是固定值**（`tabLength`），不做均分、不做滚动——见下。

**为什么没复用 `OptionBar` 做标签条**（与任务书初稿不同）：它的项宽存在自己的 style 里、
`SetItems` 不回读；且 `itemLook.width <= 0` 时**每项撑成容器整宽**（竖向菜单语义）。
横向标签条要"每项 = 宽 / 项数"、项数又随 `AddPage` 变，两条路都比直接摆
`InkingStaticButton` 差。选中指示也不是它的词汇（它没有"选中态"这个第四态），
所以下划线由 TabBar 自己在**装饰遍**（`onRenderDecoration`）画——那一遍在子孙之上。

## 十四、PrefabTheme

命名空间 `inking_prefab::theme`，一组 `inline constexpr` 常量 + 一个函数：

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

1. **给菜单栏 / 列表的 `rowLook` 三态继承没关**：`InkingButtonData::Normalize()` 会拿
   `normal` 覆盖 `hover` / `onclicked`，你给的那两份被悄悄盖掉。想不同就必须显式把
   `hoverInheritsNormal` / `onclickedInheritsNormal` 置 false。
2. **`rowHeight <= 0`**：窗口化靠它算位置，直接坏掉。
3. **`ListView` 不 `SyncVisibleWindow()`**：窗口化之后画面停在旧区间。
4. **`Select` 自己开 `clipChildren`**：展开的那一列被自己剪掉。
5. **`FilePicker::SetDirectory` 传窄串**：中文目录抛异常。传 `std::filesystem::path`。
6. **菜单栏不重写 `HandlesSubtreeWheel`**：滚轮被指针下的按钮吃掉（本类已重写，别删）。
7. **`Slider` / `ProgressBar` 没有 `SetPainter`**：`InkingCanvasContext` 的构造是
   `private`（`friend` 只有 `InkingStaticCanvas`，必须带一个 `CanvasRecorder`），
   叶组件挂不了绘制器。要整块自绘请把 `InkingStaticCanvas` 叠在上面。
8. **部件外观写成 `type == Image`**：部件级贴图没接，三笔会按各自 `color` 画成纯色
   （构造时有一条 `WARN`，不静默）。要贴图就叠一个 `InkingStaticImage`。

---

> 本文档对应版本：**0.9.0（0.x 发布版）**。
