// Типы данных шаблона и набора: нужны и HTTP-модулям, и экспорту (ADR-193).
// Вынесено из main.cc без изменения полей — это общий контракт между частями сервера.
#pragma once

#include <map>
#include <memory>
#include <string>

#include "connections.h"
#include "rest_client.h"

namespace rep {
namespace app {

// Набор данных шаблона (ADR-36, ADR-104): SQL, REST или join.
struct DatasetInfo {
    std::string sql;
    std::map<std::string, std::string> params; // имя параметра запроса → "$param" шаблона
    std::string connection;                    // ключ connections; пусто — источник сервера (ADR-76)
    std::string kind = "sql";                  // sql (по умолчанию), rest или join
    rep::RestSpec rest;
    rep::JoinSpec join;
};

// Разобранный шаблон: то, что сервер читает на старте и отдаёт обработчикам.
struct TemplateInfo {
    std::string id;
    std::string title;
    std::string datasetSql;                          // SQL первого набора (совместимость)
    std::map<std::string, std::string> datasetParams;
    std::map<std::string, DatasetInfo> datasets;     // все наборы шаблона (ADR-36)
    std::string datasetConnection;                   // соединение первого набора
    std::map<std::string, rep::ConnSpec> connections; // источники данных шаблона (ADR-76)
};

// Итог выбора шаблона и набора данных: один тип на все обработчики (data/aggregate/
// group/text/экспорт), чтобы не держать пять копий одного и того же выбора (ADR-105).
struct ResolvedDataset {
    std::shared_ptr<TemplateInfo> tpl;
    std::string key;
    std::string kind = "sql";
    std::string sql;
    std::string connection;
    std::map<std::string, std::string> params;
    int status = 0;
    std::string code;
    std::string message;
};

}  // namespace app
}  // namespace rep
