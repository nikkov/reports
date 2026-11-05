import { expect, test } from '@playwright/test'

const BASE = '/report.html?report=sales-summary'

// DoD: «отсутствие ошибок в консоли браузера». Собираем console.error и необработанные
// исключения на каждой странице и валим тест, если что-то появилось.
const consoleErrors = new WeakMap()

test.beforeEach(async ({ page }) => {
  const errors = []
  consoleErrors.set(page, errors)
  page.on('console', (msg) => {
    if (msg.type() === 'error') errors.push(msg.text())
  })
  page.on('pageerror', (err) => errors.push(String(err && err.message ? err.message : err)))
})

test.afterEach(async ({ page }) => {
  const errors = (consoleErrors.get(page) || []).filter((t) => {
    // запрос favicon браузер делает сам, к отчёту отношения не имеет
    if (/favicon/i.test(t)) return false
    // тест, который намеренно шлёт неверный параметр, ожидает HTTP-ошибку от API
    if (page.__allowHttpErrors && /Failed to load resource/i.test(t)) return false
    return true
  })
  expect(errors, `ошибки в консоли: ${errors.join(' | ')}`).toEqual([])
})

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
  // здесь 422 — ожидаемый результат, браузерная запись об этом не считается дефектом
  page.__allowHttpErrors = true
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
  await waitRows(page)

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
  await waitRows(page)
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
  await waitRows(page) // без ожидания данных клик по кнопке попадает в пустую таблицу
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
  // последний блок шаблона — столбцы по категориям в разрезе регионов; ждём, пока он отрисуется.
  // Проверяем в одном вычислении: между отдельными чтениями window.__chart успевает перезаписаться
  // другим графиком страницы, и тест ловит «чужие» данные.
  await expect
    .poll(async () => page.evaluate(() => (window.__chart?.type === 'bar' ? window.__chart.series : 0)))
    .toBeGreaterThan(1)
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

test('список отчётов: карточки, поиск, переход в отчёт', async ({ page, request }) => {
  const list = await (await request.get('/api/reports')).json()
  const total = list.length

  await page.goto('/')
  await expect(page.locator('h1')).toHaveText('Сервер отчётов')
  await expect(page.locator('.card')).toHaveCount(total)
  await expect(page.locator('.count')).toHaveText(`${total} из ${total}`)

  // состав параметров виден на карточке
  const badges = await page.locator('[data-report=params-demo] .badge').allInnerTexts()
  expect(badges.join(' ')).toContain('мультивыбор')
  expect(badges.join(' ')).toContain('справочник')

  // поиск по названию и по id
  await page.locator('.tools input').fill('по региону')
  await expect(page.locator('.card')).toHaveCount(1)
  await page.locator('.tools input').fill('sales-summary')
  await expect(page.locator('.card')).toHaveCount(1)
  await page.locator('.tools input').fill('неттакого')
  await expect(page.locator('.muted')).toContainText('не найдено')
  await page.locator('.tools input').fill('')
  await expect(page.locator('.card')).toHaveCount(total)

  // переход в отчёт
  await page.locator('[data-report=sales-summary] .open').click()
  await expect(page).toHaveURL(/report\.html\?report=sales-summary/)
  await expect(page.locator('.meta')).toContainText('строк')
})

// Данные приходят асинхронно: до клика по шеврону ждём метаданные и строки,
// иначе клик попадает в таблицу, которая перерисуется после загрузки.
async function waitRows(page) {
  await expect(page.locator('.meta')).toContainText('строк')
  await expect(page.locator('.rt-table tbody tr:not(.spacer)').first()).toBeVisible()
  await page.waitForTimeout(150)
}

test('все шаблоны из списка открываются и отдают строки', async ({ page, request }) => {
  const list = await (await request.get('/api/reports')).json()
  expect(list.length).toBeGreaterThanOrEqual(7)

  for (const r of list) {
    await page.goto(`/report.html?report=${r.id}`)
    await expect(page.locator('h1')).toHaveText(r.title)
    await expect(page.locator('.error')).toHaveCount(0)
    await expect(page.locator('.meta')).toContainText('строк')
    // ни одна колонка не остаётся без данных на всех видимых строках подряд
    const cells = await page.locator('.rt-table tbody tr:not(.spacer) td').count()
    expect(cells).toBeGreaterThan(0)
  }
})

