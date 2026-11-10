#pragma once

// Префикс приложения в URL (ADR-81): сервер отдаёт страницы и API под /reports,
// чтобы адрес был http(s)://host:port/reports/…, как за реверс-прокси.
// Префикс снимается до маршрутизации, поэтому все обработчики остаются на своих путях.

#include <string>

namespace rep {

// "/reports/" → "/reports"; "reports" → "/reports"; "", "/" → "" (без префикса).
std::string normalizeBasePath(const std::string &raw);

// "/reports/api/health" при base="/reports" → rest="/api/health", true.
// Сам префикс без хвостового слэша ("/reports") → rest="/", true (нужен редирект на слэш).
bool stripBasePath(const std::string &path, const std::string &base, std::string &rest);

}  // namespace rep
