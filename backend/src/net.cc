#include "net.h"

#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

namespace rep {
namespace net {

bool startup(std::string &err) {
#ifdef _WIN32
    static bool started = false;
    if (started) return true;
    WSADATA data{};
    const int code = ::WSAStartup(MAKEWORD(2, 2), &data);
    if (code != 0) {
        err = "WSAStartup: " + errorText(code);
        return false;
    }
    started = true;
#else
    (void)err;
#endif
    return true;
}

void cleanup() {
#ifdef _WIN32
    ::WSACleanup();
#endif
}

std::string errorText(int code) {
#ifdef _WIN32
    // winsock не пишет в errno: strerror() вернул бы «Unknown error» с чужим кодом
    char *buffer = nullptr;
    const DWORD len = ::FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                           FORMAT_MESSAGE_IGNORE_INSERTS,
                                       nullptr, static_cast<DWORD>(code), 0,
                                       reinterpret_cast<char *>(&buffer), 0, nullptr);
    if (len && buffer) {
        std::string text(buffer, len);
        ::LocalFree(buffer);
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
        return text + " (" + std::to_string(code) + ")";
    }
    return "код " + std::to_string(code);
#else
    return std::strerror(code);
#endif
}

bool setIoTimeout(socket_t s, int seconds) {
    if (!valid(s)) return false;
    if (seconds <= 0) return false;
#ifdef _WIN32
    const DWORD ms = static_cast<DWORD>(seconds) * 1000;
    return ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&ms), sizeof(ms)) == 0 &&
           ::setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&ms), sizeof(ms)) == 0;
#else
    timeval tv{};
    tv.tv_sec = seconds;
    return ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0 &&
           ::setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0;
#endif
}

bool portFree(int port) {
    const socket_t probe = create(AF_INET, SOCK_STREAM, 0);
    if (!valid(probe)) return true; // нет сокета — не наше дело мешать запуску

    int one = 1;
#ifdef _WIN32
    ::setsockopt(probe, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&one), sizeof(one));
#else
    ::setsockopt(probe, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = ::htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = INADDR_ANY;
    const bool busy = ::bind(probe, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0;
    closeSocket(probe);
    return !busy;
}

std::string exeDir() {
    namespace fs = std::filesystem;
    std::error_code ec;
#ifdef _WIN32
    char buffer[MAX_PATH * 4];
    const DWORD size = ::GetModuleFileNameA(nullptr, buffer, static_cast<DWORD>(sizeof(buffer)));
    if (size == 0 || size >= sizeof(buffer)) return {};
    const auto dir = fs::path(std::string(buffer, size)).parent_path();
#elif defined(__linux__)
    char buffer[4096];
    const ssize_t size = ::readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (size <= 0) return {};
    buffer[size] = '\0';
    const auto dir = fs::path(std::string(buffer, static_cast<size_t>(size))).parent_path();
#else
    return {};
#endif
    const auto canonical = fs::weakly_canonical(dir, ec);
    return ec ? dir.string() : canonical.string();
}

std::time_t timegmUtc(std::tm *tm) {
#ifdef _WIN32
    return ::_mkgmtime(tm);
#else
    return ::timegm(tm);
#endif
}

} // namespace net
} // namespace rep
