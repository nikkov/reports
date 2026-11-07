#include "template_migrate.h"

#include <string>

namespace rep {

MigrationResult migrateTemplate(Json::Value &root) {
    MigrationResult result;
    if (!root.isObject()) {
        result.error = "шаблон должен быть JSON-объектом";
        return result;
    }

    int version = 0;
    if (root.isMember("version")) {
        if (!root["version"].isInt()) {
            result.error = "version должен быть целым числом";
            return result;
        }
        version = root["version"].asInt();
    }
    result.fromVersion = version;

    if (version > kCurrentTemplateVersion) {
        result.error = "шаблон версии " + std::to_string(version) + ", сервер поддерживает до " +
                       std::to_string(kCurrentTemplateVersion) + " — обновите сервер";
        return result;
    }

    // 0 → 1: до версионирования поле version не писали
    if (version < 1) {
        root["version"] = 1;
        result.steps.push_back("0→1: добавлена версия формата");
        version = 1;
    }

    // 1 → 2: у диаграмм стало обязательным поле chartType (ADR-51)
    if (version < 2) {
        if (root["blocks"].isArray()) {
            int charts = 0;
            for (auto &block : root["blocks"]) {
                if (!block.isObject()) continue;
                if (block.get("type", "").asString() != "chart") continue;
                if (block.isMember("chartType")) continue;
                block["chartType"] = "bar";
                ++charts;
            }
            if (charts > 0) {
                result.steps.push_back("1→2: у " + std::to_string(charts) +
                                       " диаграмм(ы) chartType по умолчанию «bar»");
            } else {
                result.steps.push_back("1→2: формат помечен версией 2");
            }
        } else {
            result.steps.push_back("1→2: формат помечен версией 2");
        }
        root["version"] = 2;
        version = 2;
    }

    result.toVersion = version;
    result.ok = true;
    return result;
}

} // namespace rep
