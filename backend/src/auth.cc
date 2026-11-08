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

bool validateUserInput(const std::string &login, const std::string &role, const std::string &password,
                       bool creating, std::string &err) {
    if (login.size() < 3 || login.size() > 32) {
        err = "логин: нужно от 3 до 32 символов";
        return false;
    }
    for (char ch : login) {
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '.' || ch == '-';
        if (!ok) {
            err = "логин: допустимы строчные латинские буквы, цифры, '_', '.', '-'";
            return false;
        }
    }
    if (role != "admin" && role != "author" && role != "viewer") {
        err = "роль должна быть admin, author или viewer";
        return false;
    }
    if (creating && password.empty()) {
        err = "пароль обязателен при создании";
        return false;
    }
    if (!password.empty() && password.size() < 8) {
        err = "пароль: не короче 8 символов";
        return false;
    }
    return true;
}

AuthMode parseAuthMode(const std::string &value) {
    if (value == "proxy") return AuthMode::Proxy;
    if (value == "1" || value == "session" || value == "true") return AuthMode::Session;
    return AuthMode::Off;
}

bool isValidProxyLogin(const std::string &login) {
    if (login.empty() || login.size() > 128) return false;
    for (unsigned char ch : login) {
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                        ch == '.' || ch == '_' || ch == '-' || ch == '@' || ch == '\\' || ch == '+' ||
                        ch == '$' || ch == '~' || ch == ':';
        if (!ok) return false;
    }
    return true;
}

std::vector<std::string> parseTrustedProxies(const std::string &value) {
    std::vector<std::string> out;
    std::string item;
    const auto flush = [&out, &item]() {
        const auto first = item.find_first_not_of(" \t");
        const auto last = item.find_last_not_of(" \t");
        if (first != std::string::npos) out.push_back(item.substr(first, last - first + 1));
        item.clear();
    };
    for (char ch : value) {
        if (ch == ',') {
            flush();
        } else {
            item.push_back(ch);
        }
    }
    flush();
    return out;
}

bool isTrustedPeer(const std::string &peer, const std::vector<std::string> &trusted) {
    if (peer.empty()) return false;
    const auto same = [&peer](const std::string &candidate) {
        if (peer == candidate) return true;
        // dual-stack сокет отдаёт IPv4 как ::ffff:a.b.c.d
        static const std::string kMapped = "::ffff:";
        if (candidate.compare(0, kMapped.size(), kMapped) != 0 && peer.compare(0, kMapped.size(), kMapped) == 0) {
            return peer.substr(kMapped.size()) == candidate;
        }
        if (peer.compare(0, kMapped.size(), kMapped) != 0 && candidate.compare(0, kMapped.size(), kMapped) == 0) {
            return candidate.substr(kMapped.size()) == peer;
        }
        return false;
    };
    for (const auto &candidate : trusted) {
        if (same(candidate)) return true;
    }
    return false;
}

} // namespace rep