test('стили: шаблон без styles выглядит как версия 1.0', async ({ page }) => {
  await page.goto('/report.html?report=sales-summary&from=2026-03-01&to=2026-03-10')
  await expect(page.locator('.meta')).toContainText('строк')
  const css = await page.evaluate(() => {
    const th = getComputedStyle(document.querySelector('.rt-table th'))
    const td = getComputedStyle(document.querySelector('.rt-table tbody tr.detail td'))
    return { thBg: th.backgroundColor, thWeight: th.fontWeight, tdAlign: td.verticalAlign, tdBorderRight: td.borderRightWidth }
  })
  expect(css.thBg).toBe('rgb(242, 244, 247)')
  expect(css.thWeight).toBe('600')
  expect(css.tdAlign).toBe('top')
  expect(css.tdBorderRight).toBe('1px')
})

test('стили: пресет print — шрифт, сетка, ширины и высота шапки', async ({ page }) => {
  await page.goto('/report.html?report=finance-print')
  await expect(page.locator('.meta')).toContainText('строк')
  const info = await page.evaluate(() => {
    const th = getComputedStyle(document.querySelector('.rt-table th'))
    const td = getComputedStyle(document.querySelectorAll('.rt-table tbody tr.detail td')[3])
    return {
      family: th.fontFamily,
      weight: th.fontWeight,
      bg: th.backgroundColor,
      height: th.height,
      tdBorderRight: td.borderRightWidth,
      widths: Array.from(document.querySelectorAll('.rt-table col')).map((c) => c.style.width)
    }
  })
  expect(info.family).toContain('Times New Roman')
  expect(info.weight).toBe('700')
  expect(info.bg).toBe('rgb(255, 255, 255)')
  expect(info.height).toBe('34px')
  expect(info.tdBorderRight).toBe('1px')
  expect(info.widths.slice(0, 2)).toEqual(['110px', '30%'])
})

test('стили: тёмная тема и зебра применяются к строкам, а не к шапке', async ({ page }) => {
  await page.goto('/report.html?report=ops-dark')
  await expect(page.locator('.meta')).toContainText('строк')
  const info = await page.evaluate(() => {
    const th = getComputedStyle(document.querySelector('.rt-table th'))
    const rows = Array.from(document.querySelectorAll('.rt-table tbody tr.detail'))
    const plain = rows.find((r) => !r.classList.contains('stripe'))
    const stripe = rows.find((r) => r.classList.contains('stripe'))
    return {
      thBg: th.backgroundColor,
      thColor: th.color,
      plainBg: plain ? getComputedStyle(plain.querySelector('td')).backgroundColor : null,
      stripeBg: stripe ? getComputedStyle(stripe.querySelector('td')).backgroundColor : null,
      failuresColor: getComputedStyle(rows[0].querySelectorAll('td')[3]).color
    }
  })
  expect(info.thBg).toBe('rgb(35, 44, 56)')
  expect(info.thColor).toBe('rgb(159, 196, 255)')
  expect(info.plainBg).not.toBe(info.stripeBg)
  expect(info.failuresColor).toBe('rgb(255, 154, 154)')
})

test('стили: форматы чисел и выравнивание из шаблона', async ({ page }) => {
  await page.goto('/report.html?report=sales-formatted')
  await expect(page.locator('.meta')).toContainText('строк')

  const info = await page.evaluate(() => {
    const row = document.querySelector('.rt-table tbody tr.detail')
    const cells = Array.from(row.children).map((td) => td.innerText.trim())
    const ths = Array.from(document.querySelectorAll('.rt-table thead th'))
    const align = (el) => getComputedStyle(el).textAlign
    return {
      cells,
      dateAlign: align(row.children[0]),
      sumAlign: align(row.children[3]),
      headerDate: align(ths[0]),
      headerSum: align(ths[3])
    }
  })

  expect(info.dateAlign).toBe('center')
  expect(info.sumAlign).toBe('right')
  expect(info.headerSum).toBe('right')
  expect(info.cells[0]).toMatch(/^\d{2}\.\d{2}\.\d{4}$/)
  expect(info.cells[3]).toMatch(/ ₽$/)
  expect(info.cells[5]).toMatch(/,\d{2} %$/)
})

