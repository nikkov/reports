// Разбор JSON шаблона в TemplateInfo (ADR-27, ADR-104). Вынесено из main.cc (ADR-193).
#pragma once

#include <string>

#include "app/template_types.h"

namespace rep {
namespace app {

// false — шаблон не разобран (нет id/наборов, queryId, битая ссылка join и т.п.).
bool parseTemplate(const std::string &text, const std::string &fallbackId, TemplateInfo &out);

}  // namespace app
}  // namespace rep
