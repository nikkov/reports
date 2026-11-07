// Версия формата шаблона и миграции между версиями (ADR-51).
// Версия хранится в поле version; сервер поднимает старые шаблоны до текущей
// версии при сохранении и публикации, а слишком новые — отклоняет.
#pragma once

#include <string>
#include <vector>

#include <json/json.h>

namespace rep {

// Текущая версия формата. Поднятие версии = новая ступень миграции ниже.
constexpr int kCurrentTemplateVersion = 2;

struct MigrationResult {
    bool ok = false;
    int fromVersion = 0;
    int toVersion = 0;
    std::vector<std::string> steps; // что именно сделано: «1→2: …»
    std::string error;
};

// Приводит шаблон к kCurrentTemplateVersion. Меняет root на месте.
MigrationResult migrateTemplate(Json::Value &root);

} // namespace rep
