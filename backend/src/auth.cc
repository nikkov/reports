#include "auth.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <cctype>
#include <stdexcept>
#include <vector>

namespace rep {
namespace {

std::string toHex(const unsigned char *data, std::size_t len) {
    static const char *digits = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (std::size_t i = 0; i < len; ++i) {
        out.push_back(digits[(data[i] >> 4) & 0xF]);
        out.push_back(digits[data[i] & 0xF]);
    }
    return out;
}

std::vector<unsigned char> fromHex(const std::string &hex) {
    if (hex.size() % 2 != 0) throw std::runtime_error("нечётная длина hex");
    std::vector<unsigned char> out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        const auto byte = static_cast<unsigned char>(std::stoi(hex.substr(i, 2), nullptr, 16));
        out.push_back(byte);
    }
    return out;
}

std::string pbkdf2(const std::string &password, const std::string &saltHex, int iterations) {
    const auto salt = fromHex(saltHex);
    unsigned char out[32];
    if (PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()), salt.data(),
                          static_cast<int>(salt.size()), iterations, EVP_sha256(),
                          static_cast<int>(sizeof(out)), out) != 1) {
        throw std::runtime_error("pbkdf2 failed");
    }
    return toHex(out, sizeof(out));
}

} // namespace

bool roleAtLeast(const std::string &role, Role needed) {
    Role have = Role::Viewer;
    if (role == "admin") have = Role::Admin;
    else if (role == "author") have = Role::Author;
    else if (role == "viewer") have = Role::Viewer;
    else return needed == Role::Public;
    return static_cast<int>(have) >= static_cast<int>(needed);
}

std::string hashPassword(const std::string &password, const std::string &saltHex, int iterations) {
    return pbkdf2(password, saltHex, iterations);
}

bool verifyPassword(const std::string &password, const std::string &saltHex, const std::string &expectedHex,
                    int iterations) {
    const std::string actual = pbkdf2(password, saltHex, iterations);
    if (actual.size() != expectedHex.size()) return false;
    return CRYPTO_memcmp(actual.data(), expectedHex.data(), actual.size()) == 0;
}

std::string randomHex(std::size_t bytes) {
    std::vector<unsigned char> buf(bytes);
    if (RAND_bytes(buf.data(), static_cast<int>(buf.size())) != 1) {
        throw std::runtime_error("нет источника случайности");
    }
    return toHex(buf.data(), buf.size());
}

Role requiredRole(const std::string &method, const std::string &path) {
    const bool get = method == "GET";
    const bool post = method == "POST";
    const bool put = method == "PUT";
    const bool del = method == "DELETE";

    if (path == "/api/health") return Role::Public;
    if (path.rfind("/api/auth/", 0) == 0) return Role::Public;

    // черновик — автор и выше (проверяем раньше общего префикса /api/admin/)
    if ((put || del) && path.find("/draft") != std::string::npos) return Role::Author;

    // админские маршруты: список, полный шаблон, история, откат
    if (path.rfind("/api/admin/", 0) == 0) return Role::Admin;

    // чтение данных — всем авторизованным
    if (path == "/api/report/data") return Role::Viewer;
    if (get && path.rfind("/api/reports", 0) == 0) return Role::Viewer;

    // публикация, правка и удаление отчёта — только админ
    if (put && path.rfind("/api/reports/", 0) == 0 && path.find("/draft") == std::string::npos) return Role::Admin;
    if (del && path.rfind("/api/reports/", 0) == 0) return Role::Admin;
    if (post && path.rfind("/api/reports/", 0) == 0 && path.find("/publish") != std::string::npos) return Role::Admin;

    // создание отчёта — автор и выше
    if (post && path == "/api/reports") return Role::Author;

    return Role::Admin;
}

} // namespace rep
