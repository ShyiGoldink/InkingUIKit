#pragma once

// CXXCSS → C++ 的发射器：把加载好的配置写成两个文件。
//
// 产物固定两个文件，名字由头文件名推出来：
//   <headerName>              常量与各类声明（`inline constexpr` 常量放这里，
//                             头文件可以直接被业务代码 include）
//   <header 主体>_register.cpp   实现 + 一个启动期就位的静态对象
//
// 为什么每个组件类型一个类、而不是"名字 → 运行期查表"：见 `Cxxcss.h` 与
// `CXXCSS.md` §6——拼错名字应该在编译期红，而不是等运行期 Find 返回 nullptr。
//
// **加组件类型要改哪几处**（A2 起 Panel、A7 起 Label / Text）：
//   1. `Cxxcss.h` 的 `ComponentKind` + 目录名映射 + 一个 Loader；
//   2. 这里：头文件里多一段类声明、源文件里多一段 `Data()` 定义。
// 其余（校验、重名、跳过 `.example.json`、产物规则）都是共用的。
//
// A7 的两项各有各的形态，不是"再抄一遍类声明"：
//   - `Label` 与 Button / Panel 同形（一个名字一个类，数据烘在类里）；
//   - `Text` 是**语言表**（不是组件）：产物里是静态数据 + 一个安装函数，
//     启动期自动装进 `inking::InstallTextTable`；它只允许一份。

#include "Cxxcss.h"

#include <string>
#include <vector>

namespace inkgen {

struct EmitOptions {
    /// 产物目录（不存在会创建）。约定只指向 build 目录，不写源码树。
    std::string outDir;
    /// 生成代码的命名空间，默认 `inking::cxxcss`。
    std::string nameSpace = "inking::cxxcss";
    /// 生成的头文件名，默认 `button_service.h`。
    /// 用 `#include <inkgen/button_service.h>` 这种带前缀的写法，避免撞名。
    std::string headerName = "button_service.h";
};

struct EmitResult {
    bool ok = false;
    std::string error;
    std::string headerPath;
    std::string sourcePath;
};

/**
 * @brief 把一次加载的结果写成头文件 + 源文件。
 *
 * 收 `LoadResult` 而不是四个 `vector`：组件类型只会越加越多，参数表不该跟着长；
 * 而且"一次加载 → 一次生成"本来就是这条链路的形状。
 *
 * 调用方保证名字已经唯一且校验过（`LoadConfigs` 负责）——包括**跨组件类型**的
 * 唯一性：一个按钮和一个面板同名同样会被拦下。有 `errors` 的结果不该传进来。
 */
EmitResult Emit(const LoadResult& loaded, const EmitOptions& options);

}  // namespace inkgen
