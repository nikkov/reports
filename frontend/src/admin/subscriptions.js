import { createApp } from 'vue'
import SubscriptionsApp from './SubscriptionsApp.vue'
import { ensureSession } from '../auth/session.js'

// рассылка — админская функция; сервер повторно проверяет роль на каждом запросе
async function boot() {
  const session = await ensureSession()
  if (!session) return
  if (session.auth && session.role !== 'admin') {
    document.body.innerHTML =
      '<p style="font:14px system-ui;padding:24px;color:#8c1f1f">Недостаточно прав: рассылка доступна роли admin.</p>'
    return
  }
  createApp(SubscriptionsApp, { session }).mount('#app')
}
boot()
