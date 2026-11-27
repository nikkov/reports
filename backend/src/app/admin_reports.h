// Админские маршруты шаблонов (ADR-193). Часть 1: сырой шаблон и список удалённых.
#pragma once

namespace rep {
namespace app {
struct AppContext;
}

namespace routes {

void registerAdminReportReadRoutes(rep::app::AppContext &ctx);

}  // namespace routes
}  // namespace rep
