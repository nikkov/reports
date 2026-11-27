// Админские маршруты шаблонов (ADR-193). Часть 1: сырой шаблон и список удалённых.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "validate.h"

namespace rep {
namespace app {
struct AppContext;
}

namespace routes {

void registerAdminReportReadRoutes(rep::app::AppContext &ctx);

// Запись: восстановление, сохранение, история, откат, черновики, публикация.
// applyTemplate/prepareTemplate — обёртки над app/template_store и app/template_parse.
void registerAdminReportWriteRoutes(
    rep::app::AppContext &ctx,
    std::function<bool(const std::string &, const std::string &, const std::string &)> applyTemplateInMemory,
    std::function<rep::ValidationResult(const std::string &, const std::string &, std::string *,
                                        std::vector<std::string> *)> prepareTemplate,
    std::function<rep::ValidationResult(const std::string &, const std::string &)> validateTemplateFull);

}  // namespace routes
}  // namespace rep
