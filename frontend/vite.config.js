import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import { resolve } from 'node:path'
import { ssePassthrough } from './sse-proxy.js'

// Три входа: index.html — песочница рендерера (спайк), report.html — страница отчёта,
// admin.html — админка шаблонов (CodeMirror попадает только в её чанк).
// Сборка переносимая: base './' даёт относительные ссылки на ассеты, поэтому
// каталог dist можно скопировать куда угодно без пересборки (ADR-53).
// Фиксированный префикс нужен редко — тогда REP_BASE=/reports/.
export default defineConfig({
  base: process.env.REP_BASE || './',
  // Отдельного dev/preview-сервера нет: и API, и статику отдаёт rep на :8080
  // (ADR-79, ADR-80), поэтому у стенда один порт. Vite нужен только для сборки.
  plugins: [vue(), ssePassthrough()],
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
