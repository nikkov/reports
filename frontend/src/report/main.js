import { createApp } from 'vue'
import ReportViewer from './ReportViewer.vue'
import { ensureSession } from '../auth/session.js'
import { api } from '../api/client.js'

async function boot() {
  // Публичный отчёт (ADR-103): открывается по прямой ссылке без входа. Шаблон отдаётся
  // без авторизации и помечен public: true — тогда страницу входа не показываем.
  const reportId = new URLSearchParams(location.search).get('report') || ''
  if (reportId) {
    try {
      const tpl = await api.template(reportId)
      if (tpl && tpl.public === true) {
        createApp(ReportViewer, { session: null }).mount('#app')
        return
      }
    } catch {
      // 401/403/404 — обычный путь: без сессии не пустят
    }
  }
  const session = await ensureSession()
  if (session) createApp(ReportViewer, { session }).mount('#app')
}
boot()
