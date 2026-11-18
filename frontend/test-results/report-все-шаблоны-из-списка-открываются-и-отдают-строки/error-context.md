# Instructions

- Following Playwright test failed.
- Explain why, be concise, respect Playwright best practices.
- Provide a snippet of code with the fix, if possible.

# Test info

- Name: report.spec.js >> все шаблоны из списка открываются и отдают строки
- Location: e2e/report.spec.js:572:1

# Error details

```
Error: expect(locator).toContainText(expected) failed

Locator: locator('.meta')
Expected substring: "строк"
Timeout: 5000ms
Error: element(s) not found

Call log:
  - Expect "toContainText" locator('.meta') with timeout 5000ms
  - waiting for locator('.meta')

```

```yaml
- banner:
  - 'heading "Проверка: неверная база источника" [level=1]'
  - button "Excel"
  - button "Печать / PDF"
- paragraph: "DB_UNAVAILABLE источник 'main': connection to server at \"127.0.0.1\", port 5432 failed: ВАЖНО: база данных \"no_such_db\" не существует (повтор через 30 с)"
- text: Период с
- textbox "Период с": 2025-11-18
- text: по
- textbox "по": 2026-11-18
- button "Обновить"
- searchbox "Текст для поиска в отчёте"
- button "Найти"
```

```
Error: ошибки в консоли: Failed to load resource: the server responded with a status of 503 (Service Unavailable) | Failed to load resource: the server responded with a status of 503 (Service Unavailable)

expect(received).toEqual(expected) // deep equality

- Expected  - 1
+ Received  + 4

- Array []
+ Array [
+   "Failed to load resource: the server responded with a status of 503 (Service Unavailable)",
+   "Failed to load resource: the server responded with a status of 503 (Service Unavailable)",
+ ]
```

# Test source

