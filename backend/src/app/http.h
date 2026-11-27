// HTTP-обвязка сервера: ответы JSON, коды ошибок и диагностический мьютекс (ADR-193).
// Вынесено из main.cc без изменения поведения.
#pragma once

#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>

#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include <json/json.h>

namespace rep {
namespace http {

// Диагностика взаимной блокировки (ADR-152): при REP_DEBUG_MUTEX=1 печатаем захват и
// освобождение мьютекса шаблонов с идентификатором потока — видно, кто держит его вечно.
// Причина дедлока найдена и устранена (ADR-153), диагностика осталась для будущих ошибок.
class TracedMutex {
public:
    void lock();
    void unlock();

private:
    static bool enabled();
    static std::size_t tid();
    static void dumpStack(const char *tag, void *self, std::size_t threadId);
    static bool &held();

    std::mutex m;
};

// Логин запроса, который обрабатывается в этом потоке. sendError не получает req
// (ботве 180 вызовов), а в журнале нужно видеть, кто получил ошибку (ADR-128).
std::string &actorRef();
void setActor(const std::string &login);

Json::Value errorBody(const std::string &code, const std::string &message);

drogon::HttpResponsePtr jsonResponse(const Json::Value &body,
                                     drogon::HttpStatusCode status = drogon::k200OK);

void sendError(const std::function<void(const drogon::HttpResponsePtr &)> &cb, drogon::HttpStatusCode status,
               const std::string &code, const std::string &message);

}  // namespace http
}  // namespace rep