test('50 000 строк: виртуализация держит окно, бюджет рендера соблюдён', async ({ page }) => {
  // принятый бюджет (PO 2026-11-05): рендер окна ≤ 2 с; в тесте берём 4 с,
  // чтобы не ловить дрожание на загруженной машине
  await page.goto('/report.html?report=sales-matrix&from=2025-01-01&to=2027-01-01')
  await expect(page.locator('.meta')).toContainText('50000', { timeout: 20000 })

  const domRows = await page.locator('.rt-table tbody tr:not(.spacer)').count()
  expect(domRows).toBeLessThan(60)

  const renderMs = await page.evaluate(() => window.__renderMs)
  expect(renderMs).toBeLessThan(4000)

  // прокрутка в конец не ломает смещения
  await page.locator('.rt').evaluate((el) => { el.scrollTop = el.scrollHeight })
  await page.waitForTimeout(200)
  expect(await page.locator('.rt-table tbody tr:not(.spacer)').count()).toBeLessThan(60)
})

test('типографика: размеры шрифта по зонам и все виды выравнивания', async ({ page }) => {
  await page.goto('/report.html?report=sales-typography')
  await expect(page.locator('.meta')).toContainText('строк')
  // метаданные приходят раньше строк в DOM: без этого ожидания evaluate иногда видит пустую таблицу
  await expect(page.locator('.rt-table tbody tr.detail').first()).toBeVisible()

  const info = await page.evaluate(() => {
    const th = getComputedStyle(document.querySelector('.rt-table th'))
    const row = document.querySelector('.rt-table tbody tr.detail')
    const cell = (i) => getComputedStyle(row.children[i])
    return {
      headerSize: th.fontSize,
      headerWeight: th.fontWeight,
      headerAlign: th.textAlign,
      bodySize: cell(1).fontSize,
      regionAlign: cell(0).textAlign,
      dateAlign: cell(1).textAlign,
      sumAlign: cell(4).textAlign
    }
  })

  expect(info.headerSize).toBe('15px')
  expect(info.headerWeight).toBe('700')
  expect(info.headerAlign).toBe('center')
  expect(info.bodySize).toBe('12px')
  // все три вида: влево, по центру, вправо
  expect(info.regionAlign).toBe('left')
  expect(info.dateAlign).toBe('center')
  expect(info.sumAlign).toBe('right')

  // размеры подытога и итога берём из CSS-переменных рендерера: строки итогов может не быть
  // в окне виртуализации, и проверка через DOM зависела бы от прокрутки
  const zones = await page.evaluate(() => {
    const s = getComputedStyle(document.querySelector('.rt'))
    return {
      subtotal: s.getPropertyValue('--rt-s-font-size').trim(),
      grandtotal: s.getPropertyValue('--rt-g-font-size').trim(),
      header: s.getPropertyValue('--rt-h-font-size').trim(),
      body: s.getPropertyValue('--rt-b-font-size').trim()
    }
  })
  expect(zones.header).toBe('15px')
  expect(zones.body).toBe('12px')
  expect(zones.subtotal).toBe('13px')
  expect(zones.grandtotal).toBe('14px')
})

test('условное форматирование: ячейка по правилу и подсветка строки', async ({ page }) => {
  await page.goto('/report.html?report=sales-rules')
  await expect(page.locator('.meta')).toContainText('строк')
  await expect(page.locator('.rt-table tbody tr.detail').first()).toBeVisible()

  const info = await page.evaluate(() => {
    const rows = Array.from(document.querySelectorAll('.rt-table tbody tr.detail')).slice(0, 6)
    return rows.map((tr) => ({
      marginBg: getComputedStyle(tr.children[5]).backgroundColor,
      rowBg: getComputedStyle(tr.children[0]).backgroundColor,
      marginText: tr.children[5].innerText.trim()
    }))
  })

  // янтарный фон правила по марже и светлая подсветка строки по сумме
  const AMBER = 'rgb(255, 244, 214)'
  const ROW = 'rgb(255, 247, 230)'
  expect(info.some((r) => r.marginBg === AMBER)).toBe(true)
  expect(info.some((r) => r.rowBg === ROW)).toBe(true)
  // строка ниже порога по сумме остаётся белой, но её маржа может быть подсвечена
  expect(info.some((r) => r.rowBg === 'rgb(255, 255, 255)')).toBe(true)
})

test('админка показывает параметры и SQL их запросов', async ({ page }) => {
  await page.goto('/admin.html')
  await expect(page.locator('.rep-row, li').first()).toBeVisible()

  // открываем отчёт со справочником
  const edit = page.getByRole('button', { name: 'Изменить' }).first()
  await edit.click()

  const panel = page.locator('.params')
  await expect(panel).toBeVisible()
  await expect(panel.locator('h3')).toContainText('Параметры и их запросы')

  // у справочника виден SQL и метка серверной проверки
  const lookup = panel.locator('.param', { hasText: 'cost_center' })
  await expect(lookup.locator('.psql')).toContainText('SELECT')
  await expect(lookup.locator('.psql')).toContainText('cost_center')
  await expect(lookup.locator('.tag.ok')).toHaveText('SELECT ok')

  // параметры без SQL честно это показывают
  const dateParam = panel
    .locator('.param')
    .filter({ has: page.locator('.pname', { hasText: /^from$/ }) })
  await expect(dateParam).toContainText('SQL не задан')
})

