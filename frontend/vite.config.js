import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import { resolve } from 'node:path'

// Три входа: index.html — песочница рендерера (спайк), report.html — страница отчёта,
// admin.html — админка шаблонов (CodeMirror попадает только в её чанк).
export default defineConfig({
  plugins: [vue()],
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
        login: resolve(__dirname, 'login.html')
      }
    }
  }
})
