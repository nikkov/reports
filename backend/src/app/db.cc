#include "app/db.h"

#include <drogon/drogon.h>

#include <atomic>
#include <memory>
#include <stdexcept>

namespace rep {
namespace app {

std::string requestIp(const drogon::HttpRequestPtr &req) {
    if (!req) return "";
    return req->peerAddr().toIp();
}

std::string describeError(const std::exception_ptr &e) {
    std::string msg = "ошибка выполнения";
    try {
        if (e) std::rethrow_exception(e);
    } catch (const std::exception &ex) {
        msg = ex.what();
    }
    return msg;
}

void runQuery(const drogon::orm::DbClientPtr &db, const std::string &sql,
              const std::vector<std::string> &values,
              std::function<void(const drogon::orm::Result &)> onOk,
              std::function<void(const std::exception_ptr &)> onErr, int timeoutMs) {
    auto binder = *db << sql;
    for (const auto &v : values) binder << v;
    binder << drogon::orm::Mode::NonBlocking;
    auto done = std::make_shared<std::atomic<bool>>(false);
    if (timeoutMs > 0) {
        drogon::app().getLoop()->runAfter(
            static_cast<double>(timeoutMs) / 1000.0, [done, onErr, timeoutMs]() {
                if (done->exchange(true)) return;
                onErr(std::make_exception_ptr(std::runtime_error(
                    "источник не ответил за " + std::to_string(timeoutMs) +
                    " мс: проверьте host, port и database соединения, а также доступность сервера")));
            });
    }
    binder >> [done, onOk](const drogon::orm::Result &res) {
        if (!done->exchange(true)) onOk(res);
    };
    binder >> [done, onErr](const std::exception_ptr &e) {
        if (!done->exchange(true)) onErr(e);
    };
    binder.exec();
}

}  // namespace app
}  // namespace rep
