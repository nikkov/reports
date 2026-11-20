import '../theme/tokens.css'
import { initTheme as initAppTheme } from '../theme/index.js'
import { createApp } from 'vue'
import AdminShell from './AdminShell.vue'
import { ensureSession } from '../auth/session.js'

// админка доступна только роли admin; сервер всё равно проверит права на каждом запросе
async function boot() {
  const session = await ensureSession()
  if (!session) return
  if (session.auth && session.role !== 'admin') {
    document.body.innerHTML =
      '<p style="font:14px system-ui;padding:24px;color:#8c1f1f">Недостаточно прав: админка доступна роли admin.</p>'
    return
  }
  createApp(AdminShell, { session }).mount('#app')
}
initAppTheme()
boot()
