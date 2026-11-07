import { createApp } from 'vue'
import AuditApp from './AuditApp.vue'
import { ensureSession } from '../auth/session.js'

// журнал действий — только admin
async function boot() {
  const session = await ensureSession()
  if (!session) return
  if (session.auth && session.role !== 'admin') {
    document.body.innerHTML =
      '<p style="font:14px system-ui;padding:24px;color:#8c1f1f">Недостаточно прав: журнал доступен роли admin.</p>'
    return
  }
  createApp(AuditApp, { session }).mount('#app')
}
boot()
