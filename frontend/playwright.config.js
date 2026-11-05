import { defineConfig } from '@playwright/test'

// e2e страницы отчёта. Сервер отчётов и dev-сервер поднимаются снаружи (check.sh),
// локально Playwright переиспользует уже запущенный Vite.
export default defineConfig({
  testDir: './e2e',
  timeout: 30_000,
  retries: 0,
  workers: 1,
  reporter: [['list']],
  use: {
    baseURL: process.env.REP_E2E_BASE || 'http://127.0.0.1:5173',
    trace: 'off'
  }
})
