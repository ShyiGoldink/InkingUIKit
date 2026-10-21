#include <detail/InkLog.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#if defined(INK_ISLOG)

// 报告是**派生产物**：运行期只写纯文本 .log，需要浏览器视图时调用一次
// InkLog::report()，由这里把 .log 渲染成一份自包含的 HTML。
// 这样日志热路径上不再有 CSS/JS 模板，报告随时可以从事实源重新生成。

namespace inking {
namespace {

/** 取第 n 个 " | " 分隔字段（0 基），越界返回空串。 */
std::string fieldOf(const std::string& line, int index) {
    std::size_t cursor = 0;
    for (int current = 0; current < index; ++current) {
        const std::size_t separator = line.find(" | ", cursor);
        if (separator == std::string::npos) {
            return {};
        }
        cursor = separator + 3;
    }
    const std::size_t end = line.find(" | ", cursor);
    std::string value = line.substr(cursor, end == std::string::npos ? std::string::npos : end - cursor);
    while (!value.empty() && value.back() == ' ') {
        value.pop_back();
    }
    return value;
}

/** 内联进 <script type="text/plain"> 时只需要防住结束标签。 */
std::string escapeForScript(const std::string& text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text.compare(index, 8, "</script") == 0) {
            escaped += "<\\/script";
            index += 7;
            continue;
        }
        escaped += text[index];
    }
    return escaped;
}

std::string htmlEscape(const std::string& text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (const char item : text) {
        switch (item) {
            case '&': escaped += "&amp;"; break;
            case '<': escaped += "&lt;"; break;
            case '>': escaped += "&gt;"; break;
            default: escaped += item; break;
        }
    }
    return escaped;
}

}  // namespace

