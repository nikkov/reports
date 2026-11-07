import { createApp } from 'vue'
import ReportList from './ReportList.vue'
import { ensureSession } from '../auth/session.js'

async function boot() {
  const session = await ensureSession()
  if (session) createApp(ReportList, { session }).mount('#app')
}
boot()
