import { createApp } from 'vue'
import ReportViewer from './ReportViewer.vue'
import { ensureSession } from '../auth/session.js'

async function boot() {
  const session = await ensureSession()
  if (session) createApp(ReportViewer, { session }).mount('#app')
}
boot()