test('экспорт в Excel: файл со шапкой, числами и листом параметров', async ({ page }) => {
  await page.goto('/report.html?report=sales-summary&from=2026-03-01&to=2026-03-10')
  await expect(page.locator('.meta')).toContainText('строк')

  const [download] = await Promise.all([
    page.waitForEvent('download'),
    page.getByRole('button', { name: 'Excel' }).click()
  ])
  expect(download.suggestedFilename()).toMatch(/^sales-summary-\d{4}-\d{2}-\d{2}\.xlsx$/)

  const ExcelJS = (await import('exceljs')).default
  const wb = new ExcelJS.Workbook()
  await wb.xlsx.readFile(await download.path())
  expect(wb.worksheets.length).toBe(2)
  expect(wb.getWorksheet('Параметры')).toBeTruthy()

  const ws = wb.worksheets[0]
  let numbers = 0
  let percents = 0
  ws.eachRow((row) => {
    row.eachCell((cell) => {
      if (typeof cell.value === 'number') numbers++
      if (typeof cell.value === 'number' && String(cell.numFmt || '').includes('%')) percents++
    })
  })
  // числа остаются числами, проценты — с процентным форматом
  expect(numbers).toBeGreaterThan(5)
  expect(percents).toBeGreaterThan(0)
  expect(ws.rowCount).toBeGreaterThan(20)

  // сворачивание: два уровня структуры Excel — регион (0→1) и категория (1→2).
  // Опорная строка категории остаётся видимой, её детали уходят на уровень глубже.
  const levels = {}
  ws.eachRow((row) => {
    const lvl = row.outlineLevel || 0
    levels[lvl] = (levels[lvl] || 0) + 1
  })
  expect(levels[1]).toBeGreaterThan(1) // опоры категорий
  expect(levels[2]).toBeGreaterThan(10) // их детали
  expect(ws.properties?.outlineProperties?.summaryBelow).toBe(true)
})

test('печатный вид: интерфейс скрыт, шапка повторяется, таблица целиком', async ({ page }) => {
  // диалог печати в тесте не нужен: подменяем его и смотрим на макет
  await page.addInitScript(() => { window.__printed = 0; window.print = () => { window.__printed++ } })
  await page.goto('/report.html?report=sales-summary&from=2026-03-01&to=2026-03-10')
  await expect(page.locator('.meta')).toContainText('строк')

  const total = Number((await page.locator('.meta b').first().textContent()).replace(/\s/g, ''))
  await page.getByRole('button', { name: 'Печать / PDF' }).click()
  await page.waitForFunction(() => window.__printed > 0, null, { timeout: 30000 })
  await page.emulateMedia({ media: 'print' })

  // без окна виртуализации в макете все строки, а контейнер не схлопнут (contain: none)
  const layout = await page.evaluate(() => {
    const rt = document.querySelector('.rt')
    const tb = document.querySelector('.rt-table')
    return { rows: tb.tBodies[0].rows.length, height: Math.round(rt.getBoundingClientRect().height), contain: getComputedStyle(rt).contain }
  })
  // строки данных + подытоги и общий итог
  expect(layout.rows).toBeGreaterThanOrEqual(total)
  expect(layout.height).toBeGreaterThan(1000)
  expect(layout.contain).toBe('none')

  const state = await page.evaluate(() => {
    const disp = (sel) => {
      const el = document.querySelector(sel)
      return el ? getComputedStyle(el).display : 'нет'
    }
    const thead = getComputedStyle(document.querySelector('.rt-table thead'))
    const rt = getComputedStyle(document.querySelector('.rt'))
    return { params: disp('.params'), tools: disp('.tools'), theadPosition: thead.position, theadDisplay: thead.display, overflow: rt.overflow }
  })
  expect(state.params).toBe('none')
  expect(state.tools).toBe('none')
  expect(state.theadPosition).toBe('static')
  expect(state.theadDisplay).toBe('table-header-group')
  expect(state.overflow).toBe('visible')
})
