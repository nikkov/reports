// Сокеты и UTC-время одной точкой: под Windows это Winsock2, под POSIX — системные
// вызовы. Остальной код не знает, где он собирается (порт на Visual Studio 2022).
#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX // std::min/std::max в windows.h ломают шаблоны
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include <cerrno>
#include <cstring>
#include <ctime>
#include <string>

namespace rep {
namespace net {

#ifdef _WIN32
using socket_t = SOCKET;
inline const socket_t kInvalidSocket = INVALID_SOCKET;
#else
using socket_t = int;
inline const socket_t kInvalidSocket = -1;
#endif

// Каталог, где лежит исполняемый файл: от него ищется каталог статики, когда сервер
// и собранный фронтенд разложены одним комплектом (ADR-79). Пустая строка, если
// платформа не умеет его определить — тогда остаются пути от рабочего каталога.
std::string exeDir();

// WSAStartup под Windows, пустышка под POSIX. Вызывается один раз на процесс,
// до первого сокета (в main и в тестах, если они работают с сетью).
bool startup(std::string &err);
void cleanup();

inline socket_t create(int family, int type, int protocol) { return ::socket(family, type, protocol); }

inline bool valid(socket_t s) {
#ifdef _WIN32
    return s != INVALID_SOCKET;
#else
    return s >= 0;
#endif
}

inline void closeSocket(socket_t s) {
    if (!valid(s)) return;
#ifdef _WIN32
    ::closesocket(s);
#else
    ::close(s);
#endif
}

inline int lastError() {
#ifdef _WIN32
    return ::WSAGetLastError();
#else
    return errno;
#endif
}

std::string errorText(int code);

// Таймаут приёма и передачи: Windows ждёт DWORD в миллисекундах, POSIX — timeval.
bool setIoTimeout(socket_t s, int seconds);

// Свободен ли порт: проба bind() с SO_REUSEADDR (иначе TIME_WAIT после остановки
// сервера даёт ложное «порт занят»). true — можно занимать.
bool portFree(int port);

// Время в UTC без пояса: timegm на POSIX, _mkgmtime на MSVC. Расписание подписок
// считается в «наивном» календаре, поэтому функция нужна обеим платформам.
std::time_t timegmUtc(std::tm *tm);

} // namespace net
} // namespace rep