```ts
  1   | import { expect, request as apiRequest, test } from '@playwright/test'
  2   | import JSZip from 'jszip'
  3   | import { normalizeDataset } from '../src/api/normalize.js'
  4   | import { readFileSync } from 'node:fs'
  5   | 
  6   | const BASE = '/report.html?report=sales-summary'
  7   | 
  8   | // DoD: «отсутствие ошибок в консоли браузера». Собираем console.error и необработанные
  9   | // исключения на каждой странице и валим тест, если что-то появилось.
  10  | const consoleErrors = new WeakMap()
  11  | 
  12  | test.beforeEach(async ({ page }) => {
  13  |   const errors = []
  14  |   consoleErrors.set(page, errors)
  15  |   page.on('console', (msg) => {
  16  |     if (msg.type() === 'error') errors.push(msg.text())
  17  |   })
  18  |   page.on('pageerror', (err) => errors.push(String(err && err.message ? err.message : err)))
  19  | })
  20  | 
  21  | test.afterEach(async ({ page }) => {
  22  |   const errors = (consoleErrors.get(page) || []).filter((t) => {
  23  |     // запрос favicon браузер делает сам, к отчёту отношения не имеет
  24  |     if (/favicon/i.test(t)) return false
  25  |     // тест, который намеренно шлёт неверный параметр, ожидает HTTP-ошибку от API
  26  |     if (page.__allowHttpErrors && /Failed to load resource/i.test(t)) return false
  27  |     return true
  28  |   })
> 29  |   expect(errors, `ошибки в консоли: ${errors.join(' | ')}`).toEqual([])
      |                                                             ^ Error: ошибки в консоли: Failed to load resource: the server responded with a status of 503 (Service Unavailable) | Failed to load resource: the server responded with a status of 503 (Service Unavailable)
  30  | })
  31  | 
  32  | test('отчёт открывается с параметрами из URL и показывает строки', async ({ page }) => {
  33  |   await page.goto(`${BASE}&from=2026-01-01&to=2026-03-31`)
  34  | 
  35  |   await expect(page.locator('h1')).toHaveText('Сводная таблица продаж')
  36  |   await expect(page.locator('.error')).toHaveCount(0)
  37  |   await expect(page.locator('.text')).toContainText('2026-01-01')
  38  |   await expect(page.locator('.text')).toContainText('2026-03-31')
  39  | 
  40  |   // meta: «строк: 24723 · запрос: N мс · шаблон v1»
  41  |   const meta = page.locator('.meta')
  42  |   await expect(meta).toContainText('строк')
  43  |   const rows = Number((await meta.innerText()).match(/строк:\s*(\d+)/)[1])
  44  |   expect(rows).toBeGreaterThan(20000)
  45  | 
  46  |   // витрина рендерит только окно, а не все строки
  47  |   const domRows = await page.locator('.rt-table tbody tr:not(.spacer)').count()
  48  |   expect(domRows).toBeGreaterThan(5)
  49  |   expect(domRows).toBeLessThan(200)
  50  | })
  51  | 
  52  | test('смена параметра меняет данные и попадает в URL', async ({ page }) => {
  53  |   await page.goto(`${BASE}&from=2026-01-01&to=2026-03-31`)
  54  |   const meta = page.locator('.meta')
  55  |   const before = Number((await meta.innerText()).match(/строк:\s*(\d+)/)[1])
  56  | 
  57  |   await page.fill('input[type="date"] >> nth=0', '2026-03-01')
  58  |   await page.click('button[type="submit"]')
  59  |   await expect(meta).toContainText('строк')
  60  | 
  61  |   await expect.poll(async () => {
  62  |     const text = await meta.innerText()
  63  |     return Number(text.match(/строк:\s*(\d+)/)[1])
  64  |   }).toBeLessThan(before)
  65  | 
  66  |   expect(page.url()).toContain('from=2026-03-01')
  67  | })
  68  | 
  69  | test('битый параметр показывает типизированную ошибку', async ({ page }) => {
  70  |   // здесь 422 — ожидаемый результат, браузерная запись об этом не считается дефектом
  71  |   page.__allowHttpErrors = true
  72  |   await page.goto(`${BASE}&from=01.01.2026&to=2026-03-31`)
  73  |   const error = page.locator('.error')
  74  |   await expect(error).toBeVisible()
  75  |   await expect(error).toContainText('INVALID_PARAMS')
  76  |   await expect(error).toContainText('from')
  77  | })
  78  | 
  79  | test('диаграммы отрисованы на canvas', async ({ page }) => {
  80  |   await page.goto(`${BASE}&from=2026-01-01&to=2026-03-31`)
  81  |   // число блоков диаграмм задаётся шаблоном — проверяем наличие, а не магическое число.
  82  |   // Ждём отрисовку: canvas появляется после загрузки данных и инициализации ECharts.
  83  |   await expect(page.locator('.chart canvas').nth(1)).toBeVisible()
  84  | })
  85  | 
  86  | test('сгруппированный отчёт: подписи групп и объединённые ячейки', async ({ page }) => {
  87  |   await page.goto(`${BASE}&from=2026-03-01&to=2026-03-10`)
  88  | 
  89  |   // первая колонка первой строки — значение группы (регион), вторая — категория
  90  |   const firstRowCells = page.locator('.rt-table tbody tr:not(.spacer)').first().locator('td')
  91  |   await expect(firstRowCells.nth(0)).not.toHaveText('')
  92  |   await expect(firstRowCells.nth(1)).not.toHaveText('')
  93  | 
  94  |   // объединения есть, и они не выходят за окно
  95  |   const merges = await page.locator('.rt-table td[rowspan]').count()
  96  |   expect(merges).toBeGreaterThan(0)
  97  | })
  98  | 
  99  | test('многоуровневый заголовок: группа «Финансы» на две колонки', async ({ page }) => {
  100 |   await page.goto(`${BASE}&from=2026-03-01&to=2026-03-10`)
  101 |   const group = page.locator('.rt-table thead th[colspan="2"]')
  102 |   await expect(group).toHaveCount(1)
  103 |   await expect(group).toHaveText('Финансы')
  104 |   // листья под группой остались на своих местах
  105 |   await expect(page.locator('.rt-table thead th', { hasText: 'Сумма' })).toHaveCount(1)
  106 | })
  107 | 
  108 | test('обе строки заголовка остаются видимыми при прокрутке', async ({ page }) => {
  109 |   await page.goto(`${BASE}&from=2026-03-01&to=2026-03-10`)
  110 | 
  111 |   await page.locator('.rt').evaluate((el) => { el.scrollTop = 20000 })
  112 |   await page.waitForTimeout(300)
  113 | 
  114 |   const group = page.locator('.rt-table thead th[colspan="2"]')
  115 |   const leaf = page.locator('.rt-table thead th', { hasText: 'Сумма' })
  116 |   await expect(group).toBeVisible()
  117 |   await expect(leaf).toBeVisible()
  118 | 
  119 |   const box = await page.locator('.rt').boundingBox()
  120 |   const groupBox = await group.boundingBox()
  121 |   const leafBox = await leaf.boundingBox()
  122 |   // группа — верхняя строка заголовка, листья — сразу под ней, обе внутри видимой области
  123 |   expect(groupBox.y).toBeGreaterThanOrEqual(box.y - 1)
  124 |   expect(groupBox.y).toBeLessThan(box.y + 30)
  125 |   expect(leafBox.y).toBeGreaterThan(groupBox.y)
  126 |   expect(leafBox.y + leafBox.height).toBeLessThan(box.y + 70)
  127 | })
  128 | 
  129 | test('сворачивание и разворачивание группы', async ({ page }) => {
```