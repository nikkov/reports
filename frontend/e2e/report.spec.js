import { expect, request as apiRequest, test } from '@playwright/test'

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
  // число блоков диаграмм задаётся шаблоном — проверяем наличие, а не магическое число.
  // Ждём отрисовку: canvas появляется после загрузки данных и инициализации ECharts.
  await expect(page.locator('.chart canvas').nth(1)).toBeVisible()
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
  // Первая строка окна может быть частично скрыта шапкой — её групповая ячейка
  // прозрачная (держит выравнивание колонок), а шеврон стоит на первой видимой строке.
  const scrolled = page.locator('.rt-table tbody tr:not(.spacer)').filter({ has: page.locator('.chev') }).first()
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

// Дефект стенда: у отчётов с объединёнными группами при прокрутке между липкой шапкой
// и первой строкой оставалась пустая полоса — окно начиналось со следующей строки.
test('при прокрутке под шапкой нет пустой полосы, а подпись группы видна', async ({ page }) => {
  await page.goto(`${BASE}&from=2026-03-01&to=2026-03-10`)
  await waitRows(page)

  for (const top of [200, 1200, 6000]) {
    await page.locator('.rt').evaluate((el, y) => {
      el.scrollTop = y
      el.dispatchEvent(new Event('scroll'))
    }, top)
    await page.waitForTimeout(250)

    const measured = await page.evaluate(() => {
      const sc = document.querySelector('.rt')
      const head = sc.querySelector('thead')
      const headerBottom = head.getBoundingClientRect().bottom
      const rows = [...sc.querySelectorAll('tbody tr')].filter((r) => !r.classList.contains('spacer'))
      const painted = rows.filter((r) => r.getBoundingClientRect().bottom > headerBottom + 0.5)
      const visible = rows.find((r) => r.getBoundingClientRect().top >= headerBottom - 0.5)
      return {
        gap: painted[0] ? painted[0].getBoundingClientRect().top - headerBottom : null,
        label: visible ? (visible.children[0].textContent || '').trim() : ''
      }
    })

    // строка, попавшая под шапку, должна быть видна хотя бы частично — иначе полоса пустая
    expect(measured.gap, `пустая полоса под шапкой при scrollTop=${top}`).not.toBeNull()
    expect(measured.gap, `пустая полоса под шапкой при scrollTop=${top}`).toBeLessThanOrEqual(1)
    // первая полноценно видимая строка обязана показывать подпись группы
    expect(measured.label, `групповая колонка пуста при scrollTop=${top}`).not.toBe('')
  }
})

// Дефект стенда: ширины колонок считались по строкам, попавшим в окно виртуализации, —
// при появлении строки итога («Итого …») колонка скачком расширялась, а при уходе сужалась.
test('ширины колонок не меняются при прокрутке и не выезжают за контейнер', async ({ page }) => {
  await page.goto(`${BASE}&from=2026-03-01&to=2026-03-10`)
  await waitRows(page)

  const measure = () =>
    page.evaluate(() => {
      const sc = document.querySelector('.rt')
      const table = sc.querySelector('table')
      return {
        cols: [...table.querySelectorAll('colgroup col')].map((c) => Math.round(c.getBoundingClientRect().width)),
        tableW: Math.round(table.getBoundingClientRect().width),
        clientW: sc.clientWidth,
        scrollW: sc.scrollWidth,
        subtotals: sc.querySelectorAll('tbody tr.subtotal, tbody tr.grandtotal').length
      }
    })

  const start = await measure()
  const seen = new Set([start.cols.join('/')])
  let sawSubtotal = start.subtotals > 0

  // Идём вниз до первой строки итога в окне: до неё ширины детальных строк, после —
  // они же плюс широкая подпись «Итого …», из-за которой колонка и «дышала».
  const positions = []
  for (let top = 1000; top <= 24000 && !sawSubtotal; top += 1000) positions.push(top)
  positions.push(
    ...positions.slice(-3).map((top) => top + 500),
    ...positions.slice(-2).map((top) => top + 1200)
  )
  for (const top of positions) {
    await page.locator('.rt').evaluate((el, y) => {
      el.scrollTop = y
      el.dispatchEvent(new Event('scroll'))
    }, top)
    await page.waitForTimeout(120)
    const m = await measure()
    seen.add(m.cols.join('/'))
    sawSubtotal = sawSubtotal || m.subtotals > 0
    expect(m.scrollW, `таблица выехала за контейнер при scrollTop=${top}`).toBeLessThanOrEqual(m.clientW + 1)
  }

  expect(sawSubtotal, 'в окно так и не попала строка итога — тест ничего не проверил').toBe(true)
  expect([...seen], 'ширины колонок менялись при прокрутке').toHaveLength(1)
})

// Дефект стенда: разрез объединения рисовал границу — в групповой колонке появлялась
// линия на месте разреза, хотя группа продолжается.
test('на разрезе объединения нет линии, групповые ячейки её не рисуют', async ({ page }) => {
  await page.goto(`${BASE}&from=2026-03-01&to=2026-03-10`)
  await waitRows(page)
  await page.locator('.rt').evaluate((el) => {
    el.scrollTop = 2000
    el.dispatchEvent(new Event('scroll'))
  })
  await page.waitForTimeout(250)

  const result = await page.evaluate(() => {
    const sc = document.querySelector('.rt')
    const phantom = [...sc.querySelectorAll('tbody td.phantom')]
    return {
      phantoms: phantom.length,
      borders: phantom.map((td) => getComputedStyle(td).borderBottomColor),
      empty: phantom.every((td) => !td.textContent.trim())
    }
  })
  // разрез объединения обязателен: строка выше окна остаётся прозрачной ячейкой
  expect(result.phantoms).toBeGreaterThan(0)
  expect(result.empty).toBe(true)
  for (const color of result.borders) {
    expect(['rgba(0, 0, 0, 0)', 'transparent'], `линия на разрезе объединения: ${color}`).toContain(color)
  }
})

test('«свернуть всё» после прокрутки не оставляет пустоты и показывает итоги', async ({ page }) => {
  await page.goto(`${BASE}&from=2026-03-01&to=2026-03-10`)
  await waitRows(page)

  await page.locator('.rt').evaluate((el) => {
    el.scrollTop = 9000
    el.dispatchEvent(new Event('scroll'))
  })
  await page.waitForTimeout(250)
  await page.getByRole('button', { name: 'Свернуть всё' }).click()

  await expect(page.locator('.rt-table tbody tr.subtotal').first()).toContainText('Итого')
  const measured = await page.evaluate(() => {
    const sc = document.querySelector('.rt')
    const headerBottom = sc.querySelector('thead').getBoundingClientRect().bottom
    const rows = [...sc.querySelectorAll('tbody tr')].filter((r) => !r.classList.contains('spacer'))
    return {
      scrollTop: sc.scrollTop,
      gap: rows[0] ? rows[0].getBoundingClientRect().top - headerBottom : null,
      kinds: rows.map((r) => r.className.split(' ')[0])
    }
  })
  // модель схлопнулась — окно обязано уехать к началу, иначе показывается хвост таблицы
  expect(measured.scrollTop).toBe(0)
  expect(measured.gap).toBeLessThanOrEqual(1)
  expect(measured.kinds.every((k) => k === 'subtotal' || k === 'grandtotal')).toBe(true)
})

