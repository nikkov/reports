#include "pg_probe.h"

#include <libpq-fe.h>

namespace rep {

namespace {

std::string tidy(const char *raw) {
    std::string text = raw ? raw : "";
    // PostgreSQL завершает сообщение переводом строки и иногда ставит двойные пробелы
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
        text.pop_back();
    }
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        if (c == ' ' && !out.empty() && out.back() == ' ') continue;
        out.push_back(c);
    }
    return out;
}

} // namespace

ProbeResult probeConnection(const std::string &dsn) {
    ProbeResult result;
    PGconn *conn = PQconnectdb(dsn.c_str());
    if (conn == nullptr) {
        result.error = "не удалось создать соединение с источником";
        return result;
    }
    result.ok = PQstatus(conn) == CONNECTION_OK;
    if (!result.ok) {
        result.error = tidy(PQerrorMessage(conn));
        if (result.error.empty()) result.error = "источник недоступен";
    }
    PQfinish(conn);
    return result;
}

} // namespace rep
