// Замер рендера большого отчёта (DoD: 50 000 строк до интерактивности ≤ 2 с).
// Запуск: npm run bench [-- <id отчёта>], сервер должен быть поднят (REP_BENCH_URL).
import { chromium } from '@playwright/test'
import { existsSync } from 'node:fs'

const report = process.argv[2] || 'bench-50k'
const base = process.env.REP_BENCH_URL || 'http://127.0.0.1:8080/reports'
const storage = process.env.REP_BENCH_AUTH || '.auth/admin.json'
if (!existsSync(storage)) {
  console.error(`нет сессии ${storage}: сначала e2e (global-setup сохраняет .auth/admin.json)`)
  process.exit(2)
}

const browser = await chromium.launch()
const context = await browser.newContext({
  viewport: { width: 1600, height: 900 },
  storageState: storage
})
const page = await context.newPage()

const started = Date.now()
await page.goto(`${base}/report.html?report=${report}`)
// интерактивность = появились строки таблицы
await page.waitForFunction(() => document.querySelectorAll('.rt-table tbody tr').length > 0, null, {
  timeout: 180000
})
const firstRowMs = Date.now() - started
const meta = (await page.locator('.meta').first().innerText()).trim()
const rowsInDom = await page.evaluate(() => document.querySelectorAll('.rt-table tbody tr').length)

// прокрутка в середину: виртуализация должна перерисовать окно, а не весь набор
const scrollStart = Date.now()
await page.evaluate(() => {
  const el = document.querySelector('.rt-screen')
  if (el) el.scrollTop = Math.round(el.scrollHeight / 2)
})
await page.waitForTimeout(400)
const scrollMs = Date.now() - scrollStart
const rowsAfterScroll = await page.evaluate(() => document.querySelectorAll('.rt-table tbody tr').length)

// разбивка: сколько занял запрос данных и сколько — клиент (передача, разбор, рендер)
const apiMs = await page.evaluate(() => {
  const entry = performance
    .getEntriesByType('resource')
    .filter((e) => e.name.includes('/api/report/data'))
    .pop()
  return entry ? Math.round(entry.duration) : null
})
const clientMs = apiMs === null ? null : Math.max(0, firstRowMs - apiMs)

const total = Number((meta.match(/строк:\s*([\d\s]+)/) || [])[1]?.replace(/\s/g, '')) || null
const budget = 2000
const ok = firstRowMs <= budget

console.log(
  JSON.stringify(
    {
      report,
      rows: total,
      meta,
      firstRowMs,
      apiMs,
      clientMs,
      budgetMs: budget,
      withinBudget: ok,
      rowsInDom,
      rowsAfterScroll,
      scrollMs
    },
    null,
    2
  )
)
await browser.close()
process.exit(ok ? 0 : 1)
