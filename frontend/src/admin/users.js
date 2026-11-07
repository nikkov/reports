import { createApp } from 'vue'
import UsersApp from './UsersApp.vue'
import { ensureSession } from '../auth/session.js'

// управление пользователями — только admin; сервер повторно проверяет роль на каждом запросе
async function boot() {
  const session = await ensureSession()
  if (!session) return
  if (session.auth && session.role !== 'admin') {
    document.body.innerHTML =
      '<p style="font:14px system-ui;padding:24px;color:#8c1f1f">Недостаточно прав: управление пользователями доступно роли admin.</p>'
    return
  }
  createApp(UsersApp, { session }).mount('#app')
}
boot()
