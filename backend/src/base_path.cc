#include "base_path.h"

#include <cctype>

namespace rep {

namespace {

std::string trim(const std::string &value) {
    size_t begin = 0;
    size_t end = value.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    return value.substr(begin, end - begin);
}

}  // namespace

std::string normalizeBasePath(const std::string &raw) {
    std::string value = trim(raw);
    // пусто и "/" — работы без префикса: пути остаются как есть
    if (value.empty() || value == "/") return {};
    if (value.front() != '/') value.insert(value.begin(), '/');
    while (value.size() > 1 && value.back() == '/') value.pop_back();
    // двойные слэши внутри префикса — почти всегда опечатка в конфиге
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        if (ch == '/' && !out.empty() && out.back() == '/') continue;
        out += ch;
    }
    return out == "/" ? std::string() : out;
}

bool stripBasePath(const std::string &path, const std::string &base, std::string &rest) {
    rest.clear();
    if (base.empty()) return false;
    if (path == base) {
        rest = "/";
        return true;
    }
    if (path.size() > base.size() && path.compare(0, base.size(), base) == 0 &&
        path[base.size()] == '/') {
        rest = path.substr(base.size());
        return true;
    }
    return false;
}

}  // namespace rep
