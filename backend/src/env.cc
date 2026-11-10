#include "env.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace rep {

std::string envOr(const std::string &name, const std::string &defaultValue) {
    const char *v = std::getenv(name.c_str());
    if (v == nullptr || *v == '\0') return defaultValue;
    return v;
}

static std::string trim(const std::string &s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

size_t loadDotEnv(const std::string &path) {
    std::ifstream in(path);
    if (!in) return 0;

    size_t loaded = 0;
    std::string line;
    while (std::getline(in, line)) {
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;

        const auto eq = t.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trim(t.substr(0, eq));
        std::string value = trim(t.substr(eq + 1));
        if (key.empty()) continue;

        // Кавычки вокруг значения снимаем, но не перетираем уже заданное окружение.
        if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                                  (value.front() == '\'' && value.back() == '\''))) {
            value = value.substr(1, value.size() - 2);
        }
        if (std::getenv(key.c_str()) == nullptr) {
            setenv(key.c_str(), value.c_str(), 0);
            ++loaded;
        }
    }
    return loaded;
}

std::string connInfoFromEnv() {
    const std::string host = envOr("PGHOST", "127.0.0.1");
    const std::string port = envOr("PGPORT", "5432");
    const std::string user = envOr("PGUSER", "postgres");
    const std::string password = envOr("PGPASSWORD", "");
    const std::string dbname = envOr("PGDATABASE", "reporting");
    const std::string timeoutMs = envOr("REP_STATEMENT_TIMEOUT_MS", "30000");
    // Ядро живёт в отдельной схеме (ADR-74): без search_path приложение искало бы
    // таблицы в public. Примеры (reports_examples) подключаются только из SQL шаблонов
    // и поэтому указываются там явно.
    const std::string schema = envOr("REP_DB_SCHEMA", "reports");

    // statement_timeout задаётся на уровне соединения через libpq option (ADR-7).
    std::ostringstream dsn;
    dsn << "host=" << host << " port=" << port << " user=" << user
        << " password=" << password << " dbname=" << dbname
        << " application_name=rep-backend"
        << " options='-c statement_timeout=" << timeoutMs << " -c search_path=" << schema << "'";
    return dsn.str();
}

std::string readOnlyConnInfoFromEnv() {
    const std::string user = envOr("REP_RO_DB_USER", "");
    const std::string password = envOr("REP_RO_DB_PASSWORD", "");
    if (user.empty() || password.empty()) return {};

    const std::string host = envOr("REP_RO_DB_HOST", envOr("PGHOST", "127.0.0.1"));
    const std::string port = envOr("REP_RO_DB_PORT", envOr("PGPORT", "5432"));
    const std::string dbname = envOr("REP_RO_DB_NAME", envOr("PGDATABASE", "reporting"));
    const std::string timeoutMs = envOr("REP_STATEMENT_TIMEOUT_MS", "30000");
    // Роль только-чтения работает с данными отчётов, то есть с примерами (ADR-33, ADR-74)
    const std::string schema = envOr("REP_RO_DB_SCHEMA", "reports_examples");

    std::ostringstream dsn;
    dsn << "host=" << host << " port=" << port << " user=" << user
        << " password=" << password << " dbname=" << dbname
        << " application_name=rep-backend-ro"
        << " options='-c statement_timeout=" << timeoutMs << " -c search_path=" << schema << "'";
    return dsn.str();
}

}  // namespace rep