std::filesystem::path InkLog::report() {
    const std::filesystem::path logPath = logFilePath();
    if (logPath.empty()) {
        return {};
    }

    std::ifstream input(logPath, std::ios::binary);
    if (!input) {
        return {};
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    input.close();

    std::size_t records = 0;
    std::size_t folds = 0;
    std::size_t errors = 0;
    std::map<std::string, std::size_t> levelCounts;
    for (const std::string& item : lines) {
        if (item.empty() || item.front() == '#') {
            continue;
        }
        ++records;
        const std::string tag = fieldOf(item, 1);
        if (tag == "FOLD") {
            ++folds;
        }
        if (tag == "ERROR") {
            ++errors;
        }
        ++levelCounts[tag];
    }

    std::ostringstream chips;
    for (const auto& entry : levelCounts) {
        const char* css = entry.first == "ERROR" ? "error"
                          : entry.first == "WARN " ? "warn"
                          : entry.first == "PASS " ? "pass"
                          : entry.first == "DEBUG" ? "debug"
                          : entry.first == "FOLD"  ? "fold"
                                                   : "info";
        chips << "<button class=\"chip " << css << "\" data-level=\"" << htmlEscape(entry.first)
              << "\">" << htmlEscape(entry.first) << " <b>" << entry.second << "</b></button>";
    }

    std::ostringstream raw;
    for (const std::string& item : lines) {
        raw << item << '\n';
    }

    const std::string stem = logPath.stem().string();  // run-时-分-秒-pid，纯 ASCII
    const std::filesystem::path outputPath =
        logPath.parent_path() / (std::filesystem::path("report-") += stem + ".html");

    std::ofstream output(outputPath, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!output) {
        return {};
    }

    output << R"HTML(<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="color-scheme" content="dark">
<title>日志报告</title>
<style>
:root{
  --bg:#070b14; --line:#1b2a44; --cyan:#00e5ff; --green:#35f0a6; --red:#ff5c7a;
  --amber:#ffc857; --violet:#b58cff; --dim:#68799b; --text:#c9d5ec; --panel:#0e1729;
}
*{box-sizing:border-box}
body{
  margin:0; padding:26px 22px 60px; color:var(--text); font-size:13px; line-height:1.55;
  font-family:"Cascadia Code","JetBrains Mono",Consolas,"Courier New",monospace;
  background:
    radial-gradient(900px 480px at 88% -12%, rgba(0,229,255,.08), transparent 60%),
    linear-gradient(rgba(27,42,68,.14) 1px, transparent 1px),
    linear-gradient(90deg, rgba(27,42,68,.14) 1px, transparent 1px),
    var(--bg);
  background-size:auto,38px 38px,38px 38px,auto;
}
header{position:sticky; top:0; z-index:5; padding:14px 16px; margin-bottom:14px;
  border:1px solid rgba(0,229,255,.22); border-radius:12px;
  background:rgba(8,13,23,.94); backdrop-filter:blur(8px);}
h1{margin:0 0 6px; font-size:17px; letter-spacing:.28em; color:var(--cyan); font-weight:600}
.meta{color:var(--dim); font-size:11px; letter-spacing:.06em}
.controls{display:flex; flex-wrap:wrap; gap:8px; align-items:center; margin-top:10px}
.chip{border:1px solid rgba(0,229,255,.28); background:rgba(0,229,255,.05); color:var(--text);
  border-radius:999px; padding:3px 10px; font:inherit; font-size:11px; cursor:pointer}
.chip b{color:var(--cyan); font-weight:600}
.chip.info{color:var(--dim)} .chip.info b{color:var(--text)}
.chip.pass{border-color:rgba(53,240,166,.4)} .chip.pass b{color:var(--green)}
.chip.warn{border-color:rgba(255,200,87,.35)} .chip.warn b{color:var(--amber)}
.chip.error{border-color:rgba(255,92,122,.4)} .chip.error b{color:var(--red)}
.chip.debug{border-color:rgba(181,140,255,.4)} .chip.debug b{color:var(--violet)}
.chip.fold{border-color:rgba(104,121,155,.5)} .chip.fold b{color:var(--dim)}
.chip.off{opacity:.35}
select,input{background:var(--panel); color:var(--text); border:1px solid var(--line);
  border-radius:8px; padding:4px 8px; font:inherit; font-size:12px}
input{min-width:200px}
#jump{border:1px solid rgba(255,92,122,.45); background:rgba(255,92,122,.08); color:var(--red);
  border-radius:8px; padding:4px 10px; font:inherit; font-size:11px; cursor:pointer}
main{display:flex; flex-direction:column; gap:1px}
.row{display:grid; grid-template-columns:96px 52px 150px 1fr; gap:10px; padding:2px 10px;
  border-left:2px solid transparent; border-radius:4px; white-space:pre-wrap; word-break:break-word}
.row:hover{background:rgba(0,229,255,.05); border-left-color:var(--cyan)}
.row .t{color:var(--dim)} .row .lv{font-weight:600}
.row .where{color:var(--cyan)} .row .site{color:var(--dim)}
.row.lv-ERROR .lv,.row.lv-ERROR .where{color:var(--red)}
.row.lv-WARN .lv{color:var(--amber)} .row.lv-PASS .lv{color:var(--green)}
.row.lv-DEBUG .lv{color:var(--violet)} .row.lv-FOLD .lv,.row.lv-FOLD .where{color:var(--dim)}
.row.lv-MARK .lv{color:var(--amber)}
.row.flash{background:rgba(255,92,122,.18)}
.hidden{display:none}
#empty{color:var(--dim); padding:20px 10px}
::selection{background:rgba(0,229,255,.25)}
</style>
</head>
<body>
<header>
  <h1>日志报告</h1>
  <div class="meta">)HTML"
           << htmlEscape(logPath.filename().string()) << " · 共 " << records << " 条 · 折叠 " << folds
           << " 条 · 错误 " << errors << " 条</div>\n  <div class=\"controls\">" << chips.str() << R"HTML(
    <select id="module"><option value="">全部模块</option></select>
    <input id="search" placeholder="搜索关键字（消息/模块/文件行）">
    <button id="jump">跳到下一个错误</button>
    <span class="meta" id="count"></span>
  </div>
</header>
<main id="list"></main>
<div id="empty" class="hidden">没有匹配的记录</div>
<script type="text/plain" id="raw">)HTML"
           << escapeForScript(raw.str()) << R"HTML(</script>