// Дефект стенда: в отчёте «Продажи: агрегация на сервере» группы раскрывались «в пустоту» —
// сервер отдаёт одну строку на категорию, деталей в модели нет. Теперь раскрытие подгружает
// строки группы с сервера, а итог группы остаётся серверным (ADR-65).
test('раскрытие группы серверной агрегации подгружает детальные строки', async ({ page }) => {
  await page.goto('/report.html?report=sales-aggregate&from=2026-03-01&to=2026-03-10')
  await waitRows(page)
  // ждём именно агрегированную витрину: 5 групп с шевронами
  await expect(page.locator('.rt-table tbody tr .chev')).toHaveCount(5)

  const detailCount = () =>
    page.evaluate(() => {
      const sc = document.querySelector('.rt')
      const ids = [...sc.querySelectorAll('tbody tr.detail')]
      return { window: ids.length, height: sc.scrollHeight }
    })

  const collapsed = await detailCount()
  // Группы серверной агрегации стартуют СВЁРНУТЫМИ: строк в браузере нет, пока их не
  // подгрузили, и шеврон «▾ раскрыто» при пустой группе — обещание без данных.
  expect(collapsed.window).toBe(0)
  expect(collapsed.height).toBeLessThan(400) // 5 строк-итогов, а не 5 агрегатов
  const chevrons = await page.$$eval('.rt-table tbody tr .chev', (els) => els.map((e) => e.textContent.trim()))
  expect(chevrons).toEqual(['▸', '▸', '▸', '▸', '▸'])

  // один клик по ▸ и подгружает детали, и раскрывает группу
  await page.locator('.rt-table tbody tr .chev').first().click()

  // детали пришли с сервера: высота таблицы выросла в разы, строки перестали быть агрегатами
  await expect
    .poll(async () => (await detailCount()).height, { timeout: 15000 })
    .toBeGreaterThan(collapsed.height * 3)

  const amounts = await page.evaluate(() => {
    const sc = document.querySelector('.rt')
    return [...sc.querySelectorAll('tbody tr.detail')]
      .map((tr) => (tr.children[1]?.textContent || '').trim())
      .filter(Boolean)
  })
  expect(amounts.length).toBeGreaterThan(5)

  // итог группы — серверный, общий итог не пересчитан по загруженной части
  await page.locator('.rt').evaluate((el) => {
    el.scrollTop = 17600
    el.dispatchEvent(new Event('scroll'))
  })
  await page.waitForTimeout(300)
  const sums = await page.evaluate(() => {
    const sc = document.querySelector('.rt')
    const subtotal = sc.querySelector('tbody tr.subtotal')
    const grand = [...sc.querySelectorAll('tbody tr.grandtotal')].pop()
    // суммы приходят с неразрывными пробелами — сравниваем через обычные
    const norm = (s) => s.replace(/[\s\u00a0\u202f]+/g, ' ').trim()
    const cells = (tr) => (tr ? [...tr.children].map((c) => norm(c.textContent)) : [])
    return { subtotal: cells(subtotal), grand: cells(grand) }
  })
  expect(sums.subtotal[0]).toContain('Итого')
  expect(sums.subtotal[1]).toBe('37 634 100')
  expect(sums.grand[1]).toBe('150 977 000')
})

