// Служебные маршруты без домена: /api/health (ADR-193).
#pragma once

namespace rep {
namespace app {
struct AppContext;
}

namespace routes {

void registerSystemRoutes(rep::app::AppContext &ctx);

}  // namespace routes
}  // namespace rep
