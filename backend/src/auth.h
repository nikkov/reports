// Аутентификация и роли (ADR-34).
// Чистые функции вынесены сюда, чтобы проверять их Catch2 без базы и HTTP:
// хэширование пароля, генерация токена и матрица «метод + путь → минимальная роль».
#pragma once

#include <string>

namespace rep {

struct Session {
    long userId = 0;
    std::string login;
    std::string role; // admin | author | viewer
};

enum class Role { Public = 0, Viewer = 1, Author = 2, Admin = 3 };

bool roleAtLeast(const std::string &role, Role needed);

// PBKDF2-HMAC-SHA256, 120 000 итераций; соль и результат — hex.
std::string hashPassword(const std::string &password, const std::string &saltHex, int iterations = 120000);
bool verifyPassword(const std::string &password, const std::string &saltHex, const std::string &expectedHex,
                    int iterations = 120000);

// Криптостойкий токен сессии (hex).
std::string randomHex(std::size_t bytes);

// Минимальная роль для запроса. Неизвестные пути требуют admin: безопасное умолчание.
Role requiredRole(const std::string &method, const std::string &path);

} // namespace rep
