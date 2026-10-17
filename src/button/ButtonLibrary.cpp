#include <button/ButtonLibrary.h>

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace inking {

namespace {

/**
 * 登记表本体：函数内静态量。
 *
 * 用 map 不用 unordered_map：这里的项数是"界面里有几个按钮"，几十到几百，
 * 线性查找都够；map 的**迭代器与元素地址稳定**这一条才是关键——
 * `Find` 返回的指针会被调用方长期持有（生成器一次、组件一次），
 * 任何一次插入都不该让它失效。
 *
 * 名字有序也顺带让 GetAllNames 的输出稳定，自检里好比对。
 */
std::map<std::string, ButtonData>& table() {
    static std::map<std::string, ButtonData> entries;
    return entries;
}

/**
 * 上一批登记的名字。
 *
 * 只记名字不记数据：撤下时按名字从表里删，删不到就当它已经被别人处理过。
 * 用 vector 不用 set：一批通常只有几个到几十个，而且撤下是一次性的事。
 *
 * 同样用函数内静态量（见 table() 的理由），登记与撤下都只在启动期 / UI 线程。
 */
std::vector<std::string>& previousBatch() {
    static std::vector<std::string> names;
    return names;
}

/** 本批已经登记过的名字（EndRegistrationBatch 时变成"上一批"）。 */
std::vector<std::string>& currentBatch() {
    static std::vector<std::string> names;
    return names;
}

}  // namespace

ButtonLibrary::RegisterResult ButtonLibrary::Register(const ButtonData& data) {
    if (data.name.empty()) {
        // 空名字登记进去也查不到，只会在表里留一个空 key。
        return RegisterResult::Invalid;
    }

    const auto inserted = table().emplace(data.name, data);
    if (!inserted.second) {
        // 名字是查找键：让后来者顶掉先来者，等于"按钮长什么样取决于初始化顺序"。
        return RegisterResult::NameTaken;
    }

    // 收尾（省略 hover / onclicked 时继承 normal）在**入库前**做一次，
    // 之后所有读取方拿到的都是补齐过的数据，不用各自再判一遍。
    // 放这里而不是让调用方自己调：Find 拿到的数据必须已经是补齐过的，
    // 否则"省略即继承"就成了一条只在某些路径上生效的约定。
    inserted.first->second.Normalize();

    // 记进本批：EndRegistrationBatch 时会成为"上一批"，下次开始时被撤下。
    currentBatch().push_back(data.name);
    return RegisterResult::Ok;
}

void ButtonLibrary::BeginRegistrationBatch() {
    // 把上一批撤下：生成代码重新跑一遍时不撞 NameTaken。
    // 只删自己登记过的名字——别人（自检、业务代码）塞的条目不动。
    for (const std::string& name : previousBatch()) {
        table().erase(name);
    }
    previousBatch().clear();
    currentBatch().clear();
}

void ButtonLibrary::EndRegistrationBatch() {
    previousBatch() = currentBatch();
    currentBatch().clear();
}

const ButtonData* ButtonLibrary::Find(const std::string& name) {
    const auto it = table().find(name);
    return it == table().end() ? nullptr : &it->second;
}

bool ButtonLibrary::IsNameTaken(const std::string& name) {
    return table().find(name) != table().end();
}

std::vector<std::string> ButtonLibrary::GetAllNames() {
    std::vector<std::string> names;
    names.reserve(table().size());
    for (const auto& entry : table()) {
        names.push_back(entry.first);
    }
    return names;
}

std::size_t ButtonLibrary::Count() {
    return table().size();
}

void ButtonLibrary::ClearForTest() {
    table().clear();
}

}  // namespace inking
