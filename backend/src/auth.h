// Аутентификация и роли (ADR-34).
// Чистые функции вынесены сюда, чтобы проверять их Catch2 без базы и HTTP:
// хэширование пароля, генерация токена и матрица «метод + путь → минимальная роль».
#pragma once

#include <string>
#include <vector>

namespace rep {

struct Session {
    long userId = 0;
    std::string login;
    std::string role; // admin | author | viewer
};

enum class Role { Public = 0, Viewer = 1, Author = 2, Admin = 3 };

// Режим аутентификации (ADR-54): выключена, сессии в cookie (ADR-34) или
// личность приходит от реверс-прокси в HTTP-заголовке.
enum class AuthMode { Off = 0, Session = 1, Proxy = 2 };

// REP_AUTH: "0"/"" → Off, "1"/"session" → Session, "proxy" → Proxy.
AuthMode parseAuthMode(const std::string &value);

// Логин из заголовка прокси: печатные ASCII без пробелов и управляющих символов,
// не длиннее 128 символов. Заголовок идёт от прокси, но проверять всё равно надо:
// доверенный прокси может пробросить что угодно, а логин попадает в SQL и журнал.
bool isValidProxyLogin(const std::string &login);

// "127.0.0.1, ::1" → список адресов; пробелы и пустые элементы отбрасываются.
std::vector<std::string> parseTrustedProxies(const std::string &value);

// Адрес запроса входит в список доверенных прокси. Loopback сравнивается
// в обеих формах (127.0.0.1 и ::ffff:127.0.0.1), иначе IPv4-запрос с
// dual-stack сокета не прошёл бы проверку.
bool isTrustedPeer(const std::string &peer, const std::vector<std::string> &trusted);

bool roleAtLeast(const std::string &role, Role needed);

// PBKDF2-HMAC-SHA256, 120 000 итераций; соль и результат — hex.
std::string hashPassword(const std::string &password, const std::string &saltHex, int iterations = 120000);
bool verifyPassword(const std::string &password, const std::string &saltHex, const std::string &expectedHex,
                    int iterations = 120000);

// Криптостойкий токен сессии (hex).
std::string randomHex(std::size_t bytes);

// Минимальная роль для запроса. Неизвестные пути требуют admin: безопасное умолчание.
Role requiredRole(const std::string &method, const std::string &path);

// Проверка данных пользователя из админки: логин, роль, пароль (при создании обязателен).
// Пустой пароль при обновлении означает «не менять».
bool validateUserInput(const std::string &login, const std::string &role, const std::string &password,
                       bool creating, std::string &err);

} // namespace rep
