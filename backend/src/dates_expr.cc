#include "dates_expr.h"

#include <cctype>
#include <vector>

namespace rep {
namespace {

std::string trim(const std::string &s) {
    size_t b = 0;
    size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

const char *kBases[] = {"today(*)", "quarter(*)", "month(*)", "week(*)", "year(*)", "*"};

// Длина базового выражения в начале строки; 0 — не база.
size_t baseLength(const std::string &s) {
    for (const char *base : kBases) {
        const std::string b(base);
        if (s.compare(0, b.size(), b) == 0) return b.size();
    }
    return 0;
}

} // namespace

bool LooksLikeDateExpr(const std::string &value) {
    const std::string s = trim(value);
    if (s.empty()) return false;
    return s[0] == '*' || s.find('(') != std::string::npos;
}

bool CheckDateExprSyntax(const std::string &expr, std::string &error) {
    const std::string s = trim(expr);
    const size_t base = baseLength(s);
    if (base == 0) {
        error = "неизвестное начало выражения «" + s + "»";
        return false;
    }
    size_t i = base;
    while (i < s.size()) {
        const char sign = s[i];
        if (sign != '+' && sign != '-') {
            error = "не разобрано «" + s.substr(i) +
                    "»: ожидается смещение вида +1d, -5h, +2m, -1y";
            return false;
        }
        size_t j = i + 1;
        size_t digits = 0;
        while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) {
            ++j;
            ++digits;
        }
        if (digits == 0) {
            error = "не разобрано «" + s.substr(i) +
                    "»: ожидается смещение вида +1d, -5h, +2m, -1y";
            return false;
        }
        if (j >= s.size()) {
            error = "не разобрано «" + s.substr(i) +
                    "»: ожидается смещение вида +1d, -5h, +2m, -1y";
            return false;
        }
        const char unit = s[j];
        if (unit != 'h' && unit != 'd' && unit != 'm' && unit != 'y') {
            error = "не разобрано «" + s.substr(i) +
                    "»: ожидается смещение вида +1d, -5h, +2m, -1y";
            return false;
        }
        i = j + 1;
    }
    return true;
}

} // namespace rep
