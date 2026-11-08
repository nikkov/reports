// Расписание рассылки: разбор cron-выражения и расчёт следующего запуска (ADR-46).
// Пять полей: минута час день_месяца месяц день_недели. Поддержаны '*', числа,
// диапазоны 'a-b', шаги '*/k' и 'a-b/k', перечисления через запятую.
// Время — локальное время сервера (часовой пояс задаётся TZ окружения).
#pragma once

#include <ctime>
#include <string>
#include <vector>

namespace rep {

struct Cron {
    std::vector<bool> minutes; // 0..59
    std::vector<bool> hours;   // 0..23
    std::vector<bool> days;    // 1..31
    std::vector<bool> months;  // 1..12
    std::vector<bool> weekdays; // 0..6, 0 = воскресенье

    bool matches(const std::tm &t) const;
};

// Возвращает false и текст ошибки, если выражение не разобрано.
bool parseCron(const std::string &expr, Cron &cron, std::string &err);

// Ближайшее совпадение строго после `after`; 0, если в течение года его нет.
std::time_t nextRun(const Cron &cron, std::time_t after);

// То же, но во «наивном» локальном времени: `naiveAfter` считается как время без
// пояса (переходы на летнее время здесь не участвуют — их применяет база, ADR-60).
// Так расписание можно считать в часовом поясе подписки, не меняя TZ процесса.
std::time_t nextRunNaive(const Cron &cron, std::time_t naiveAfter);

} // namespace rep
