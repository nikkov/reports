#include "app/sse.h"

#include <atomic>
#include <map>
#include <mutex>
#include <vector>

#include <drogon/drogon.h>

namespace rep {
namespace app {
namespace sse {

namespace {

std::mutex sseMutex;
std::map<std::string, std::map<uint64_t, std::shared_ptr<drogon::ResponseStream>>> sseClients;
std::atomic<uint64_t> sseNextId{1};
std::atomic<bool> sseHeartbeatStarted{false};

void sseWrite(const std::shared_ptr<drogon::ResponseStream> &stream, const std::string &payload) {
    if (!stream) return;
    stream->send(payload);
}

// пульс: держит соединение живым и заодно убирает отвалившихся клиентов
void sseHeartbeat() {
    std::lock_guard<std::mutex> lock(sseMutex);
    for (auto report = sseClients.begin(); report != sseClients.end();) {
        for (auto client = report->second.begin(); client != report->second.end();) {
            if (!client->second->send(": ping\n\n")) {
                client = report->second.erase(client);
            } else {
                ++client;
            }
        }
        if (report->second.empty()) {
            report = sseClients.erase(report);
        } else {
            ++report;
        }
    }
}

}  // namespace

uint64_t nextClientId() {
    return sseNextId++;
}

void registerClient(const std::string &reportId, uint64_t clientId,
                    std::shared_ptr<drogon::ResponseStream> stream) {
    sseWrite(stream, ": connected\n\n");
    {
        std::lock_guard<std::mutex> lock(sseMutex);
        sseClients[reportId][clientId] = std::move(stream);
    }
}

void notifyChange(const std::string &reportId) {
    std::vector<std::shared_ptr<drogon::ResponseStream>> targets;
    {
        std::lock_guard<std::mutex> lock(sseMutex);
        const auto it = sseClients.find(reportId);
        if (it == sseClients.end()) return;
        for (const auto &kv : it->second) targets.push_back(kv.second);
    }
    const std::string payload = "event: change\ndata: {\"reportId\":\"" + reportId + "\"}\n\n";
    for (const auto &stream : targets) sseWrite(stream, payload);
}

void startHeartbeat() {
    if (!sseHeartbeatStarted.exchange(true)) {
        drogon::app().getLoop()->runEvery(15.0, sseHeartbeat);
    }
}

}  // namespace sse
}  // namespace app
}  // namespace rep
