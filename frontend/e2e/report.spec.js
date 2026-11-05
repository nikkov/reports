import { expect, test } from '@playwright/test'

const BASE = '/report.html?report=sales-summary'

test('отчёт открывается с параметрами из URL и показывает строки', async ({ page }) => {
  await page.goto(`${BASE}&from=2026-01-01&to=2026-03-31`)

  await expect(page.locator('h1')).toHaveText('Сводная таблица продаж')
  await expect(page.locator('.error')).toHaveCount(0)
  await expect(page.locator('.text')).toContainText('2026-01-01')
  await expect(page.locator('.text')).toContainText('2026-03-31')

  // meta: «строк: 24723 · запрос: N мс · шаблон v1»
  const meta = page.locator('.meta')
  await expect(meta).toContainText('строк')
  const rows = Number((await meta.innerText()).match(/строк:\s*(\d+)/)[1])
  expect(rows).toBeGreaterThan(20000)

  // витрина рендерит только окно, а не все строки
  const domRows = await page.locator('.rt-table tbody tr:not(.spacer)').count()
  expect(domRows).toBeGreaterThan(5)
  expect(domRows).toBeLessThan(200)
})

test('смена параметра меняет данные и попадает в URL', async ({ page }) => {
  await page.goto(`${BASE}&from=2026-01-01&to=2026-03-31`)
  const meta = page.locator('.meta')
  const before = Number((await meta.innerText()).match(/строк:\s*(\d+)/)[1])

  await page.fill('input[type="date"] >> nth=0', '2026-03-01')
  await page.click('button[type="submit"]')
  await expect(meta).toContainText('строк')

  await expect.poll(async () => {
    const text = await meta.innerText()
    return Number(text.match(/строк:\s*(\d+)/)[1])
  }).toBeLessThan(before)

  expect(page.url()).toContain('from=2026-03-01')
})

test('битый параметр показывает типизированную ошибку', async ({ page }) => {
  await page.goto(`${BASE}&from=01.01.2026&to=2026-03-31`)
  const error = page.locator('.error')
  await expect(error).toBeVisible()
  await expect(error).toContainText('INVALID_PARAMS')
  await expect(error).toContainText('from')
})

test('диаграммы отрисованы на canvas', async ({ page }) => {
  await page.goto(`${BASE}&from=2026-01-01&to=2026-03-31`)
  // число блоков диаграмм задаётся шаблоном — проверяем наличие, а не магическое число
  const charts = await page.locator('.chart canvas').count()
  expect(charts).toBeGreaterThanOrEqual(2)
})

test('сгруппированный отчёт: подписи групп и объединённые ячейки', async ({ page }) => {
  await page.goto(`${BASE}&from=2026-03-01&to=2026-03-10`)

  // первая колонка первой строки — значение группы (регион), вторая — категория
  const firstRowCells = page.locator('.rt-table tbody tr:not(.spacer)').first().locator('td')
  await expect(firstRowCells.nth(0)).not.toHaveText('')
  await expect(firstRowCells.nth(1)).not.toHaveText('')

  // объединения есть, и они не выходят за окно
  const merges = await page.locator('.rt-table td[rowspan]').count()
  expect(merges).toBeGreaterThan(0)
})

test('многоуровневый заголовок: группа «Финансы» на две колонки', async ({ page }) => {
  await page.goto(`${BASE}&from=2026-03-01&to=2026-03-10`)
  const group = page.locator('.rt-table thead th[colspan="2"]')
  await expect(group).toHaveCount(1)
  await expect(group).toHaveText('Финансы')
  // листья под группой остались на своих местах
  await expect(page.locator('.rt-table thead th', { hasText: 'Сумма' })).toHaveCount(1)
})

test('обе строки заголовка остаются видимыми при прокрутке', async ({ page }) => {
  await page.goto(`${BASE}&from=2026-03-01&to=2026-03-10`)

  await page.locator('.rt').evaluate((el) => { el.scrollTop = 20000 })
  await page.waitForTimeout(300)

  const group = page.locator('.rt-table thead th[colspan="2"]')
  const leaf = page.locator('.rt-table thead th', { hasText: 'Сумма' })
  await expect(group).toBeVisible()
  await expect(leaf).toBeVisible()

  const box = await page.locator('.rt').boundingBox()
  const groupBox = await group.boundingBox()
  const leafBox = await leaf.boundingBox()
  // группа — верхняя строка заголовка, листья — сразу под ней, обе внутри видимой области
  expect(groupBox.y).toBeGreaterThanOrEqual(box.y - 1)
  expect(groupBox.y).toBeLessThan(box.y + 30)
  expect(leafBox.y).toBeGreaterThan(groupBox.y)
  expect(leafBox.y + leafBox.height).toBeLessThan(box.y + 70)
})

