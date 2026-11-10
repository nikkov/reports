import { defineConfig } from '@playwright/test'

// e2e страницы отчёта. Всё обслуживает один процесс на :8080 — и API, и статика
// (ADR-79, ADR-80); отдельного Vite-сервера больше нет.
export default defineConfig({
  testDir: './e2e',
  // вход выполняется один раз за прогон; cookie admin подставляется всем тестам
  globalSetup: './e2e/global-setup.js',
  timeout: 30_000,
  retries: 0,
  workers: 1,
  reporter: [['list']],
  expect: {
    // скриншоты-эталоны: без анимаций, допуск на антиалиасинг
    toHaveScreenshot: { maxDiffPixelRatio: 0.02, animations: 'disabled', scale: 'css' }
  },
  use: {
    baseURL: process.env.REP_E2E_BASE || 'http://127.0.0.1:8080',
    storageState: '.auth/admin.json',
    trace: 'off'
  }
})
