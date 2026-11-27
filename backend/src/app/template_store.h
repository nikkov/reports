// Память шаблонов: атомарная замена и удаление (ADR-153, ADR-193).
// Раньше это были лямбды внутри main(), и любой маршрут, которому нужна публикация,
// тянул за собой кусок инициализации.
#pragma once

#include <string>

namespace rep {
namespace app {

struct AppContext;

// Разбирает и мигрирует шаблон, кладёт в карты под мьютексом, уведомляет SSE,
// увеличивает версию шаблонов и чистит кэш результатов. false — шаблон не разобран.
bool applyTemplate(AppContext &ctx, const std::string &id, const std::string &raw,
                   const std::string &title);

// Убирает шаблон из памяти (мягкое удаление и откат к состоянию без шаблона).
void dropTemplate(AppContext &ctx, const std::string &id);

// Версия шаблонов и кэш результатов: любое изменение набора данных делает старые ответы
// невалидными (ADR-64).
void bumpTemplatesVersion(AppContext &ctx);

}  // namespace app
}  // namespace rep
