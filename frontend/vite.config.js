import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import { resolve } from 'node:path'
import { ssePassthrough } from './sse-proxy.js'

// Три входа: index.html — песочница рендерера (спайк), report.html — страница отчёта,
// admin.html — админка шаблонов (CodeMirror попадает только в её чанк).
// Приложение можно ставить не только в корень: REP_BASE=/reports/ при сборке
// даёт ассеты и страницы под префиксом, что и нужно за реверс-прокси (ADR-53).
export default defineConfig({
  base: process.env.REP_BASE || '/',
  plugins: [vue(), ssePassthrough()],
  server: {
    port: 5173,
    host: '127.0.0.1',
    proxy: { '/api': 'http://127.0.0.1:8080' }
  },
  // Демонстрация работает на prebuilt dist: preview тоже должен проксировать /api.
  preview: {
    port: 5173,
    host: '127.0.0.1',
    proxy: { '/api': process.env.REP_API || 'http://127.0.0.1:8080' }
  },
  test: {
    include: ['src/**/*.test.js'],
    exclude: ['e2e/**', 'node_modules/**'],
    // DoD: покрытие engine/ не ниже 90% — порог проверяется в `npm run coverage`
    coverage: {
      provider: 'v8',
      include: ['src/engine/**/*.js'],
      exclude: ['src/engine/**/*.test.js'],
      reporter: ['text'],
      thresholds: { lines: 90, branches: 90, functions: 90, statements: 90 }
    }
  },
  build: {
    rollupOptions: {
      input: {
        list: resolve(__dirname, 'index.html'),
        spike: resolve(__dirname, 'spike.html'),
        report: resolve(__dirname, 'report.html'),
        admin: resolve(__dirname, 'admin.html'),
        subscriptions: resolve(__dirname, 'subscriptions.html'),
        users: resolve(__dirname, 'users.html'),
        audit: resolve(__dirname, 'audit.html'),
        login: resolve(__dirname, 'login.html')
      }
    }
  }
})
