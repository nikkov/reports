// Серверная валидация шаблона отчёта (ADR-45): те же правила, что в клиентском
// validate.js, но проверяются на сервере — админский API не должен принимать
// шаблон, который клиент просто не отправил на проверку.
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include <json/json.h>

namespace rep {

struct Issue {
    std::string path;
    std::string message;
};

struct ValidationResult {
    std::vector<Issue> errors;
    std::vector<Issue> warnings;

    bool ok() const { return errors.empty(); }
};

using SqlCheck = std::function<bool(const std::string &sql, std::string &err)>;

// Разрешён только чтение: SELECT/WITH, без ';', '--', '/*' и операций записи.
bool isReadOnlySql(const std::string &sql, std::string &err);

// urlId непустой — значит, id шаблона должен совпадать с адресом.
// maxBytes > 0 — предел размера сериализованного шаблона.
ValidationResult validateTemplateJson(const Json::Value &root,
                                      const std::string &urlId,
                                      std::size_t maxBytes = 0,
                                      const SqlCheck &sqlCheck = nullptr);

} // namespace rep
