#pragma once

// 预制件 C5「路径 / 文件选择」的**纯数据**面。
//
// **路线已由用户裁定：组装自绘**（不是系统原生对话框）。所以这里的东西全是
// 现有配件的配置：C3 列表 + B7 单行文本框 + C1 选项栏（快捷位置 / 盘符栏）+
// 一条**普通横排容器**放底部按钮。一条新的底层词汇都没有。
//
// 底部按钮那一栏**不用 `MenuBar`**（0.5.3 改）：`MenuBar` 是"贴着窗口的横条"，
// 数据面里捆着 `hideInFullscreen` / `zoomWheel` / `draggable` / `captionButtons`
// 四件窗口语义（前两个默认 **true**），实现里还订阅窗口的全屏事件、绑快捷键表。
// 对话框的一行按钮只是"横向排两个按钮"，这些策略一条都不该继承——早先为了
// "不新增底层词汇"复用它，结果是全屏下整行消失（`hideInFullscreen`）、滚轮落在
// 按钮上变成窗口缩放（`zoomWheel`）。布局该复用，策略不该复用：现在只用
// `PanelData`（Stack / Horizontal / End）排，窗口语义按构造不存在。
//
// 为什么不走 `IFileOpenDialog`（写在注释里，免得下次有人重开这个决定）：
// 它要 COM 初始化 + **全程宽字符**（`AGENTS.md` §6 第 53 条），而且
// **自检拿不到确定性判据**（弹窗要人点）——本项目的验收纪律是"能变成数值判据的
// 自己验"，原生路线那一条直接出局。它真正多给的只有"最近位置"这类便利。

#include <button/ButtonData.h>
#include <dataStruct/InkingLayout.h>
#include <panel/PanelData.h>
#include <prefab/ListViewData.h>
#include <prefab/OptionBarData.h>
#include <text/InkingTextInput.h>

#include <cstdint>
#include <string>
#include <vector>

namespace inking::prefab {

/// 模块名常量：日志与自检要用。
inline constexpr const char* kFilePickerModuleName = "FilePicker";

/// 选什么。
enum class FilePickMode : std::uint8_t {
    OpenFile = 0,    ///< 开文件（默认）
    SaveFile,        ///< 存文件
    PickDirectory,   ///< 选目录
};

/**
 * @brief 文件选择器的外观与行为。
 *
 * 四块内容从上到下：路径框（B7）→ 快捷位置（C1）→ 目录内容（C3）→ 按钮栏。
 * 每块的位置与尺寸都在这里配（设计坐标，相对选择器左上角）。
 */
struct FilePickerStyle {
    /// 选择器自己那一层：尺寸 / 背景 / 形状。
    PanelData panel{};

    /// 路径文本框（显示当前目录；回车进入那个目录）。
    TextInputData pathInput{};

    /// 快捷位置那一栏（上级目录 / 根目录 / 主目录）。
    OptionBarStyle shortcuts{};

    /**
     * 盘符那一栏（换盘符）：横向一排窄按钮（"C:" "D:" …）。
     *
     * 为什么单独占一块、而不是塞进 `shortcuts`：选项栏**每项同宽**（宽度来自
     * `itemLook.width`），快捷位置那三项已经是 96 宽，再塞几个盘符就会顶出 640。
     * 盘符很短（"C:"），自己一份 `itemLook` 给 52 宽，一行放得下 11 个盘符。
     */
    OptionBarStyle drives{};

    /// 目录内容列表。
    ListViewStyle list{};

    /**
     * 底部按钮那一栏（确定 / 取消）：**一条普通横排容器**，不是 `MenuBar`。
     *
     * 只用 `PanelData` 的 `layout`（`Stack / Horizontal / justify=End / align=Center`）
     * 排两个按钮。为什么不用 `MenuBar`（0.5.3 的改动，理由也在文件抬头）：
     * 它自带窗口语义——`hideInFullscreen` / `zoomWheel` 默认都是 true，还会订阅
     * 窗口的全屏事件、绑快捷键表。对话框的按钮行只要"横向排好"，不要那些。
     */
    PanelData buttonBar{};

    /// 底部那两个按钮的外观模板（"确定" / "取消"）。宽度就是按钮宽度（Stack 不拉伸主轴）。
    ButtonData buttonLook{};

    /**
     * 扩展名过滤（小写比较）：`{".png", ".jpg"}`。**空 = 全要**。
     *
     * 目录永远显示（不然没法往下走）。
     */
    std::vector<std::string> extensions;

    FilePickMode mode = FilePickMode::OpenFile;

    /// 能不能多选（`PickDirectory` 模式下没有意义）。
    bool multiSelect = false;

    /// 显示以 "." 开头的隐藏项吗。
    bool showHidden = false;

    /**
     * 显示盘符栏吗（默认 true）。
     *
     * 注意它只是"想不想"：**平台上枚举不到盘符时这一栏整条不建**（不是画一条空栏）。
     * 非 Windows 没有盘符这个词汇（根目录只有一个 `/`，快捷位置的"根目录"已经覆盖），
     * 所以在那些平台上这一行永远不会占高度。
     */
    bool showDrives = true;

    // ---- 各块的位置与尺寸（相对选择器左上角） ----

    float contentGap = 4.0f;        ///< 块与块之间的缝
    float pathInputHeight = 32.0f;
    float shortcutHeight = 30.0f;
    float driveBarHeight = 30.0f;   ///< 盘符栏；枚举不到盘符时这一块不占高度
    float listHeight = 300.0f;
    float buttonBarHeight = 40.0f;

    /// 路径框里没有任何目录时显示的占位文字。
    std::string pathPlaceholder = "输入目录路径后回车";

    /// 底部两个按钮的文字。
    std::string acceptText = "确定";
    std::string cancelText = "取消";

    /// 一份能直接用的默认样式（640×480、竖向四块、无过滤、单文件）。
    static FilePickerStyle MakeDefault();
};

/// 一份底部按钮的默认外观（深色圆角 + 浅色字，够看清）。
ButtonData MakeDefaultPickerButtonLook();

}  // namespace inking::prefab
