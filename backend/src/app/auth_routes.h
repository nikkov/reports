// Маршруты входа: /api/auth/login, /logout, /me (ADR-193).
#pragma once

namespace rep {
namespace app {
struct AppContext;
}

namespace routes {

void registerAuthRoutes(rep::app::AppContext &ctx);

}  // namespace routes
}  // namespace rep
