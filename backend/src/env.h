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

}  // namespace rep
