// Проверка соединения с источником до запроса (ADR-126).
//
// Зачем: drogon при недоступной базе переподключается в своём цикле, и колбэки запроса
// не вызываются. Единственный способ понять причину сразу — один прямой PQconnectdb:
// он отдаёт сообщение PostgreSQL («база данных ... не существует») за миллисекунды
// вместо ожидания таймаута запроса.
#pragma once

#include <string>

namespace rep {

struct ProbeResult {
    bool ok = false;
    std::string error; // текст PostgreSQL без завершающих переводов строки
};

// Синхронная проверка: вызывать только из отдельного потока, не из event loop drogon.
ProbeResult probeConnection(const std::string &dsn);

} // namespace rep