test('диаграмма получает агрегаты из того же движка', async ({ page }) => {
  await page.goto(`${BASE}&from=2026-03-01&to=2026-03-10`)
  // ждём именно второй canvas: диаграммы рисуются после загрузки данных
  await expect(page.locator('.chart canvas').nth(1)).toBeVisible()

  // данные приходят асинхронно: ждём, пока у диаграммы появятся серии, а не читаем один раз
  const info = await page.evaluate(() => window.__chart)
  expect(['line', 'bar', 'pie']).toContain(info.type)
  await expect
    .poll(() => page.evaluate(() => window.__chart?.instance?.getOption?.().series?.length ?? 0), {
      timeout: 5000
    })
    .toBeGreaterThanOrEqual(1)

  // canvas получает реальный размер после раскладки: ждём, а не проверяем один раз
  await expect
    .poll(() => page.evaluate(() => (window.__chart?.instance?.getWidth?.() ?? 0) > 0), { timeout: 5000 })
    .toBe(true)
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
  // каталог отдаёт страницы по 10 (ADR-41)
  await expect(page.locator('.card')).toHaveCount(Math.min(10, total))
  await expect(page.locator('.count')).toHaveText(`${Math.min(10, total)} из ${total}`)
  if (total > 10) {
    await page.getByRole('button', { name: /Показать ещё/ }).click()
    await expect(page.locator('.card')).toHaveCount(total)
  }

  // состав параметров виден на карточке
  const badges = await page.locator('[data-report=params-demo] .badge').allInnerTexts()
  expect(badges.join(' ')).toContain('мультивыбор')
  expect(badges.join(' ')).toContain('справочник')

  // поиск по названию и по id
  await page.locator('.tools input').fill('центрам затрат')
  await expect(page.locator('.card')).toHaveCount(1)
  await page.locator('.tools input').fill('sales-summary')
  await expect(page.locator('.card')).toHaveCount(1)
  await page.locator('.tools input').fill('неттакого')
  await expect(page.locator('.muted')).toContainText('не найдено')
  await page.locator('.tools input').fill('')
  await expect(page.locator('.card')).toHaveCount(Math.min(10, total))
  if (total > 10) {
    await page.getByRole('button', { name: /Показать ещё/ }).click()
    await expect(page.locator('.card')).toHaveCount(total)
  }

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
    // Отчёт считается живым, если показал либо строки таблицы, либо диаграмму:
    // у отчёта из одних диаграмм таблицы может не быть вовсе (sales-flow, ADR-56).
    const cells = await page.locator('.rt-table tbody tr:not(.spacer) td').count()
    const charts = await page.locator('.chart-box canvas').count()
    expect(cells + charts).toBeGreaterThan(0)
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

test('печатный вид: таблица сразу под заголовком, шапка повторяется, объединения сохранены', async ({ page, context }) => {
  // печать идёт в этой же вкладке: подменяем диалог и смотрим печатный слой
  await context.addInitScript(() => { window.__printed = 0; window.print = () => { window.__printed++ } })
  await page.goto('/report.html?report=sales-summary&from=2026-03-01&to=2026-03-10')
  await expect(page.locator('.meta')).toContainText('строк')
  const total = Number((await page.locator('.meta b').first().textContent()).replace(/\s/g, ''))
  const pagesBefore = context.pages().length

  await page.getByRole('button', { name: 'Печать / PDF' }).click()
  await page.waitForFunction(() => window.__printed > 0, null, { timeout: 30000 })
  await page.emulateMedia({ media: 'print' })

  const doc = await page.evaluate(() => {
    const layer = document.querySelector('.rt-print')
    const tables = Array.from(layer.querySelectorAll('table'))
    const h1 = document.querySelector('h1').getBoundingClientRect()
    const first = tables[0].getBoundingClientRect()
    const spans = Array.from(layer.querySelectorAll('td[rowspan]'))
    return {
      screen: getComputedStyle(document.querySelector('.rt-screen')).display,
      tables: tables.length,
      rows: tables.reduce((sum, t) => sum + t.tBodies[0].rows.length, 0),
      top: Math.round(first.top),
      gap: Math.round(first.top - h1.bottom),
      spans: spans.length,
      maxSpan: Math.max(...spans.map((td) => td.rowSpan)),
      chevrons: layer.querySelectorAll('.chev').length
    }
  })

  // без окон: печать в текущей вкладке
  expect(context.pages().length).toBe(pagesBefore)
  expect(doc.screen).toBe('none') // экранная таблица скрыта
  expect(doc.tables).toBeGreaterThan(1) // страницы печати
  expect(doc.rows).toBeGreaterThanOrEqual(total)
  expect(doc.top).toBeLessThan(140) // таблица сразу под заголовком
  expect(doc.gap).toBeLessThan(60)
  expect(doc.spans).toBeGreaterThan(10) // объединения сохранены
  expect(doc.maxSpan).toBeLessThan(40) // и обрезаны по границе страницы
  expect(doc.chevrons).toBe(0) // «▾» на бумаге не нужен

  const pdf = await page.pdf({ format: 'A4', landscape: true })
  expect(pdf.length).toBeGreaterThan(10000)
})

test('drill-through: подытог группы открывает детальный отчёт с подставленными параметрами', async ({ page }) => {
  await page.goto('/report.html?report=sales-summary&from=2026-03-01&to=2026-03-10')
  await expect(page.locator('.meta')).toContainText('строк')

  // переход доступен прямо с ячейки группы в первой строке
  const link = page.locator('a.drill').first()
  await expect(link).toBeVisible()
  const label = (await link.textContent()).replace(/\s*→\s*$/, '').trim()

  await Promise.all([page.waitForURL(/report=sales-drill/), link.click()])
  const url = new URL(page.url())
  expect(url.searchParams.get('report')).toBe('sales-drill')
  expect(url.searchParams.get('from')).toBe('2026-03-01')
  expect(url.searchParams.get('to')).toBe('2026-03-10')
  // группа подставлена в параметр: регион и категория не пустые
  expect(url.searchParams.get('region')).toBeTruthy()
  expect(url.searchParams.get('category')).toBeTruthy()
  // ссылка — ячейка группы: её текст совпадает с подставленным регионом
  expect(label).toBe(url.searchParams.get('region'))

  await expect(page.locator('.rt-table')).toBeVisible()
  await expect(page.locator('.meta')).toContainText('строк')
})

test('каталог: папки, теги и избранное', async ({ page, request }) => {
  const total = (await (await request.get('/api/reports')).json()).length
  await page.goto('/')
  // каталог отдаёт страницы по 10: сначала первая, остальное — по кнопке
  await expect(page.locator('.card')).toHaveCount(Math.min(10, total))
  if (total > 10) {
    await page.getByRole('button', { name: /Показать ещё/ }).click()
    await expect(page.locator('.card')).toHaveCount(total)
  }
  await expect(page.locator('.folder', { hasText: 'Финансы' })).toBeVisible()

  // папка
  await page.locator('.folder', { hasText: 'Финансы' }).click()
  await expect(page.locator('.card')).toHaveCount(2)
  for (const folder of await page.locator('.card').evaluateAll((els) => els.map((e) => e.dataset.folder))) {
    expect(folder).toBe('Финансы')
  }

  // тег внутри папки
  await page.locator('.tag', { hasText: 'печать' }).click()
  await expect(page.locator('.card')).toHaveCount(1)
  await page.locator('.tag', { hasText: 'печать' }).click()

  // возврат ко всем и поиск по описанию/тегу
  await page.locator('.folder', { hasText: 'Все отчёты' }).click()
  await page.locator('input[type=search]').fill('SLA')
  await expect(page.locator('.card')).toHaveCount(1)

  // избранное сохраняется между перезагрузками
  await page.locator('input[type=search]').fill('')
  await page.locator('.card').first().locator('.star').click()
  await expect(page.locator('.folder', { hasText: 'Избранное' })).toContainText('1')
  await page.reload()
  await page.locator('.folder', { hasText: 'Избранное' }).click()
  await expect(page.locator('.card')).toHaveCount(1)
})

test('публикация: правка живёт в черновике и попадает в отчёт только после публикации', async ({ page, request }) => {
  const id = 'draft-e2e'
  const base = {
    version: 1,
    id,
    title: 'Черновик e2e',
    params: [],
    datasets: { s: { sql: 'SELECT 1 AS x' } },
    blocks: [{ type: 'table', dataKey: 's', columns: [{ field: 'x', title: 'X' }] }]
  }
  await request.delete(`/api/reports/${id}`)
  const created = await request.post('/api/reports', { data: { id, body: base } })
  expect(created.ok()).toBeTruthy()

  // черновик с новым заголовком
  const draft = await request.put(`/api/admin/reports/${id}/draft`, { data: { body: { ...base, title: 'Черновик e2e (правка)' } } })
  expect(draft.ok()).toBeTruthy()

  // публичный отчёт не изменился
  const before = await (await request.get(`/api/reports/${id}`)).json()
  expect(before.title).toBe('Черновик e2e')

  // админка открывает черновик и показывает его статус
  await page.goto('/admin.html')
  await page.locator('.search').fill(id)
  await page.locator(`[data-report=${id}] .report-main`).click()
  await expect(page.locator('.draft-chip')).toBeVisible()
  await expect(page.locator('.draft-chip')).toContainText('черновик')
  await expect(page.locator('.cm-content')).toContainText('Черновик e2e (правка)')

  // публикация из админки
  await page.getByRole('button', { name: 'Опубликовать' }).click()
  await expect(page.locator('.status')).toContainText('опубликовано')
  await expect(page.locator('.draft-chip')).toHaveCount(0)

  const after = await (await request.get(`/api/reports/${id}`)).json()
  expect(after.title).toBe('Черновик e2e (правка)')

  // отчёт открывается с новой версией и данными
  await page.goto(`/report.html?report=${id}`)
  await expect(page.locator('h1')).toHaveText('Черновик e2e (правка)')

  await request.delete(`/api/reports/${id}`)
})

test('кэш результатов: повтор из кэша, 304, сброс после публикации', async ({ request }) => {
  // отдельный отчёт, чтобы не плодить снапшоты в истории демонстрационных шаблонов
  const id = 'cache-e2e'
  const body = {
    version: 1,
    id,
    title: 'Кэш e2e',
    params: [],
    datasets: { s: { sql: 'SELECT 1 AS x' } },
    blocks: [{ type: 'table', dataKey: 's', columns: [{ field: 'x', title: 'X' }] }]
  }
  await request.delete(`/api/reports/${id}`)
  expect((await request.post('/api/reports', { data: { id, body } })).ok()).toBeTruthy()

  const data = { reportId: id, params: {} }

  const first = await request.post('/api/report/data', { data })
  expect(first.ok()).toBeTruthy()
  const etag = first.headers()['etag']
  expect(etag).toBeTruthy()

  const second = await request.post('/api/report/data', { data })
  expect(second.headers()['x-cache']).toBe('hit')
  expect(await second.text()).toBe(await first.text())

  const notModified = await request.post('/api/report/data', {
    data,
    headers: { 'If-None-Match': etag }
  })
  expect(notModified.status()).toBe(304)

  // публикация поднимает версию шаблонов → прежние результаты больше не отдаются
  await request.put(`/api/admin/reports/${id}/draft`, { data: { body: { ...body, title: 'Кэш e2e 2' } } })
  await request.post(`/api/admin/reports/${id}/publish`)

  const afterPublish = await request.post('/api/report/data', { data })
  expect(afterPublish.headers()['x-cache']).toBe('miss')
  expect(afterPublish.headers()['etag']).not.toBe(etag)

  await request.delete(`/api/reports/${id}`)
})

test('мультинаборность: блоки читают разные наборы одного шаблона', async ({ page, request }) => {
  // публичный шаблон не должен отдавать SQL ни одного набора
  const tpl = await (await request.get('/api/reports/sales-two-sets')).json()
  expect(Object.keys(tpl.datasets).sort()).toEqual(['byCategory', 'byRegion'])
  for (const ds of Object.values(tpl.datasets)) expect(ds.sql).toBeUndefined()

  await page.goto('/report.html?report=sales-two-sets&from=2026-03-01&to=2026-03-10')
  await expect(page.locator('.meta')).toContainText('строк')
  await expect(page.locator('.rt-table')).toHaveCount(2)

  const tables = await page.locator('.rt-table').evaluateAll((els) =>
    els.map((t) => ({
      head: Array.from(t.querySelectorAll('thead th')).map((th) => th.textContent.trim()),
      first: t.tBodies[0].rows[0] ? t.tBodies[0].rows[0].cells[0].textContent.trim() : ''
    }))
  )
  // первый блок — регионы, второй — категории: данные разные
  expect(tables[0].head).toContain('Регион')
  expect(tables[1].head).toContain('Категория')
  expect(tables[0].first).not.toBe(tables[1].first)

  // короткий отчёт не оставляет пустого поля под последней строкой
  const boxes = await page.locator('.rt-table').evaluateAll((els) =>
    els.map((t) => ({
      container: Math.round(t.closest('.rt').getBoundingClientRect().height),
      content: Math.round(t.getBoundingClientRect().height)
    }))
  )
  for (const box of boxes) expect(box.container - box.content).toBeLessThan(60)

  // набор можно запросить явно, неизвестный — 422
  const one = await request.post('/api/report/data', { data: { reportId: 'sales-two-sets', dataset: 'byRegion', params: { from: '2026-03-01', to: '2026-03-10' } } })
  expect(one.ok()).toBeTruthy()
  expect((await one.json()).columns[0].name).toBe('region')

  const bad = await request.post('/api/report/data', { data: { reportId: 'sales-two-sets', dataset: 'nope', params: { from: '2026-03-01', to: '2026-03-10' } } })
  expect(bad.status()).toBe(422)
  expect((await bad.json()).error.code).toBe('DATASET_NOT_FOUND')
})

test('серверная агрегация: GROUP BY в базе, на экране 6 строк вместо 2 746', async ({ page, request }) => {
  const params = { from: '2026-03-01', to: '2026-03-10' }
  const spec = {
    reportId: 'sales-aggregate',
    dataset: 'sales',
    params,
    groupBy: ['category'],
    measures: [{ field: 'amount', agg: 'sum' }, { field: 'profit', agg: 'sum' }]
  }

  const api = await request.post('/api/report/aggregate', { data: spec })
  expect(api.ok()).toBeTruthy()
  const payload = await api.json()
  expect(payload.rowCount).toBe(5) // 4 категории + пустая
  const amount = payload.rows.reduce((sum, r) => sum + Number(r.amount), 0)
  const profit = payload.rows.reduce((sum, r) => sum + Number(r.profit), 0)
  // сверка с эталоном golden: суммы по всей выборке
  expect(amount).toBe(150977000)
  expect(profit).toBe(31013250)

  // инъекция в именах колонок отклоняется до SQL
  const bad = await request.post('/api/report/aggregate', {
    data: { ...spec, groupBy: ['category; DROP TABLE sales'] }
  })
  expect(bad.status()).toBe(422)
  expect((await bad.json()).error.code).toBe('INVALID_PARAMS')

  // страница показывает 5 групп + ИТОГО
  await page.goto('/report.html?report=sales-aggregate&from=2026-03-01&to=2026-03-10')
  await expect(page.locator('.meta')).toContainText('строк')
  await expect(page.locator('.rt-table tbody tr')).toHaveCount(6)
})

test('серверный экспорт .xlsx: файл совпадает с экраном и содержит параметры', async ({ request }) => {
  const res = await request.post('/api/report/export.xlsx', {
    data: { reportId: 'sales-aggregate', params: { from: '2026-03-01', to: '2026-03-10' } }
  })
  expect(res.ok()).toBeTruthy()
  expect(res.headers()['content-type']).toContain('spreadsheetml.sheet')

  const ExcelJS = (await import('exceljs')).default
  const wb = new ExcelJS.Workbook()
  await wb.xlsx.load(await res.body())

  // лист с данными и лист параметров
  expect(wb.worksheets.length).toBe(2)
  const ws = wb.worksheets[0]
  expect(String(ws.getRow(1).getCell(1).value)).toBe('Категория')

  // экспорт учитывает агрегацию блока: 5 групп + ИТОГО + шапка, а не 2 746 детальных строк
  expect(ws.rowCount).toBe(7)
  expect(String(ws.getRow(7).getCell(1).value)).toBe('ИТОГО')
  const amounts = []
  ws.eachRow((row, n) => {
    // ИТОГО (последняя строка) в сумму групп не входит — иначе она считается дважды
    if (n > 1 && n < ws.rowCount && typeof row.getCell(2).value === 'number') amounts.push(row.getCell(2).value)
  })
  expect(amounts.length).toBeGreaterThanOrEqual(4)
  expect(amounts.reduce((a, b) => a + b, 0)).toBe(150977000) // сумма совпадает с экраном и golden
  expect(ws.getRow(ws.rowCount).getCell(2).value).toBe(150977000) // ИТОГО равно сумме групп

  const params = wb.getWorksheet('Параметры')
  expect(params).toBeTruthy()
  const names = []
  params.eachRow((row) => names.push(String(row.getCell(1).value || '')))
  expect(names).toContain('from')
  expect(names).toContain('to')

  // неизвестный набор — 422
  const bad = await request.post('/api/report/export.xlsx', {
    data: { reportId: 'sales-aggregate', dataset: 'nope', params: {} }
  })
  expect(bad.status()).toBe(422)
})

// ADR-66: серверный .xlsx долго выгружал плоскую таблицу — подытоги и ИТОГО были только
// в браузерном экспорте. Теперь книгу собирает один лист: подытог на уровне из шаблона,
// общий итог последней строкой, а CSV остаётся машинным (без строк итогов).
test('серверный экспорт .xlsx: подытоги групп и ИТОГО, CSV без них', async ({ request }) => {
  const params = { from: '2026-03-01', to: '2026-03-10' }
  const res = await request.post('/api/report/export.xlsx', { data: { reportId: 'sales-summary', params } })
  expect(res.ok()).toBeTruthy()

  const ExcelJS = (await import('exceljs')).default
  const wb = new ExcelJS.Workbook()
  await wb.xlsx.load(await res.body())
  const ws = wb.worksheets[0]

  // 2 748 детальных строк + 6 подытогов (5 регионов и (пусто)) + ИТОГО + шапка
  expect(ws.rowCount).toBe(2756)

  const summary = []
  ws.eachRow((row, n) => {
    const first = String(row.getCell(1).value ?? '')
    if (n > 1 && (first.startsWith('Итого') || first === 'ИТОГО')) {
      summary.push({
        n,
        first,
        amount: row.getCell(4).value,
        profit: row.getCell(5).value,
        bold: Boolean(row.getCell(1).font && row.getCell(1).font.bold)
      })
    }
  })
  expect(summary.map((s) => s.first)).toEqual([
    'Итого Восток',
    'Итого Запад',
    'Итого Север',
    'Итого Центр',
    'Итого Юг',
    'Итого (пусто)',
    'ИТОГО'
  ])
  // суммы совпадают с GROUP BY по тем же датам (проверено запросом к БД)
  expect(summary[0].amount).toBe(30367500)
  expect(summary[0].profit).toBe(6134375)
  expect(summary[5].amount).toBe(43600) // группа с пустым регионом
  expect(summary[6].amount).toBe(150977000)
  expect(summary[6].profit).toBe(31013250)
  // строки итогов оформлены как зона экрана, а не как обычные детали
  expect(summary.every((s) => s.bold)).toBe(true)

  // CSV — машинный формат: строк итогов в нём нет
  const csv = await request.post('/api/report/export.csv', { data: { reportId: 'sales-summary', params } })
  const text = (await csv.body()).toString('utf8')
  expect(text).not.toContain('Итого')
  expect(text.trim().split('\r\n')).toHaveLength(2749) // шапка + 2 748 деталей
})

test('drill-through с диаграммы: клик по столбцу открывает детали с параметрами', async ({ page }) => {
  await page.goto('/report.html?report=sales-summary&from=2026-03-01&to=2026-03-10')
  await expect(page.locator('.rt-table')).toBeVisible()
  await expect.poll(() => page.evaluate(() => !!window.__chart?.instance)).toBe(true)
  await page.waitForTimeout(600)

  // кликаем по центру первого столбца первой серии — настоящий клик мышью по canvas
  const point = await page.evaluate(() => {
    const ch = window.__chart.instance
    const dom = ch.getDom()
    dom.scrollIntoView({ block: 'center' })
    const box = dom.getBoundingClientRect()
    const zr = ch.getZr()
    const opt = ch.getOption()
    // ищем точку, где под курсором действительно столбец (а не ось или подпись)
    for (let x = 60; x < ch.getWidth(); x += 6) {
      for (let y = 24; y < ch.getHeight() - 40; y += 6) {
        const el = zr.handler.findHover(x, y).target
        if (el && el.type === 'rect') {
          return {
            x: box.left + x,
            y: box.top + y,
            categories: opt.xAxis[0].data,
            seriesNames: opt.series.map((s) => s.name)
          }
        }
      }
    }
    return null
  })
  expect(point, 'столбец должен быть кликабелен').toBeTruthy()

  await page.mouse.click(point.x, point.y)
  await expect(page).toHaveURL(/report=sales-drill/)
  const url = new URL(page.url())
  expect(point.categories).toContain(url.searchParams.get('category'))
  expect(point.seriesNames).toContain(url.searchParams.get('region'))
  expect(url.searchParams.get('from')).toBe('2026-03-01')
  expect(url.searchParams.get('to')).toBe('2026-03-10')

  // целевой отчёт действительно открылся на этих данных
  await expect(page.locator('.rt-table')).toBeVisible()
  await expect(page.locator('.meta')).toContainText('строк')
})

test('band: значение и закрашенный коридор между границами', async ({ page }) => {
  await page.goto('/report.html?report=ops-band&from=2026-03-01&to=2026-03-31')
  await expect(page.locator('.meta')).toContainText('шаблон v2')

  const band = await page.evaluate(() => {
    const entry = (window.__charts || []).find((c) => c.type === 'band')
    const option = entry?.instance?.getOption()
    const series = option?.series || []
    const fill = series.find((s) => s.areaStyle?.color)
    const value = series.find((s) => s.name === 'Значение')
    return {
      count: series.length,
      points: value?.data?.length ?? 0,
      bandPoints: fill?.data?.length ?? 0,
      color: fill?.areaStyle?.color,
      opacity: fill?.areaStyle?.opacity,
      stack: fill?.stack,
      firstCategory: option?.xAxis?.[0]?.data?.[0],
      firstBand: fill?.data?.[0],
      firstLow: series.find((s) => s.name === '__band_low')?.data?.[0]
    }
  })

  expect(band.count).toBe(3)                       // нижняя граница, заливка, значение
  expect(band.points).toBe(31)                     // 31 день марта
  expect(band.bandPoints).toBe(31)
  expect(band.stack).toBe('band')                  // заливка строится стеком
  expect(band.color).toBe('#2e7d32')
  expect(band.opacity).toBeCloseTo(0.22, 2)
  expect(band.firstCategory).toBe('2026-03-01')
  expect(band.firstLow).toBe(127)                  // минимум по сервисам
  expect(band.firstBand).toBe(150)                 // 277 − 127
  await expect(page.locator('.chart-box').first()).toContainText('значение и коридор')
})

test('gauge с четырьмя границами: пять цветовых зон и цвет указателя', async ({ page }) => {
  await page.goto('/report.html?report=margin-zones&from=2026-03-01&to=2026-03-10')
  await expect(page.locator('.meta')).toContainText('шаблон v2')

  const gauge = await page.evaluate(() => {
    const entry = (window.__charts || []).find((c) => c.type === 'gauge')
    const s = entry?.instance?.getOption()?.series?.[0]
    return s
      ? {
          value: s.data?.[0]?.value,
          bands: s.axisLine?.lineStyle?.color?.length ?? 0,
          first: s.axisLine?.lineStyle?.color?.[0]?.[1],
          last: s.axisLine?.lineStyle?.color?.at(-1)?.[1],
          middle: s.axisLine?.lineStyle?.color?.[2]?.[1],
          pointer: s.pointer?.itemStyle?.color,
          detail: s.detail?.color
        }
      : null
  })

  expect(gauge).not.toBeNull()
  expect(gauge.value).toBeCloseTo(20.54, 2)      // попадает в зелёную зону 15…25
  expect(gauge.bands).toBe(5)                     // 4 границы → 5 зон
  expect(gauge.first).toBe('#c62828')             // ниже p1 — красная
  expect(gauge.middle).toBe('#2e7d32')            // p2…p3 — зелёная
  expect(gauge.last).toBe('#c62828')              // выше p4 — красная
  expect(gauge.pointer).toBe('#2e7d32')
  expect(gauge.detail).toBe('#2e7d32')
  await expect(page.locator('.chart-box').first()).toContainText('Маржа за период')
})

test('диаграммы gauge и sankey: спидометр и потоки рисуются из своих наборов', async ({ page }) => {
  await page.goto('/report.html?report=sales-flow&from=2026-03-01&to=2026-03-10')
  await expect(page.locator('.meta')).toContainText('шаблон v2')
  await expect(page.locator('.chart-box h2')).toHaveCount(2)

  // обе диаграммы инициализированы (по канвасу на каждую)
  await expect(page.locator('.chart canvas')).toHaveCount(2)

  const charts = await page.evaluate(() =>
    (window.__charts || []).map(({ type, instance }) => {
      const s = instance.getOption().series?.[0]
      return {
        type,
        series: s?.type,
        nodes: s?.data?.length ?? null,
        links: s?.links?.length ?? null,
        value: s?.type === 'gauge' ? s.data?.[0]?.value : null,
        min: s?.type === 'gauge' ? s.min : null,
        max: s?.type === 'gauge' ? s.max : null
      }
    })
  )

  // sankey читает свой набор данных (dataKey=flows), а не первый из шаблона
  expect(charts.find((c) => c.type === 'sankey')).toMatchObject({ series: 'sankey', nodes: 9, links: 20 })

  // gauge: маржа за 1–10 марта 2026 = 20.54 % при шкале 0…100
  const gauge = charts.find((c) => c.type === 'gauge')
  expect(gauge.series).toBe('gauge')
  expect(gauge.value).toBeCloseTo(20.54, 2)
  expect([gauge.min, gauge.max]).toEqual([0, 100])
  await expect(page.locator('.chart-box').first()).toContainText('Маржа за период')
})

test('рассылка: часовой пояс расписания считается при создании', async ({ request, page }) => {
  const created = await request.post('/api/admin/subscriptions', {
    data: {
      reportId: 'sales-summary',
      title: 'e2e часовой пояс',
      cron: '0 9 * * *',
      recipients: ['tz@example.test'],
      format: 'csv',
      timezone: 'Asia/Novosibirsk'
    }
  })
  expect(created.ok()).toBeTruthy()
  const sub = await created.json()
  // 9:00 в Новосибирске (UTC+7) — это 02:00 UTC
  const next = new Date(Number(sub.nextRunAt) * 1000)
  expect(next.getUTCHours()).toBe(2)
  expect(next.getUTCMinutes()).toBe(0)
  expect(sub.timezone).toBe('Asia/Novosibirsk')

  // пояс виден в списке и в карточке подписки
  const list = await (await request.get('/api/admin/subscriptions')).json()
  const row = list.items.find((s) => s.id === sub.id)
  expect(row.timezone).toBe('Asia/Novosibirsk')

  // неизвестный пояс отклоняется сервером
  const bad = await request.post('/api/admin/subscriptions', {
    data: {
      reportId: 'sales-summary',
      title: 'e2e плохой пояс',
      cron: '0 9 * * *',
      recipients: ['tz@example.test'],
      timezone: 'Mars/Olympus'
    }
  })
  expect(bad.status()).toBe(422)
  expect((await bad.json()).error.message).toContain('часовой пояс')

  // на странице подписки пояс подписан рядом с форматом
  await page.goto('/subscriptions.html')
  await expect(page.locator(`tr[data-sub="${sub.id}"]`)).toContainText('Asia/Novosibirsk')

  await request.delete(`/api/admin/subscriptions/${sub.id}`)
})

test('аудит: действия попадают в журнал и видны на странице', async ({ page, request }) => {
  // действие: создание пользователя и его удаление
  const login = 'e2e_audit'
  const created = await request.post('/api/admin/users', {
    data: { login, role: 'viewer', password: 'audit-pass-1' }
  })
  expect(created.ok()).toBeTruthy()
  const { id } = await created.json()
  expect((await request.delete(`/api/admin/users/${id}`)).ok()).toBeTruthy()

  const feed = await (await request.get('/api/admin/audit?limit=20')).json()
  expect(feed.total).toBeGreaterThan(0)
  const actions = feed.items.map((r) => r.action)
  expect(actions).toContain('user.create')
  expect(actions).toContain('user.delete')
  const row = feed.items.find((r) => r.action === 'user.create' && r.objectId === String(id))
  expect(row.actor).toBe('admin')
  expect(row.details.login).toBe(login)
  expect(row.at).toMatch(/^\d{4}-\d{2}-\d{2}T/)

  // фильтр по действию на сервере
  const onlyDeletes = await (await request.get('/api/admin/audit?action=user.delete&limit=20')).json()
  expect(onlyDeletes.items.every((r) => r.action === 'user.delete')).toBe(true)

  // страница журнала
  await page.goto('/audit.html')
  await expect(page.locator('h1')).toHaveText('Журнал действий')
  await expect(page.locator('tr[data-audit="user.create"]').first()).toContainText(login)
})

// ADR-67: ретеншен удаляет журнал безвозвратно, значит архив нужно уметь снимать.
test('журнал: выгрузка CSV с фильтрами и запись о самой выгрузке', async ({ page, request }) => {
  const res = await request.get('/api/admin/audit/export.csv?action=auth.login')
  expect(res.ok()).toBeTruthy()
  expect(res.headers()['content-type']).toContain('text/csv')

  const body = Buffer.from(await res.body())
  // по умолчанию — RFC 4180: без BOM, запятая, CRLF
  expect(body.slice(0, 3).toString('hex')).not.toBe('efbbbf')
  const text = body.toString('utf8')
  const lines = text.trim().split('\r\n')
  expect(lines[0]).toBe('at,actor,action,object_type,object_id,details,ip')
  expect(lines.length).toBeGreaterThan(1)
  expect(lines.slice(1).every((line) => line.includes('auth.login'))).toBe(true)

  // excel=1: BOM и точка с запятой, чтобы файл открывался двойным щелчком
  const excel = await request.get('/api/admin/audit/export.csv?action=auth.login&excel=1')
  const excelBody = Buffer.from(await excel.body())
  expect(excelBody.slice(0, 3).toString('hex')).toBe('efbbbf')
  expect(excelBody.toString('utf8')).toContain('at;actor;action')

  // предел строк — честная ошибка, а не молча обрезанный архив
  const small = await request.get('/api/admin/audit/export.csv?limit=1')
  expect(small.status()).toBe(422)
  expect((await small.json()).error.code).toBe('AUDIT_LIMIT')

  // фильтр по периоду и плохая дата
  const period = await request.get('/api/admin/audit/export.csv?from=2026-11-08&to=2026-11-08')
  expect(period.ok()).toBeTruthy()
  expect((await request.get('/api/admin/audit/export.csv?from=08.11.2026')).status()).toBe(422)

  // сама выгрузка — тоже действие журнала
  const audit = await (await request.get('/api/admin/audit?action=audit.export&limit=5')).json()
  expect(audit.total).toBeGreaterThan(0)
  expect(audit.items[0].details).toHaveProperty('rows')

  // на странице кнопки выгрузки несут текущие фильтры
  await page.goto('/audit.html')
  const csvLink = page.locator('a[data-export="csv"]')
  await expect(csvLink).toHaveAttribute('href', /\/api\/admin\/audit\/export\.csv/)
  await page.locator('select').selectOption('auth.login')
  await expect(csvLink).toHaveAttribute('href', /action=auth\.login/)
  await expect(page.locator('a[data-export="excel"]')).toHaveAttribute('href', /excel=1/)
})

test('версия шаблона: старый мигрируется, слишком новый отклоняется', async ({ request }) => {
  const v1 = {
    version: 1,
    id: 'migrate-e2e',
    title: 'Миграция v1',
    params: [],
    datasets: { s: { sql: 'SELECT 1 AS x, 2 AS y' } },
    blocks: [{ type: 'chart', dataKey: 's', xField: 'x', yField: 'y' }]
  }
  await request.delete('/api/reports/migrate-e2e')
  expect((await request.post('/api/reports', { data: { id: 'migrate-e2e', body: v1 } })).ok()).toBeTruthy()

  // сервер сохранил шаблон уже в версии 2 и дозаполнил chartType
  const saved = await (await request.get('/api/reports/migrate-e2e')).json()
  expect(saved.version).toBe(2)
  expect(saved.blocks[0].chartType).toBe('bar')

  // шаблон новее сервера не принимается, в сообщении — обе версии
  const tooNew = await request.post('/api/reports', {
    data: { id: 'migrate-v99', body: { ...v1, version: 99 } }
  })
  expect(tooNew.status()).toBe(422)
  const error = await tooNew.json()
  expect(error.error.message).toContain('99')
  expect(error.error.message).toContain('до 2')

  await request.delete('/api/reports/migrate-e2e')
})

test('пользователи: создание, вход, отключение, защита последнего админа', async ({ page, request, baseURL }) => {
  const login = 'e2e_analyst'
  const password = 'e2e-pass-123'

  // чистим возможный след прошлого прогона
  const existing = await (await request.get('/api/admin/users')).json()
  const stale = existing.items.find((u) => u.login === login)
  if (stale) await request.delete(`/api/admin/users/${stale.id}`)

  const created = await request.post('/api/admin/users', { data: { login, role: 'viewer', password } })
  expect(created.ok()).toBeTruthy()
  const { id } = await created.json()

  // пароль короче 8 и дубль логина отклоняются
  expect((await request.post('/api/admin/users', { data: { login: 'e2e_short', role: 'viewer', password: '123' } })).status()).toBe(422)
  expect((await request.post('/api/admin/users', { data: { login, role: 'viewer', password } })).status()).toBe(409)

  // вход новым пользователем — в отдельном контексте, чтобы не сбить admin-cookie
  const ctx = await apiRequest.newContext({ baseURL })
  expect((await ctx.post('/api/auth/login', { data: { login, password } })).ok()).toBeTruthy()
  expect((await ctx.get('/api/reports')).ok()).toBeTruthy()

  // отключение: вход запрещён, старая сессия не действует
  expect((await request.put(`/api/admin/users/${id}`, { data: { role: 'viewer', enabled: false } })).ok()).toBeTruthy()
  expect((await ctx.post('/api/auth/login', { data: { login, password } })).status()).toBe(403)
  expect((await ctx.get('/api/reports')).status()).toBe(401)
  await ctx.dispose()

  // себя удалить нельзя
  const me = (await (await request.get('/api/admin/users')).json()).items.find((u) => u.login === 'admin')
  const selfDelete = await request.delete(`/api/admin/users/${me.id}`)
  expect(selfDelete.status()).toBe(409)
  expect((await selfDelete.json()).error.code).toBe('SELF_PROTECTED')

  // страница пользователей
  await page.goto('/users.html')
  await expect(page.locator('h1')).toHaveText('Пользователи')
  await expect(page.locator('tr[data-user="admin"]')).toContainText('это вы')
  await expect(page.locator(`tr[data-user="${login}"]`)).toContainText('отключён')

  // уборка
  expect((await request.delete(`/api/admin/users/${id}`)).ok()).toBeTruthy()
})

test('рассылка: подписки CRUD, cron и адреса проверяются на сервере', async ({ page, request }) => {
  const invalidCron = await request.post('/api/admin/subscriptions', {
    data: { reportId: 'sales-aggregate', cron: '99 * * * *', recipients: ['po@example.test'], params: {} }
  })
  expect(invalidCron.status()).toBe(422)
  expect((await invalidCron.json()).error.message).toContain('диапазона')

  const invalidMail = await request.post('/api/admin/subscriptions', {
    data: { reportId: 'sales-aggregate', cron: '0 9 * * *', recipients: ['не-адрес'], params: {} }
  })
  expect(invalidMail.status()).toBe(422)

  const badFormat = await request.post('/api/admin/subscriptions', {
    data: { reportId: 'sales-aggregate', cron: '0 9 * * *', recipients: ['po@example.test'], format: 'pdf' }
  })
  expect(badFormat.status()).toBe(422)
  expect((await badFormat.json()).error.message).toContain('csv или xlsx')

  const created = await request.post('/api/admin/subscriptions', {
    data: {
      reportId: 'sales-aggregate',
      title: 'e2e рассылка',
      cron: '0 9 * * 1-5',
      params: { from: '2026-03-01', to: '2026-03-10' },
      recipients: ['po@example.test'],
      format: 'xlsx',
      enabled: false
    }
  })
  expect(created.ok()).toBeTruthy()
  const { id } = await created.json()
  expect(id).toBeGreaterThan(0)

  const list = await (await request.get('/api/admin/subscriptions')).json()
  const item = list.items.find((s) => s.id === id)
  expect(item.title).toBe('e2e рассылка')
  expect(item.format).toBe('xlsx')
  expect(item.enabled).toBe(false)
  expect(item.nextRunAt).toMatch(/^\d{4}-\d{2}-\d{2}T/)

  // журнал отправок доступен и пуст, пока не было запусков
  const runs = await (await request.get(`/api/admin/subscriptions/${id}/runs`)).json()
  expect(Array.isArray(runs.items)).toBe(true)

  // страница рассылки показывает подписку, форму и журнал
  await page.goto('/subscriptions.html')
  await expect(page.locator('h1')).toHaveText('Рассылка отчётов')
  await expect(page.locator(`tr[data-sub="${id}"]`)).toContainText('e2e рассылка')
  await expect(page.locator('form.form')).toBeVisible()
  await page.locator(`tr[data-sub="${id}"]`).getByRole('button', { name: 'Журнал' }).click()
  await expect(page.locator('table.runs, .journal')).toBeVisible()

  const removed = await request.delete(`/api/admin/subscriptions/${id}`)
  expect(removed.ok()).toBeTruthy()
  const after = await (await request.get('/api/admin/subscriptions')).json()
  expect(after.items.find((s) => s.id === id)).toBeUndefined()
})

test('серверная валидация шаблона: список ошибок с путями, сохранение отклонено', async ({ request }) => {
  const bad = {
    version: 1,
    id: 'bad-server-e2e',
    title: 'Плохой шаблон',
    datasets: { s: { sql: 'DELETE FROM sales' } },
    blocks: [{ type: 'table', dataKey: 's', columns: [{ title: 'без поля' }] }]
  }

  // проверка отдаёт полный список, а не одну строку
  const check = await request.post('/api/reports/bad-server-e2e/validate', { data: { body: bad } })
  expect(check.ok()).toBeTruthy()
  const payload = await check.json()
  expect(payload.ok).toBe(false)
  const paths = payload.errors.map((e) => e.path)
  expect(paths).toContain('datasets.s')          // SQL только на чтение
  expect(paths).toContain('blocks.0.columns.0')  // колонка без field/expression
  expect(payload.errors[0].message).toBeTruthy()

  // сохранить такой шаблон нельзя — даже если клиент не проверял
  const save = await request.post('/api/reports', { data: { id: 'bad-server-e2e', body: bad } })
  expect(save.status()).toBe(422)
  const error = await save.json()
  expect(error.error.code).toBe('TEMPLATE_INVALID')
  expect(error.error.message).toContain('datasets.s')
})

test('обновление по событию: публикация шаблона обновляет экран без опроса', async ({ page, request }) => {
  const id = 'live-e2e'
  const body = {
    version: 1,
    id,
    title: 'По событию e2e',
    live: true,
    params: [],
    datasets: { s: { sql: 'SELECT 1 AS x' } },
    blocks: [{ type: 'table', dataKey: 's', columns: [{ field: 'x', title: 'X' }] }]
  }
  await request.delete(`/api/reports/${id}`)
  expect((await request.post('/api/reports', { data: { id, body } })).ok()).toBeTruthy()

  const calls = []
  const streams = []
  page.on('request', (req) => {
    if (req.url().includes('/api/report/data')) calls.push(Date.now())
    if (req.url().includes('/api/report/changes')) streams.push('long-poll')
  })
  page.on('response', (res) => {
    if (res.url().includes('/api/report/events')) streams.push(res.headers()['content-type'])
  })

  await page.goto(`/report.html?report=${id}`)
  await expect(page.locator('.rt-table')).toBeVisible()
  await expect(page.locator('.refresh')).toContainText('обновление по событию')
  // транспорт — SSE, а не удержание запроса
  await expect.poll(() => streams.some((s) => s.includes('text/event-stream'))).toBe(true)
  expect(streams).not.toContain('long-poll')
  await page.waitForTimeout(1500)
  expect(calls.length).toBe(1) // опроса по таймеру нет

  // публикуем шаблон — сервер будит открытую страницу, она перезапрашивает данные
  expect((await request.put(`/api/admin/reports/${id}/draft`, { data: { ...body, title: 'По событию e2e v2' } })).ok()).toBeTruthy()
  expect((await request.post(`/api/admin/reports/${id}/publish`)).ok()).toBeTruthy()

  await expect.poll(() => calls.length, { timeout: 5000 }).toBeGreaterThanOrEqual(2)
  await expect(page.locator('h1')).toHaveText('По событию e2e v2')
})

test('автообновление: шаблон задаёт период, страница перезапрашивает данные', async ({ page, request }) => {
  const id = 'refresh-e2e'
  const body = {
    version: 1,
    id,
    title: 'Автообновление e2e',
    refreshSeconds: 2,
    params: [],
    datasets: { s: { sql: 'SELECT 1 AS x' } },
    blocks: [{ type: 'table', dataKey: 's', columns: [{ field: 'x', title: 'X' }] }]
  }
  await request.delete(`/api/reports/${id}`)
  expect((await request.post('/api/reports', { data: { id, body } })).ok()).toBeTruthy()

  const calls = []
  page.on('request', (req) => {
    if (req.url().includes('/api/report/data')) calls.push(Date.now())
  })

  await page.goto(`/report.html?report=${id}`)
  await expect(page.locator('.rt-table')).toBeVisible()
  await expect(page.locator('.refresh')).toContainText('обновление каждые 2 с')

  // за 5 секунд должно быть минимум 2 запроса: первый и одно автообновление
  await page.waitForTimeout(5000)
  expect(calls.length).toBeGreaterThanOrEqual(2)

  // пауза останавливает опрос
  await page.getByRole('button', { name: 'Пауза' }).click()
  const before = calls.length
  await page.waitForTimeout(3000)
  expect(calls.length).toBe(before)
  await expect(page.locator('.refresh')).toContainText('на паузе')

  // возобновление сразу перезапрашивает
  await page.getByRole('button', { name: 'Возобновить' }).click()
  await expect.poll(() => calls.length, { timeout: 5000 }).toBeGreaterThan(before)

  await request.delete(`/api/reports/${id}`)
})