<script>
(function () {
  function splitFields(line) {
    var parts = [], rest = line;
    for (var i = 0; i < 6; i++) {
      var idx = rest.indexOf(' | ');
      if (idx < 0) { break; }
      parts.push(rest.slice(0, idx));
      rest = rest.slice(idx + 3);
    }
    parts.push(rest);
    return parts;
  }

  var raw = document.getElementById('raw').textContent || '';
  var records = [];
  var modules = Object.create(null);

  raw.split('\n').forEach(function (line) {
    if (!line || line.charAt(0) === '#') { return; }
    var f = splitFields(line);
    if (f.length < 7) { return; }
    var moduleName = f[2].split(':')[0];
    modules[moduleName] = true;
    records.push({ time: f[0], level: f[1].replace(/\s+$/, ''), where: f[2],
                   seq: f[3], thread: f[4], site: f[5], message: f[6] });
  });

  var list = document.getElementById('list');
  var empty = document.getElementById('empty');
  var countLabel = document.getElementById('count');
  var moduleSelect = document.getElementById('module');
  var searchInput = document.getElementById('search');
  var state = { levels: null, module: '', search: '' };

  Object.keys(modules).sort().forEach(function (name) {
    var option = document.createElement('option');
    option.value = name; option.textContent = name;
    moduleSelect.appendChild(option);
  });

  var cells = records.map(function (record) {
    var row = document.createElement('div');
    row.className = 'row lv-' + record.level;
    [['t', record.time + ' ' + record.seq], ['lv', record.level], ['where', record.where]].forEach(function (pair) {
      var span = document.createElement('span');
      span.className = pair[0];
      span.textContent = pair[1];
      row.appendChild(span);
    });
    var message = document.createElement('span');
    message.className = 'msg';
    message.textContent = record.message;
    if (record.site && record.site !== 'InkLog.cpp:0') {
      var site = document.createElement('span');
      site.className = 'site';
      site.textContent = '  ' + record.site;
      message.appendChild(site);
    }
    row.appendChild(message);
    list.appendChild(row);
    return { element: row, data: record };
  });

  function matches(record) {
    if (state.levels && !state.levels[record.level]) { return false; }
    if (state.module && record.where.split(':')[0] !== state.module) { return false; }
    if (state.search) {
      var haystack = (record.message + ' ' + record.where + ' ' + record.site).toLowerCase();
      if (haystack.indexOf(state.search) < 0) { return false; }
    }
    return true;
  }

  function apply() {
    var visible = 0;
    cells.forEach(function (item) {
      var ok = matches(item.data);
      item.element.classList.toggle('hidden', !ok);
      if (ok) { visible++; }
    });
    empty.classList.toggle('hidden', visible !== 0);
    countLabel.textContent = '显示 ' + visible + ' / ' + cells.length;
  }

  document.querySelectorAll('.chip').forEach(function (chip) {
    chip.addEventListener('click', function () {
      var level = chip.getAttribute('data-level');
      if (!state.levels) { state.levels = Object.create(null); }
      if (state.levels[level]) { delete state.levels[level]; chip.classList.add('off'); }
      else { state.levels[level] = true; chip.classList.remove('off'); }
      if (Object.keys(state.levels).length === 0) { state.levels = null; }
      apply();
    });
  });

  moduleSelect.addEventListener('change', function () { state.module = moduleSelect.value; apply(); });
  searchInput.addEventListener('input', function () { state.search = searchInput.value.toLowerCase().trim(); apply(); });

  document.getElementById('jump').addEventListener('click', function () {
    var errors = cells.filter(function (item) {
      return !item.element.classList.contains('hidden') && item.data.level === 'ERROR';
    });
    if (!errors.length) { return; }
    var top = 0;
    for (var i = 0; i < errors.length; i++) {
      if (errors[i].element.getBoundingClientRect().top > 120) { top = i; break; }
    }
    var target = errors[top].element;
    target.scrollIntoView({ behavior: 'smooth', block: 'center' });
    target.classList.add('flash');
    setTimeout(function () { target.classList.remove('flash'); }, 1200);
  });

  apply();
})();
</script>
</body>
</html>
)HTML";

    output.flush();
    return outputPath;
}

}  // namespace inking

#endif  // INK_ISLOG
