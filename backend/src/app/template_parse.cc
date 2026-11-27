#include "app/template_parse.h"

#include <sstream>

#include <json/json.h>

#include "connections.h"
#include "join.h"
#include "rest_client.h"
#include "template_migrate.h"
#include "validate.h"

namespace rep {
namespace app {

bool parseTemplate(const std::string &text, const std::string &fallbackId, TemplateInfo &out) {
    Json::Value root;
    Json::CharReaderBuilder b;
    std::string errs;
    std::istringstream in(text);
    if (!Json::parseFromStream(b, in, &root, &errs)) return false;

    out = TemplateInfo{};
    out.id = root.get("id", fallbackId).asString();
    out.title = root.get("title", out.id).asString();
    std::string connError;
    if (!rep::parseConnections(root["connections"], out.connections, connError)) return false;
    const Json::Value &datasets = root["datasets"];
    if (!datasets.isObject() || datasets.empty()) return true;  // шаблон без данных
    // SQL набора данных живёт в шаблоне (ADR-27); queryId из реестра больше не поддерживается
    out.datasets.clear();
    for (auto ds = datasets.begin(); ds != datasets.end(); ++ds) {
        if (ds->isMember("queryId")) return false;
        DatasetInfo info;
        const Json::Value &p = (*ds)["params"];
        info.connection = (*ds).get("connection", "").asString();
        if (p.isObject()) {
            for (auto it = p.begin(); it != p.end(); ++it) info.params[it.name()] = it->asString();
        }
        // Три вида набора (ADR-104): SQL, REST и объединение. Ровно один.
        std::string kindErr;
        if (ds->isMember("join")) {
            if (!rep::parseJoin((*ds)["join"], info.join, kindErr)) return false;
            info.kind = "join";
        } else if (ds->isMember("rest")) {
            if (!rep::parseRestSpec((*ds)["rest"], info.rest, kindErr)) return false;
            info.kind = "rest";
        } else {
            info.sql = (*ds).get("sql", "").asString();
            if (info.sql.empty()) return false;
        }
        out.datasets[ds.name()] = info;
    }
    // Ссылки join: стороны существуют и сами не являются объединением (один уровень).
    for (const auto &kv : out.datasets) {
        if (kv.second.kind != "join") continue;
        for (const std::string &side : {kv.second.join.left, kv.second.join.right}) {
            if (out.datasets.find(side) == out.datasets.end()) return false;
        }
    }
    // Цепочки A⋈B⋈C разрешены, цикл — нет: обход по ссылкам должен дойти до конца.
    for (const auto &kv : out.datasets) {
        if (kv.second.kind != "join") continue;
        std::set<std::string> seen;
        std::vector<std::string> stack{kv.second.join.left, kv.second.join.right};
        while (!stack.empty()) {
            const std::string cur = stack.back();
            stack.pop_back();
            if (cur == kv.first) return false; // цикл
            if (!seen.insert(cur).second) continue;
            const auto it = out.datasets.find(cur);
            if (it == out.datasets.end() || it->second.kind != "join") continue;
            stack.push_back(it->second.join.left);
            stack.push_back(it->second.join.right);
        }
    }
    const auto &first = out.datasets.begin()->second;
    if (first.kind == "sql") {
        out.datasetSql = first.sql;
        out.datasetParams = first.params;
        out.datasetConnection = first.connection;
    }
    return true;
}

rep::ValidationResult prepareTemplateImpl(std::size_t maxTemplateBytes, const std::string &raw,
                                        const std::string &urlId, std::string *normalized,
                                        std::vector<std::string> *steps) {
        rep::ValidationResult result;
        if (raw.size() > maxTemplateBytes) {
            result.errors.push_back({"(корень)", "шаблон больше " + std::to_string(maxTemplateBytes) + " байт"});
            return result;
        }
        Json::CharReaderBuilder rb;
        Json::Value root;
        std::string parseErr;
        std::istringstream in(raw);
        if (!Json::parseFromStream(rb, in, &root, &parseErr)) {
            result.errors.push_back({"(корень)", "шаблон не является JSON: " + parseErr});
            return result;
        }
        const auto migration = rep::migrateTemplate(root);
        if (!migration.ok) {
            result.errors.push_back({"version", migration.error});
            return result;
        }
        if (steps) *steps = migration.steps;
        if (normalized) {
            Json::StreamWriterBuilder wb;
            wb["indentation"] = "  ";
            *normalized = Json::writeString(wb, root);
        }
        return rep::validateTemplateJson(root, urlId);
}

rep::ValidationResult validateTemplateFull(std::size_t maxTemplateBytes, const std::string &raw,
                                          const std::string &urlId) {
    return prepareTemplateImpl(maxTemplateBytes, raw, urlId, nullptr, nullptr);
}

bool validateTemplateText(std::size_t maxTemplateBytes, const std::string &raw, const std::string &urlId,
                          std::string &err) {
        const auto result = validateTemplateFull(maxTemplateBytes, raw, urlId);
        if (result.ok()) return true;
        const auto &first = result.errors.front();
        err = first.path == "(корень)" ? first.message : first.path + ": " + first.message;
        return false;
}

}  // namespace app
}  // namespace rep
