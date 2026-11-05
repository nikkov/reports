import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import { resolve } from 'node:path'

// Два входа: index.html — песочница рендерера (спайк), report.html — страница отчёта.
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
    exclude: ['e2e/**', 'node_modules/**']
  },
  build: {
    rollupOptions: {
      input: {
        main: resolve(__dirname, 'index.html'),
        report: resolve(__dirname, 'report.html')
      }
    }
  }
})
