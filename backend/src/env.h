#pragma once

#include <string>

namespace rep {

// Возвращает значение переменной окружения или defaultValue.
std::string envOr(const std::string &name, const std::string &defaultValue);

// Простой парсер KEY=VALUE файла (.env). Уже установленные переменные окружения не перетирает.
// Возвращает число загруженных переменных. Отсутствующий файл — не ошибка (0).
size_t loadDotEnv(const std::string &path);

// DSN для libpq из окружения: host/port/user/password/dbname + statement_timeout.
// Значения берутся из переменных PGHOST/PGPORT/... (см. ADR-15).
std::string connInfoFromEnv();

// Отдельное подключение «только чтение» (ADR-33): если заданы REP_RO_DB_USER и
// REP_RO_DB_PASSWORD, данные отчётов читаются под урезанной ролью, а запись шаблонов
// остаётся на основной учётной записи. Пустая строка — роль не настроена.
std::string readOnlyConnInfoFromEnv();

}  // namespace rep