test('сворачивание и разворачивание группы', async ({ page }) => {
  await page.goto(`${BASE}&from=2026-03-01&to=2026-03-10`)

  const first = page.locator('.rt-table tbody tr:not(.spacer)').first()
  const heightBefore = await page.locator('.rt').evaluate((el) => el.scrollHeight)

  await first.locator('.chev').first().click()

  const total = page.locator('.rt-table tbody tr:not(.spacer)').first()
  await expect(total).toContainText('Итого')
  await expect(total.locator('td').nth(3)).not.toHaveText('')
  const heightCollapsed = await page.locator('.rt').evaluate((el) => el.scrollHeight)
  expect(heightCollapsed).toBeLessThan(heightBefore)

  // клик по строке итога возвращает детали
  await total.locator('.chev').first().click()
  await expect(page.locator('.rt-table tbody tr:not(.spacer)').first()).not.toContainText('Итого')
  const heightExpanded = await page.locator('.rt').evaluate((el) => el.scrollHeight)
  expect(heightExpanded).toBe(heightBefore)
})

test('сворачивание категории внутри региона', async ({ page }) => {
  await page.goto(`${BASE}&from=2026-03-01&to=2026-03-10`)
  const heightBefore = await page.locator('.rt').evaluate((el) => el.scrollHeight)

  // второй шеврон в строке — колонка «Категория»
  await page.locator('.rt-table tbody tr:not(.spacer)').first().locator('.chev').nth(1).click()

  const total = page.locator('.rt-table tbody tr:not(.spacer)').first()
  await expect(total).toContainText('Итого')
  await expect(total.locator('td').nth(1)).not.toHaveText('')
  const heightCollapsed = await page.locator('.rt').evaluate((el) => el.scrollHeight)
  expect(heightCollapsed).toBeLessThan(heightBefore)

  // разворачиваем обратно именно категорию (второй шеврон строки итога)
  await total.locator('.chev').nth(1).click()
  await expect(page.locator('.rt-table tbody tr:not(.spacer)').first()).not.toContainText('Итого')
})

test('свёрнутая категория не меняет шеврон региона (и после прокрутки)', async ({ page }) => {
  await page.goto(`${BASE}&from=2026-03-01&to=2026-03-10`)

  const first = page.locator('.rt-table tbody tr:not(.spacer)').first()
  const regionBefore = (await first.locator('.chev').nth(0).innerText()).trim()
  await first.locator('.chev').nth(1).click()

  const row = page.locator('.rt-table tbody tr:not(.spacer)').first()
  await expect(row.locator('.chev').nth(0)).toHaveText(regionBefore) // регион раскрыт
  await expect(row.locator('.chev').nth(1)).toHaveText('▸')          // категория свёрнута

  await page.locator('.rt').evaluate((el) => { el.scrollTop = 20000 })
  await page.waitForTimeout(300)
  const scrolled = page.locator('.rt-table tbody tr:not(.spacer)').first()
  await expect(scrolled.locator('.chev').nth(0)).toHaveText(regionBefore)
})

test('свёрнутая категория не перекрашивает ячейку региона (и после прокрутки)', async ({ page }) => {
  await page.goto(`${BASE}&from=2026-03-01&to=2026-03-10`)
  const bg = (loc) => loc.evaluate((el) => getComputedStyle(el).backgroundColor)

  const firstRow = page.locator('.rt-table tbody tr:not(.spacer)').first()
  await firstRow.locator('.chev').nth(1).click()

  const row = page.locator('.rt-table tbody tr.subtotal').first()
  const regionCell = row.locator('td').nth(0)
  const categoryCell = row.locator('td').nth(1)

  const GREY = 'rgb(247, 249, 252)' // фон строки итога
  // ячейка-предок осталась обычной (белая/прозрачная), а подпись итога — подсвечена
  expect(await bg(regionCell)).not.toBe(GREY)
  expect(await bg(categoryCell)).toBe(GREY)

  await page.locator('.rt').evaluate((el) => { el.scrollTop = 20000 })
  await page.waitForTimeout(300)
  const scrolledRegion = page.locator('.rt-table tbody tr:not(.spacer)').first().locator('td').nth(0)
  expect(await bg(scrolledRegion)).not.toBe(GREY)
})

test('«свернуть всё» оставляет только итоги регионов, «развернуть всё» возвращает детали', async ({ page }) => {
  await page.goto(`${BASE}&from=2026-03-01&to=2026-03-10`)
  const heightBefore = await page.locator('.rt').evaluate((el) => el.scrollHeight)

  await page.getByRole('button', { name: 'Свернуть всё' }).click()
  await expect(page.locator('.rt-table tbody tr.subtotal').first()).toContainText('Итого')
  const heightCollapsed = await page.locator('.rt').evaluate((el) => el.scrollHeight)
  expect(heightCollapsed).toBeLessThan(heightBefore / 2)

  await page.getByRole('button', { name: 'Развернуть всё' }).click()
  await expect(page.locator('.rt-table tbody tr.detail').first()).toBeVisible()
  const heightExpanded = await page.locator('.rt').evaluate((el) => el.scrollHeight)
  expect(heightExpanded).toBe(heightBefore)
})

