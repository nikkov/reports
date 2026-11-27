// Работа с БД и мелочи запроса, общие для всех HTTP-модулей (ADR-193).
#pragma once

#include <drogon/HttpRequest.h>
#include <drogon/orm/DbClient.h>
#include <drogon/orm/Result.h>

#include <exception>
#include <functional>
#include <string>
#include <vector>

namespace rep {
namespace app {

std::string requestIp(const drogon::HttpRequestPtr &req);

// Текст ошибки из std::exception_ptr (для ответов админки).
std::string describeError(const std::exception_ptr &e);

// Выполняет запрос с параметрами. timeoutMs > 0 — через это время запрос считается зависшим
// и отвечает ошибкой (ADR-125): внешний источник может висеть, а пользователь ждать не должен.
void runQuery(const drogon::orm::DbClientPtr &db, const std::string &sql,
              const std::vector<std::string> &values,
              std::function<void(const drogon::orm::Result &)> onOk,
              std::function<void(const std::exception_ptr &)> onErr, int timeoutMs = 0);

}  // namespace app
}  // namespace rep
