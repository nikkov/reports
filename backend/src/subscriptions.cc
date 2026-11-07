#include "subscriptions.h"

#include <cctype>
#include <sstream>

namespace rep {
namespace {

std::vector<std::string> split(const std::string &s, char sep) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : s) {
        if (c == sep) {
            parts.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    parts.push_back(current);
    return parts;
}

std::string trim(const std::string &s) {
    std::size_t begin = 0;
    std::size_t end = s.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(s[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(begin, end - begin);
}

bool toInt(const std::string &s, int &value) {
    if (s.empty()) return false;
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    value = std::stoi(s);
    return true;
}

// Одно поле: '*' | N | a-b | */k | a-b/k, перечисление через запятую.
bool parseField(const std::string &field, int min, int max, std::vector<bool> &out, std::string &err) {
    out.assign(static_cast<std::size_t>(max + 1), false);
    const std::string text = trim(field);
    if (text.empty()) {
        err = "пустое поле";
        return false;
    }

    for (const std::string &rawPart : split(text, ',')) {
        std::string part = trim(rawPart);
        if (part.empty()) {
            err = "пустой элемент в перечислении";
            return false;
        }

        int step = 1;
        const std::size_t slash = part.find('/');
        if (slash != std::string::npos) {
            const std::string stepText = part.substr(slash + 1);
            if (!toInt(stepText, step) || step <= 0) {
                err = "шаг должен быть положительным числом: «" + part + "»";
                return false;
            }
            part = part.substr(0, slash);
        }

        int low = min;
        int high = max;
        if (part != "*") {
            const std::size_t dash = part.find('-');
            if (dash == std::string::npos) {
                if (!toInt(part, low)) {
                    err = "ожидалось число: «" + part + "»";
                    return false;
                }
                high = low;
                if (step > 1) {
                    err = "шаг допустим только с диапазоном или *: «" + rawPart + "»";
                    return false;
                }
            } else {
                if (!toInt(part.substr(0, dash), low) || !toInt(part.substr(dash + 1), high)) {
                    err = "ожидался диапазон: «" + part + "»";
                    return false;
                }
            }
        }
        if (low < min || high > max || low > high) {
            err = "значение вне диапазона " + std::to_string(min) + "…" + std::to_string(max) + ": «" + part + "»";
            return false;
        }
        for (int value = low; value <= high; value += step) out[static_cast<std::size_t>(value)] = true;
    }
    return true;
}

} // namespace

bool Cron::matches(const std::tm &t) const {
    return minutes[static_cast<std::size_t>(t.tm_min)] && hours[static_cast<std::size_t>(t.tm_hour)] &&
           days[static_cast<std::size_t>(t.tm_mday)] && months[static_cast<std::size_t>(t.tm_mon + 1)] &&
           weekdays[static_cast<std::size_t>(t.tm_wday)];
}

bool parseCron(const std::string &expr, Cron &cron, std::string &err) {
    std::istringstream in(expr);
    std::vector<std::string> fields;
    std::string token;
    while (in >> token) fields.push_back(token);
    if (fields.size() != 5) {
        err = "ожидалось 5 полей (минута час день месяц день_недели), получено " +
              std::to_string(fields.size());
        return false;
    }
    return parseField(fields[0], 0, 59, cron.minutes, err) &&
           parseField(fields[1], 0, 23, cron.hours, err) &&
           parseField(fields[2], 1, 31, cron.days, err) &&
           parseField(fields[3], 1, 12, cron.months, err) &&
           parseField(fields[4], 0, 6, cron.weekdays, err);
}

std::time_t nextRun(const Cron &cron, std::time_t after) {
    std::tm t = *std::localtime(&after);
    t.tm_sec = 0;
    t.tm_min += 1; // строго после указанной минуты
    std::time_t candidate = std::mktime(&t);

    // до года вперёд по минутам: 366*24*60 = 527 040 шагов максимум
    for (int step = 0; step < 366 * 24 * 60; ++step) {
        std::tm local = *std::localtime(&candidate);
        if (cron.matches(local)) return candidate;
        local.tm_min += 1;
        candidate = std::mktime(&local);
        if (candidate == static_cast<std::time_t>(-1)) return 0;
    }
    return 0;
}

} // namespace rep