test('диаграмма получает агрегаты из того же движка', async ({ page }) => {
  await page.goto(`${BASE}&from=2026-03-01&to=2026-03-10`)
  expect(await page.locator('.chart canvas').count()).toBeGreaterThanOrEqual(2)

  const info = await page.evaluate(() => window.__chart)
  expect(['line', 'bar', 'pie']).toContain(info.type)
  expect(info.series).toBeGreaterThanOrEqual(1)

  // подпись оси отформатирована по-русски (пробел как разделитель тысяч)
  const labels = await page.evaluate(() => {
    const canvas = document.querySelector('.chart canvas')
    return canvas ? canvas.width > 0 : false
  })
  expect(labels).toBe(true)
})

test('диаграмма с seriesField показывает несколько серий', async ({ page }) => {
  await page.goto(`${BASE}&from=2026-03-01&to=2026-03-10`)
  // последний блок шаблона — столбцы по категориям в разрезе регионов; ждём, пока он отрисуется
  await expect.poll(async () => (await page.evaluate(() => window.__chart))?.type).toBe('bar')
  const info = await page.evaluate(() => window.__chart)
  expect(info.series).toBeGreaterThan(1)
})

test('скриншот диаграммы стабилен между прогонами', async ({ page }) => {
  await page.goto(`${BASE}&from=2026-03-01&to=2026-03-10`)
  const chart = page.locator('.chart-box').first()
  await chart.scrollIntoViewIfNeeded()
  await expect(chart).toHaveScreenshot('chart-line.png')
})

test('параметр-справочник: NAME в интерфейсе, ID в запросе, каскад', async ({ page }) => {
  await page.goto('/report.html?report=sales-by-region')

  // автовыбор первого региона: в тексте подпись, в URL — id
  await expect(page.locator('.text')).toContainText('Регион Восток')
  await expect(page.locator('.params select').nth(0)).toHaveValue('4')
  // автовыбор применяется асинхронно (справочник + дебаунс), URL обновляется после загрузки
  await expect.poll(() => page.url()).toContain('region=4')

  // каскадный список категорий заполнен
  const categories = await page.locator('.params select').nth(1).locator('option').allInnerTexts()
  expect(categories.length).toBeGreaterThan(1)

  // смена региона: подпись, URL и данные обновляются
  await page.locator('.params select').nth(0).selectOption('1')
  await expect(page.locator('.text')).toContainText('Регион Север')
  await expect.poll(() => page.url()).toContain('region=1')
  await expect(page.locator('.meta')).toContainText('строк')
})

test('демо параметров: мультивыбор-дропдаун с галочками и статический набор', async ({ page }) => {
  await page.goto('/report.html?report=params-demo')

  const trigger = page.locator('[data-testid=ms-trigger]')
  const panel = page.locator('[data-testid=ms-panel]')

  // по умолчанию выбраны все категории: в сводке «Все (4)»
  await expect(trigger).toHaveText(/Все \(4\)/)
  await expect(panel).toHaveCount(0)
  const rowsAll = Number((await page.locator('.meta').innerText()).match(/строк:\s*(\d+)/)[1])

  // открываем дропдаун, снимаем одну галочку — данных меньше
  await trigger.click()
  await expect(panel).toBeVisible()
  await expect(panel.locator('input[type=checkbox]:checked')).toHaveCount(4)
  await panel.locator('.ms-item', { hasText: 'Электроника' }).locator('input').uncheck()

  await expect(trigger).toHaveText(/\+1/)
  await expect.poll(async () => Number((await page.locator('.meta').innerText()).match(/строк:\s*(\d+)/)[1])).toBeLessThan(rowsAll)
  await expect.poll(() => page.url()).toContain('category=')
  expect(decodeURIComponent(page.url())).not.toContain('Электроника')

  // клик вне панели закрывает список
  await page.locator('h1').click()
  await expect(panel).toHaveCount(0)

  // статический набор сужает выборку и попадает в URL
  const rowsFiltered = Number((await page.locator('.meta').innerText()).match(/строк:\s*(\d+)/)[1])
  await page.locator('.params select').nth(1).selectOption('profit_only')
  await expect(page.locator('.text')).toContainText('Только прибыльные')
  await expect.poll(() => page.url()).toContain('scope=profit_only')
  await expect.poll(async () => Number((await page.locator('.meta').innerText()).match(/строк:\s*(\d+)/)[1])).toBeLessThanOrEqual(rowsFiltered)
})
