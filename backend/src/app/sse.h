// Обновление по событию (ADR-44): одно SSE-соединение на зрителя. Вынесено из main.cc (ADR-193).
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <drogon/HttpResponse.h>

namespace rep {
namespace app {
namespace sse {

// Идентификатор клиента и регистрация потока: с этого момента сервер может ему писать.
uint64_t nextClientId();
void registerClient(const std::string &reportId, uint64_t clientId,
                    std::shared_ptr<drogon::ResponseStream> stream);

// Сообщает всем зрителям отчёта, что шаблон изменился.
void notifyChange(const std::string &reportId);

// Запускает пульс раз в 15 с (один раз на процесс): держит соединение живым
// и заодно убирает отвалившихся клиентов.
void startHeartbeat();

}  // namespace sse
}  // namespace app
}  // namespace rep
