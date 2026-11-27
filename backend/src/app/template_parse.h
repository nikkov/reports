// Разбор JSON шаблона в TemplateInfo (ADR-27, ADR-104). Вынесено из main.cc (ADR-193).
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "validate.h"

#include "app/template_types.h"

namespace rep {
namespace app {

// Полная проверка шаблона: размер, JSON, миграция версии, схема (ADR-96).
// normalized — нормализованный JSON с отступами, steps — выполненные миграции.
rep::ValidationResult prepareTemplateImpl(std::size_t maxTemplateBytes, const std::string &raw,
                                        const std::string &urlId, std::string *normalized,
                                        std::vector<std::string> *steps);

rep::ValidationResult validateTemplateFull(std::size_t maxTemplateBytes, const std::string &raw,
                                          const std::string &urlId);

// Проверка с текстом первой ошибки: false — err заполнен.
bool validateTemplateText(std::size_t maxTemplateBytes, const std::string &raw, const std::string &urlId,
                          std::string &err);

// false — шаблон не разобран (нет id/наборов, queryId, битая ссылка join и т.п.).
bool parseTemplate(const std::string &text, const std::string &fallbackId, TemplateInfo &out);

}  // namespace app
}  // namespace rep
