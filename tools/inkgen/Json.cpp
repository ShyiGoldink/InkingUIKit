#include "Json.h"

#include "PathUtil.h"

#include <fstream>
#include <sstream>

namespace inkgen {

ParseResult LoadJsonFile(const std::string& path) {
    ParseResult result;

    // 走 `ToPath`（宽路径）：中文路径下窄的 `ifstream(串)` 打不开（T-09）。
    std::ifstream stream(ToPath(path), std::ios::binary);
    if (!stream) {
        result.ok = false;
        result.error = "打不开文件：" + path;
        return result;
    }

    std::ostringstream buffer;
    buffer << stream.rdbuf();

    // BOM 由 `Parse` 统一处理，这里不重复。
    result = inking::json::Parse(buffer.str());
    if (!result.ok) {
        // 错误已经带"第 N 行第 M 列"，这里只补文件名前缀——两边都补一次会得到
        // "路径：路径：第 N 行…"（踩过）。
        result.error = path + "：" + result.error;
    }
    return result;
}

}  // namespace inkgen
