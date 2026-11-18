import '../theme/tokens.css'
import { initTheme as initAppTheme } from '../theme/index.js'
import { createApp } from 'vue'
import ReportList from './ReportList.vue'
import { ensureSession } from '../auth/session.js'

async function boot() {
  const session = await ensureSession()
  if (session) createApp(ReportList, { session }).mount('#app')
}
initAppTheme()
boot()
