import { expect, request as apiRequest, test } from '@playwright/test'
import JSZip from 'jszip'
import { readFileSync } from 'node:fs'

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

// Каталог отдаёт по 10 карточек (ADR-41): догружаем, пока не наберём total.
// Один клик «Показать ещё» больше не гарантирует всё — отчётов стало больше двух страниц.
async function loadAllCards(page, total) {
  for (let guard = 0; guard < 10 && (await page.locator('.card').count()) < total; guard += 1) {
    const more = page.getByRole('button', { name: /Показать ещё/ })
    if ((await more.count()) === 0) break
    const before = await page.locator('.card').count()
    await more.click()
    await expect
      .poll(async () => page.locator('.card').count(), { message: 'карточки догружаются' })
      .toBeGreaterThan(before)
  }
}

test('список отчётов: карточки, поиск, переход в отчёт', async ({ page, request }) => {
  const list = await (await request.get('/api/reports')).json()
  const total = list.length

  await page.goto('/')
  await expect(page.locator('h1')).toHaveText('Сервер отчётов')
  // каталог отдаёт страницы по 10 (ADR-41)
  await expect(page.locator('.card')).toHaveCount(Math.min(10, total))
  await expect(page.locator('.count')).toHaveText(`${Math.min(10, total)} из ${total}`)
  if (total > 10) await loadAllCards(page, total)
  await expect(page.locator('.card')).toHaveCount(total)

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
  if (total > 10) await loadAllCards(page, total)
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
    // отчёт с autoLoad: false ждёт нажатия «Обновить» — тест нажимает
    if (await page.locator('.no-data').count()) {
      await page.locator('.params button[type=submit]').click()
    }
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

  // открываем конкретный отчёт со справочником, а не «первый в списке»:
  // от его порядка зависит, есть ли в панели cost_center
  await page.locator('.admin-search, input[type=search]').first().fill('finance-plan-fact')
  const row = page.locator('.rep-row, li').filter({ hasText: 'finance-plan-fact' }).first()
  await row.getByRole('button', { name: 'Изменить' }).click()

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
  if (total > 10) await loadAllCards(page, total)
  await expect(page.locator('.card')).toHaveCount(total)
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
      firstLow: series.find((s) => s.name === '__band_low_0')?.data?.[0]
    }
  })

  expect(band.count).toBe(3)                       // нижняя граница, заливка, значение
  expect(band.points).toBe(31)                     // 31 день марта
  expect(band.bandPoints).toBe(31)
  expect(band.stack).toBe('band-0')                // заливка строится стеком (номер коридора)
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

// ADR-70: детали группы приходят страницами, остаток добирается кнопкой, кэш живёт до смены данных.
// Свой шаблон без live: подписка на события перезагружает данные (и сбрасывает кэш деталей),
// а проверяем мы порционную подгрузку, а не реакцию на публикацию.
test('детали группы: страницы по 200, кнопка «ещё», повторное раскрытие без запроса', async ({ page, request }) => {
  const id = 'agg-e2e'
  await request.delete(`/api/reports/${id}`)
  const created = await request.post('/api/reports', {
    data: {
      id,
      body: {
        version: 2,
        id,
        title: 'Агрегация e2e',
        params: [
          { name: 'from', type: 'date', label: 'с', required: true, default: '2026-03-01', source: { kind: 'date' } },
          { name: 'to', type: 'date', label: 'по', required: true, default: '2026-03-10', source: { kind: 'date' } }
        ],
        datasets: {
          sales: {
            sql: 'SELECT s.category, s.amount, s.profit FROM reports_examples.sales s WHERE s.order_date >= :from::date AND s.order_date <= :to::date',
            params: { from: '$from', to: '$to' }
          }
        },
        blocks: [
          {
            type: 'table',
            dataKey: 'sales',
            title: 'Продажи',
            aggregate: {
              groupBy: ['category'],
              measures: [{ field: 'amount', agg: 'sum' }, { field: 'profit', agg: 'sum' }]
            },
            grandTotal: true,
            columns: [
              // group: true делает из строки разрез с шевроном — без него детали некуда раскрывать
              { field: 'category', title: 'Категория', group: true, width: 220 },
              { field: 'amount', title: 'Сумма', format: 'integer', align: 'right', aggregate: 'sum', width: 150 },
              { field: 'profit', title: 'Прибыль', format: 'integer', align: 'right', aggregate: 'sum', width: 150 }
            ]
          }
        ]
      }
    }
  })
  expect(created.ok()).toBeTruthy()

  // Страницы не должны ни терять, ни дублировать строки: мультимножество
  // четырёх страниц обязано совпасть с полной выборкой группы.
  const pageBody = (offset) => ({
    reportId: id,
    params: { from: '2026-03-01', to: '2026-03-10' },
    field: 'category',
    value: 'Электроника',
    dataset: 'sales',
    limit: 200,
    offset
  })
  const paged = []
  for (const offset of [0, 200, 400, 600]) {
    const res = await request.post('/api/report/group', { data: pageBody(offset) })
    expect(res.ok()).toBeTruthy()
    paged.push(...(await res.json()).rows)
  }
  const all = await (await request.post('/api/report/group', { data: { ...pageBody(0), limit: 5000 } })).json()
  const key = (row) => JSON.stringify(row)
  expect(paged).toHaveLength(all.total)
  expect(paged.map(key).sort()).toEqual(all.rows.map(key).sort())

  const calls = []
  page.on('request', (req) => {
    if (req.url().includes('/api/report/group')) calls.push(JSON.parse(req.postData() || '{}'))
  })

  await page.goto(`/report.html?report=${id}&from=2026-03-01&to=2026-03-10`)
  await expect(page.locator('.meta')).toContainText('строк: 5')

  // раскрывает шеврон в строке группы (обработчик висит на нём, а не на всей ячейке)
  await page.locator('.rt-table tr', { hasText: 'Итого Электроника' }).locator('.chev').first().click()
  await expect(page.locator('.tv-info', { hasText: 'показано 200 из 689' })).toBeVisible()
  const more = page.locator('.tv-more')
  await expect(more).toHaveText(/ещё 489/)
  expect(calls).toHaveLength(1)
  expect(calls[0].offset ?? 0).toBe(0)
  expect(calls[0].limit).toBe(200)

  // добираем остаток страницами: каждый следующий запрос идёт со своим offset
  await more.click()
  await expect(page.locator('.tv-info', { hasText: 'показано 400 из 689' })).toBeVisible()
  await expect(page.locator('.tv-more')).toHaveText(/ещё 289/)
  await page.locator('.tv-more').click()
  await expect(page.locator('.tv-info', { hasText: 'показано 600 из 689' })).toBeVisible()
  await page.locator('.tv-more').click()
  await expect(page.locator('.tv-info', { hasText: 'показано' })).toHaveCount(0)
  await expect(page.locator('.tv-more')).toHaveCount(0)
  expect(calls).toHaveLength(4)
  expect(calls.map((c) => c.offset ?? 0)).toEqual([0, 200, 400, 600])

  // «Развернуть всё» тянет все группы до конца страницами, а не только первую
  const beforeExpandAll = calls.length
  await page.getByRole('button', { name: 'Развернуть всё' }).click()
  await expect(page.getByRole('button', { name: 'Развернуть всё' })).toBeEnabled({ timeout: 30000 })
  await expect(page.locator('.tv-info', { hasText: 'показано' })).toHaveCount(0)
  await expect(page.locator('.tv-more')).toHaveCount(0)
  // пять групп по 689/687/686/685/1 строке: 4+4+4+4+1 страниц, первая из них уже была
  expect(calls.length).toBeGreaterThan(beforeExpandAll)
  const offsets = calls.slice(beforeExpandAll).map((c) => c.offset ?? 0)
  expect(offsets).toContain(600)
  expect(offsets.filter((o) => o === 0).length).toBeGreaterThanOrEqual(3) // остальные группы грузятся с начала

  // сворачивание и повторное раскрытие берут строки из кэша — новых запросов нет.
  // После «Развернуть всё» строк тысячи: возвращаемся к началу, иначе строки группы
  // нет в окне виртуализации и шеврон некуда нажать.
  const callsAfterExpandAll = calls.length
  // «Свернуть всё» возвращает короткую таблицу: в развёрнутой группу не найти в окне
  // виртуализации, а кэш деталей при сворачивании сохраняется
  await page.getByRole('button', { name: 'Свернуть всё' }).click()
  await expect(page.locator('.rt-table tr')).toHaveCount(7) // шапка + пять групп + ИТОГО
  const chev = page.locator('.rt-table tr', { hasText: 'Итого Электроника' }).locator('.chev').first()
  await chev.click()
  await expect(page.locator('.rt-info, .tv-info', { hasText: 'показано' })).toHaveCount(0)
  expect(calls).toHaveLength(callsAfterExpandAll)

  await request.delete(`/api/reports/${id}`)
})

// ADR-72: переход из цепочки уровней подставляет значения уровней, а не пару x/series.
// Запрет перехода без нужного уровня проверяется юнитом (engine/drill.test.js): в браузере
// лента рисуется поверх узла, и «клик мимо» там не отличить от клика по ленте.
test('drill из цепочки sankey: связь «регион → категория» открывает детали', async ({ page }) => {
  await page.goto('/report.html?report=sales-path&from=2026-03-01&to=2026-03-10')
  await expect(page.locator('.meta')).toContainText('строк:')
  await expect(page.locator('.chart canvas').first()).toBeVisible()

  // Ленты уровня 0→1 лежат между первым и вторым столбцом узлов (x от ширины узла
  // до узла второго уровня): точка в этом коридоре гарантированно попадает в связь,
  // где участвуют оба первых уровня
  const point = await page.evaluate(() => {
    const ch = (window.__charts || []).find((c) => c.type === 'sankey').instance
    const dom = ch.getDom()
    dom.scrollIntoView({ block: 'center' })
    const box = dom.getBoundingClientRect()
    const zr = ch.getZr()
    const sm = ch.getModel().getSeriesByIndex(0)
    const g = sm.getGraph()
    const xs = []
    g.eachNode((n) => xs.push(Math.round(n.getLayout().x)))
    const left = Math.min(...xs)
    const right = [...new Set(xs)].sort((a, b) => a - b)[1]
    for (let x = left + 20; x < right - 10; x += 3) {
      for (let y = 10; y < ch.getHeight() - 10; y += 3) {
        const el = zr.handler.findHover(x, y).target
        if (el?.type === 'path') return { x: box.left + x, y: box.top + y, left, right }
      }
    }
    return null
  })
  expect(point, 'связь уровня 0→1 должна быть кликабельна').toBeTruthy()

  await page.mouse.click(point.x, point.y)
  await expect(page).toHaveURL(/report=sales-drill/)
  const url = new URL(page.url())
  // переход подставил оба уровня цепочки и период текущего отчёта
  expect(url.searchParams.get('region')).toBeTruthy()
  expect(url.searchParams.get('category')).toBeTruthy()
  expect(url.searchParams.get('from')).toBe('2026-03-01')
  await expect(page.locator('.meta')).toContainText('строк:')
})

// ADR-71: порядок деталей задаёт шаблон, а не текст строки.
test('порядок деталей группы берётся из detailOrder шаблона', async ({ page, request }) => {
  const body = (offset) => ({
    reportId: 'sales-aggregate',
    params: { from: '2026-03-01', to: '2026-03-10' },
    field: 'category',
    value: 'Электроника',
    dataset: 'sales',
    limit: 200,
    offset,
    order: [{ field: 'profit', dir: 'desc' }, { field: 'amount', dir: 'desc' }]
  })

  const first = await (await request.post('/api/report/group', { data: body(0) })).json()
  const second = await (await request.post('/api/report/group', { data: body(200) })).json()
  const profits = [...first.rows, ...second.rows].map((r) => r.profit)

  // стартовая строка — максимальная прибыль, дальше только невозрастание
  expect(profits[0]).toBe(24400)
  expect(profits.every((v, i) => i === 0 || profits[i - 1] >= v)).toBe(true)
  // стык страниц не рвёт порядок
  expect(profits[199]).toBeGreaterThanOrEqual(profits[200])

  // то же в интерфейсе: первая детальная строка показывает максимум
  await page.goto('/report.html?report=sales-aggregate&from=2026-03-01&to=2026-03-10')
  await expect(page.locator('.meta')).toContainText('строк: 5')
  await page.locator('.rt-table tr', { hasText: 'Итого Электроника' }).locator('.chev').first().click()
  await expect(page.locator('.tv-info', { hasText: 'показано 200 из 689' })).toBeVisible()
  const rowsText = (await page.locator('.rt-table tr').allInnerTexts()).map((t) => t.replace(/\s+/g, ' '))
  // первая детальная строка — максимальная прибыль: 98 400 суммы при 24 400 прибыли
  const detail = rowsText.find((t) => t.includes('24 400'))
  expect(detail).toBeTruthy()
  expect(detail).toContain('98 400')
  // до неё нет строки с большей прибылью
  const before = rowsText.slice(0, rowsText.indexOf(detail)).join(' ')
  expect(before).not.toContain('24 500')
})

// ADR-69: несколько коридоров в band — каждая пара границ закрашивается своим цветом.
test('band: два коридора на одном графике, подписи и подсказка', async ({ page }) => {
  await page.goto('/report.html?report=ops-corridors&from=2026-03-01&to=2026-03-31')
  await expect(page.locator('.chart canvas').first()).toBeVisible()
  await expect(page.locator('.meta')).toContainText('строк: 31') // данные пришли до чтения модели

  const model = await page.evaluate(() => {
    const chart = (window.__charts || [])[0]
    const series = chart.instance.getOption().series
    return {
      series: series.map((s) => ({ name: s.name, stack: s.stack || null })),
      fills: series.filter((s) => s.areaStyle?.color).map((s) => s.areaStyle.color),
      widths: series.map((s) => (s.name.startsWith('__band_fill') ? s.data[0] : null))
    }
  })

  // два коридора = четыре служебных ряда (основание + заливка) и линия значения
  expect(model.series.map((s) => s.name)).toEqual([
    '__band_low_0', '__band_fill_0', '__band_low_1', '__band_fill_1', 'Значение'
  ])
  // стеки разные, иначе второй коридор начнётся от верха первого
  expect(model.series.map((s) => s.stack)).toEqual(['band-0', 'band-0', 'band-1', 'band-1', null])
  // цвета разные: иначе два диапазона не различить
  expect(new Set(model.fills).size).toBe(2)
  // первый день: разброс 277−127=150, среднее ± σ 253.2−150.8=102.4
  expect(model.widths[1]).toBe(150)
  expect(model.widths[3]).toBe(102.4)

  const legend = page.locator('.chart-band-legend .band-key')
  await expect(legend).toHaveCount(2)
  await expect(legend.first()).toContainText('min…max по строкам дня')
  await expect(legend.nth(1)).toContainText('среднее ± σ')
})

// Пустой элемент списка объявляет шаблон (ADR-84): фронт больше не подставляет «—»
test('параметр-справочник показывает объявленный в шаблоне элемент, а не «—»', async ({ page }) => {
  await page.goto('/report.html?report=ops-corridors&from=2026-03-01&to=2026-03-31&service=')
  await expect(page.locator('.meta')).toContainText('строк: 31')
  const select = page.locator('select').first()
  await expect(select).toHaveValue('')
  // firstOption — из emptyOption шаблона; тире интерфейс не добавляет
  await expect(select.locator('option').first()).toHaveText('Все сервисы')
  await expect(select.locator('option:checked')).toHaveText('Все сервисы')
  const texts = await select.locator('option').allInnerTexts()
  expect(texts).not.toContain('—')

  await select.selectOption('api')
  await expect(page.locator('.meta')).toContainText('строк: 5')
  await expect(select).toHaveValue('api')
})

test('без emptyOption пустого пункта нет, а default выбирает объявленное значение', async ({ page }) => {
  // справочник без emptyOption: ни тире, ни пустого пункта — только данные из БД
  await page.goto('/report.html?report=sales-by-region')
  const plain = page.locator('.params select').first()
  await expect(plain).not.toHaveValue('')
  const plainTexts = await plain.locator('option').allInnerTexts()
  expect(plainTexts).not.toContain('—')
  expect(plainTexts.every((t) => t.trim() !== '')).toBe(true)

  // emptyOption «Все центры» + default "" → выбран он, данных больше, чем по одному центру
  await page.goto('/report.html?report=finance-plan-fact')
  const cc = page.locator('.params select').first()
  await expect(cc.locator('option').first()).toHaveText('Все центры')
  await expect(cc).toHaveValue('')
  // в текстовом блоке печатается подпись пустого пункта, а не пустая строка/value (ADR-86)
  await expect(page.locator('section.block .text').first()).toContainText('Все центры. Период')
  const rowsAll = Number((await page.locator('.meta').innerText()).match(/строк:\s*(\d+)/)[1])
  expect(rowsAll).toBeGreaterThan(0)

  const values = await cc.locator('option').evaluateAll((els) => els.map((e) => e.value))
  await cc.selectOption(values[1])
  // выбрали центр затрат — в тексте его label, а не идентификатор
  await expect(page.locator('section.block .text').first()).toContainText(`${values[1]}. Период`)
  await expect.poll(async () => Number((await page.locator('.meta').innerText()).match(/строк:\s*(\d+)/)[1])).toBeLessThan(rowsAll)
})

test('band: одна полоса остаётся без подписи коридоров', async ({ page }) => {
  await page.goto('/report.html?report=ops-band&from=2026-03-01&to=2026-03-10')
  await expect(page.locator('.chart canvas').first()).toBeVisible()
  await expect(page.locator('.chart-band-legend')).toHaveCount(0)
  const stacks = await page.evaluate(() => (window.__charts || [])[0].instance.getOption().series.map((s) => s.name))
  expect(stacks).toEqual(['__band_low_0', '__band_fill_0', 'Значение'])
})

// ADR-68: многоуровневый sankey — связи между соседними уровнями цепочки.
test('многоуровневый sankey: три уровня и равные суммы на входе и выходе', async ({ page }) => {
  await page.goto('/report.html?report=sales-path&from=2026-03-01&to=2026-03-10')
  await expect(page.locator('.chart canvas').first()).toBeVisible()

  const model = await page.evaluate(() => {
    const chart = (window.__charts || []).find((c) => c.type === 'sankey')
    if (!chart) return null
    const series = chart.instance.getOption().series[0]
    const links = series.links
    const sum = (list) => list.reduce((acc, l) => acc + l.value, 0)
    return {
      nodes: series.data.length,
      links: links.length,
      sourceLevels: [...new Set(links.map((l) => l.source.split('\u0001')[0]))].sort(),
      targetLevels: [...new Set(links.map((l) => l.target.split('\u0001')[0]))].sort(),
      firstLevelSum: sum(links.filter((l) => l.source.startsWith('0\u0001'))),
      lastLevelSum: sum(links.filter((l) => l.target.startsWith('2\u0001'))),
      labelFormatted: typeof series.label.formatter === 'function'
    }
  })

  expect(model).not.toBeNull()
  // 5 регионов + 5 категорий + 39 товаров
  expect(model.nodes).toBe(49)
  expect(model.links).toBe(60)
  // связи идут только между соседними уровнями: 0→1 и 1→2
  expect(model.sourceLevels).toEqual(['0', '1'])
  expect(model.targetLevels).toEqual(['1', '2'])
  // поток не теряется по дороге: сумма на входе равна сумме на выходе
  expect(model.firstLevelSum).toBe(150892900)
  expect(model.lastLevelSum).toBe(model.firstLevelSum)
  // подписи без служебного префикса уровня
  expect(model.labelFormatted).toBe(true)
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
  await page.locator('select[data-filter="action"]').selectOption('auth.login')
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

test('доменные учётные записи: заглавные и «\\», пустой пароль (ADR-93)', async ({ page, request, baseURL }) => {
  const login = 'CORP\\e2e_domain'
  const other = 'corp\\E2E_DOMAIN' // тот же логин в другом регистре

  const users = await (await request.get('/api/admin/users')).json()
  for (const u of users.items.filter((x) => x.login.toLowerCase() === login.toLowerCase())) {
    await request.delete(`/api/admin/users/${u.id}`)
  }

  // создание с заглавными буквами и «\» без пароля — учётная запись для прокси
  const created = await request.post('/api/admin/users', { data: { login, role: 'viewer', password: '' } })
  expect(created.ok()).toBeTruthy()
  const { id, login: stored } = await created.json()
  expect(stored).toBe(login)

  // дубликат в другом регистре отклоняется: уникальность по lower(login)
  const dup = await request.post('/api/admin/users', { data: { login: other, role: 'viewer', password: '' } })
  expect(dup.status()).toBe(409)
  expect((await dup.json()).error.code).toBe('LOGIN_TAKEN')

  // вход по паролю для учётной записи без пароля невозможен (а не 500)
  const ctx = await apiRequest.newContext({ baseURL })
  expect((await ctx.post('/api/auth/login', { data: { login, password: 'whatever-123' } })).status()).toBe(401)
  expect((await ctx.post('/api/auth/login', { data: { login, password: '' } })).status()).toBe(422)
  await ctx.dispose()

  // страница пользователей показывает доменный логин как есть
  await page.goto('/users.html')
  // CSS-селектор с «\\» пришлось бы экранировать — ищем строку по тексту
  await expect(page.locator('tr', { hasText: login })).toContainText(login)

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
    datasets: { s: { sql: 'DELETE FROM reports_examples.sales' } },
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

  // убираем за собой: иначе шаблон остаётся в каталоге после прогона
  await request.delete(`/api/reports/${id}`)
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

test('внешний источник: отчёт читает вторую БД, пароль остаётся в окружении', async ({ page, request }) => {
  await page.goto('/report.html?report=sales-external&from=2026-01-01&to=2026-03-31')
  // данные из внешней БД приходят вместе с данными сервера: два блока, диаграмма
  await expect(page.getByText('Отгрузки (внешняя БД warehouse)')).toBeVisible()
  await expect(page.locator('.chart canvas').first()).toBeVisible()

  // «Восток»: 27 450 000 + 29 100 000 + 31 800 000 и 1 315 заказов — только из warehouse.
  // Ждём именно вторую таблицу: она появляется после ответа по набору shipments.
  const shipments = page.locator('table').nth(1)
  await expect(shipments).toContainText('88 350 000')
  await expect(shipments).toContainText('1 315')
  await expect(shipments).toContainText('410 450 000') // ИТОГО по внешнему источнику

  // Публичный шаблон не раскрывает ни адрес источника, ни имя переменной с паролем
  const tpl = await (await request.get('/api/reports/sales-external')).json()
  const raw = JSON.stringify(tpl)
  expect(raw).not.toContain('"connections"')
  expect(raw).not.toContain('passwordEnv')
  expect(raw).not.toContain('reporting_ext')
})

test('внешний источник: данные второго набора доступны по dataset', async ({ request }) => {
  const res = await request.post('/api/report/data', {
    data: { reportId: 'sales-external', dataset: 'shipments', params: { from: '2026-01-01', to: '2026-03-31' } }
  })
  expect(res.ok()).toBeTruthy()
  const body = await res.json()
  expect(body.rowCount).toBe(5)
  const east = body.rows.find((r) => r.region === 'Восток')
  expect(String(east.shipped)).toBe('88350000')
  expect(String(east.orders)).toBe('1315')
})

test('статика: сервер сам отдаёт фронтенд и API на одном порту', async ({ request }) => {
  // ADR-79: комплект «бинарник + web/», без nginx и node. Порт бэкенда — 8080.
  // Без сохранённой сессии: проверяем именно «оболочка свободна, API закрыт»
  const ctx = await apiRequest.newContext({
    baseURL: 'http://127.0.0.1:8080',
    storageState: { cookies: [], origins: [] }
  })

  const page = await ctx.get('/report.html?report=sales-summary')
  expect(page.status()).toBe(200)
  const html = await page.text()
  expect(html).toContain('<div id="app"')

  // ассеты сборки отдаются с immutable-кэшем (у Vite в имени хэш)
  const assetPath = html.match(/src="\.?(\/assets\/[^"]+\.js)"/)
  expect(assetPath).not.toBeNull()
  const asset = await ctx.get(assetPath[1])
  expect(asset.status()).toBe(200)
  expect(asset.headers()['content-type']).toContain('javascript')
  expect(asset.headers()['cache-control']).toContain('immutable')

  // страница приложения не кэшируется, иначе после выкладки браузер покажет старую оболочку
  const index = await ctx.get('/')
  expect(index.status()).toBe(200)
  expect(index.headers()['cache-control']).toBe('no-cache')

  // API без входа закрыт, хотя оболочка страницы отдаётся свободно
  const data = await ctx.post('/api/report/data', { data: { reportId: 'sales-summary' } })
  expect(data.status()).toBe(401)

  // с админской сессией неизвестный маршрут API — JSON-ошибка, а не HTML-страница
  const admin = await apiRequest.newContext({
    baseURL: 'http://127.0.0.1:8080',
    storageState: '.auth/admin.json'
  })
  const api = await admin.get('/api/nope')
  expect(api.status()).toBe(404)
  expect(api.headers()['content-type']).toContain('application/json')
  await admin.dispose()

  // маршрут без расширения отдаёт приложение (SPA-fallback)
  const spa = await ctx.get('/portal/x')
  expect(spa.status()).toBe(200)
  expect(await spa.text()).toContain('<div id="app"')

  // отсутствующий файл с расширением — честный 404, а не подмена страницей
  const missing = await ctx.get('/assets/nope.js')
  expect(missing.status()).toBe(404)

  await ctx.dispose()
})

test('префикс /reports: страницы, ассеты и API под одним префиксом', async () => {
  const ctx = await apiRequest.newContext({
    baseURL: 'http://127.0.0.1:8080',
    storageState: { cookies: [], origins: [] }
  })

  const page = await ctx.get('/reports/report.html?report=sales-summary')
  expect(page.status()).toBe(200)
  const html = await page.text()
  expect(html).toContain('<div id="app"')

  // ассеты запрашиваются по /reports/assets/... (клиент считает базу из расположения страницы)
  const asset = html.match(/src="\.?(\/assets\/[^"]+\.js)"/)
  expect(asset).not.toBeNull()
  const file = await ctx.get('/reports' + asset[1])
  expect(file.status()).toBe(200)
  expect(file.headers()['content-type']).toContain('javascript')

  // API живёт там же: /reports/api/...
  const health = await ctx.get('/reports/api/health')
  expect(health.status()).toBe(200)
  expect((await health.json()).status).toBe('ok')

  // корень уводит на префикс, чтобы относительные ссылки на ассеты не поехали
  const root = await ctx.get('/', { maxRedirects: 0 })
  expect(root.status()).toBe(302)
  expect(root.headers()['location']).toBe('/reports/')

  // защита не зависит от префикса: без сессии данные закрыты
  const data = await ctx.post('/reports/api/report/data', { data: { reportId: 'sales-summary' } })
  expect(data.status()).toBe(401)

  await ctx.dispose()
})

test('префикс /reports: отчёт открывается и данные приходят через /reports/api', async ({ page }) => {
  await page.goto('http://127.0.0.1:8080/reports/report.html?report=sales-summary&from=2026-03-01&to=2026-03-10')
  await expect(page.locator('.meta')).toContainText('строк: 2748')
})

test('черновик нового отчёта: просмотр считает данные по черновику, а не по каркасу', async ({ request, page }) => {
  const id = 'draft-view-e2e'
  const cleanup = () => request.delete(`/reports/api/reports/${id}`)
  await cleanup()
  try {
    // так создаёт новый отчёт админка: каркас без datasets
    const frame = { version: 1, id, title: 'Каркас', blocks: [{ type: 'text', text: 'каркас' }] }
    expect((await request.post('/reports/api/reports', { data: { id, body: frame } })).ok()).toBeTruthy()

    // в черновике — настоящий шаблон со справочником и данными
    const full = {
      version: 2,
      id,
      title: 'Черновик с данными',
      connections: {
        main: {
          kind: 'postgres',
          host: '${PGHOST}',
          port: '${PGPORT}',
          database: '${PGDATABASE}',
          user: '${REP_RO_DB_USER}',
          passwordEnv: 'REP_RO_DB_PASSWORD',
          schema: 'reports_examples'
        }
      },
      params: [{ name: 'region', type: 'select', source: { kind: 'lookup', dataset: 'regions' } }],
      datasets: {
        regions: {
          connection: 'main',
          sql: 'SELECT id::text AS value, name AS label FROM reports_examples.regions ORDER BY name'
        },
        sales: {
          connection: 'main',
          sql: 'SELECT region, amount FROM reports_examples.sales ORDER BY id LIMIT 5'
        }
      },
      blocks: [
        {
          type: 'table',
          dataKey: 'sales',
          columns: [
            { field: 'region', title: 'Регион' },
            { field: 'amount', title: 'Сумма' }
          ]
        }
      ]
    }
    expect((await request.put(`/reports/api/admin/reports/${id}/draft`, { data: { body: full } })).ok()).toBeTruthy()

    // опубликованный каркас без datasets — именно здесь раньше падало
    const bare = await request.post('/reports/api/report/data', { data: { reportId: id, params: {} } })
    expect(bare.status()).toBe(422)

    // просмотр черновика: тело берётся из админского API (localStorage в тесте пуст),
    // данные и справочник — по draft=true
    await page.goto(`/reports/report.html?report=${id}&draft=1`)
    await expect(page.locator('.error')).toHaveCount(0)
    await expect(page.locator('.meta')).toContainText('строк: 5')
    await expect(page.locator('table')).toContainText('Восток')

    // а зритель черновик не увидит
    const viewer = await apiRequest.newContext({
      baseURL: 'http://127.0.0.1:8080',
      storageState: '.auth/viewer.json'
    })
    const denied = await viewer.post('/reports/api/report/data', {
      data: { reportId: id, params: {}, draft: true }
    })
    expect(denied.status()).toBe(403)
    await viewer.dispose()
  } finally {
    // иначе упавший тест оставит отчёт и сломает счётчики каталога
    await cleanup()
  }
})

test('высота блоков: процент от окна пересчитывается, пиксели не зависят от окна', async ({ page }) => {
  await page.setViewportSize({ width: 1400, height: 900 })
  await page.goto('/report.html?report=layout-heights')
  await expect(page.locator('.meta')).toContainText('строк')

  const sections = page.locator('section.block')
  // 0 — текст, 1 — таблица height "60%", 2 — диаграмма height 320, 3 — таблица height 240
  const bigTable = sections.nth(1).locator('.rt')
  const chart = sections.nth(2).locator('.chart')
  const smallTable = sections.nth(3).locator('.rt')

  // 60 % от 900 = 540 px (таблица не растягивается сверх содержимого, строк достаточно)
  expect((await bigTable.boundingBox()).height).toBeGreaterThanOrEqual(520)
  expect((await chart.boundingBox()).height).toBe(320)
  expect((await smallTable.boundingBox()).height).toBeLessThanOrEqual(242)

  // окно ниже: процентный блок становится меньше, пиксельный — нет
  await page.setViewportSize({ width: 1400, height: 700 })
  await expect.poll(async () => (await bigTable.boundingBox()).height).toBeLessThan(520)
  expect((await chart.boundingBox()).height).toBe(320)
})

test('пример с размерами шрифта: зоны и колонки', async ({ page }) => {
  await page.goto('/report.html?report=font-sizes')
  await expect(page.locator('.meta')).toContainText('строк')

  const sizes = await page.evaluate(() => {
    const f = (el) => (el ? `${getComputedStyle(el).fontSize}/${getComputedStyle(el).fontWeight}` : 'нет')
    const tr = document.querySelector('.rt-table tbody tr.detail')
    return {
      header: f(document.querySelector('.rt-table th')),
      region: f(tr?.children[0]),
      product: f(tr?.children[2]),
      amount: f(tr?.children[3]),
      profit: f(tr?.children[4])
    }
  })
  expect(sizes.header).toBe('14px/700')
  expect(sizes.region).toBe('12px/400')
  expect(sizes.product).toBe('11px/400')
  expect(sizes.amount).toBe('16px/700')
  expect(sizes.profit).toBe('10px/400')

  // подытог и ИТОГО — внизу виртуализированной таблицы: сначала прокрутить
  await page.evaluate(() => {
    const rt = document.querySelector('.rt')
    rt.scrollTop = rt.scrollHeight
  })
  const zone = (sel) =>
    page.evaluate((s) => {
      const td = document.querySelector(s)
      return td ? `${getComputedStyle(td).fontSize}/${getComputedStyle(td).fontWeight}` : 'нет'
    }, sel)
  await expect.poll(() => zone('.rt-table tr.grandtotal td')).toBe('18px/800')
  expect(await zone('.rt-table tr.subtotal td')).toBe('13px/600')
})

test('параметры даты и времени: интервал фильтрует события по минутам', async ({ page }) => {
  await page.goto('/report.html?report=ops-events')
  const inputs = page.locator('.params input[type=datetime-local]')
  await expect(inputs).toHaveCount(2)
  await expect(page.locator('.meta')).toContainText('строк: 6')
  await expect(page.locator('.rt-table tbody tr.detail').first()).toContainText('01.03.2026')
  // ИТОГО считается по тем же отфильтрованным строкам
  await expect(page.locator('.rt-table tr.grandtotal').first()).toContainText('3 288')

  // другой интервал — другие события (время внутри дня, а не только дата)
  await inputs.nth(0).fill('2026-03-31T00:00')
  await inputs.nth(0).dispatchEvent('change')
  await inputs.nth(1).fill('2026-03-31T23:59')
  await inputs.nth(1).dispatchEvent('change')
  await expect.poll(() => page.locator('.rt-table tbody tr.detail').first().innerText()).toContain('31.03.2026')
  await expect.poll(() => page.url()).toContain('dt_from=2026-03-31T00')
})

test('форматы дат в колонках: pattern из шаблона и styles.columns', async ({ page }) => {
  await page.goto('/report.html?report=date-formats&from=2026-03-01&to=2026-03-01')
  await expect(page.locator('.meta')).toContainText('строк')

  const cells = await page.evaluate(() => {
    const tr = document.querySelector('.rt-table tbody tr.detail')
    return tr ? [...tr.children].map((td) => td.innerText) : []
  })
  // DD.MM.YYYY HH:mm — формат самой колонки
  expect(cells[0]).toMatch(/^\d{2}\.\d{2}\.\d{4} \d{2}:\d{2}$/)
  // YYYY-MM-DD — «сырой» ISO не должен переформатироваться рендерером
  expect(cells[1]).toBe('2026-03-01')
  // DD.MM — без года
  expect(cells[2]).toBe('01.03')
  // DD/MM/YYYY — задан в styles.columns, а не в колонке
  expect(cells[3]).toBe('01/03/2026')
  expect(cells[4]).toBe('search')
})

test('автозагрузка выключена: данные только по кнопке, во время загрузки — «Загрузка…»', async ({ page }) => {
  const dataCalls = []
  page.on('request', (r) => {
    if (r.url().includes('/api/report/data')) dataCalls.push(r.url())
  })

  await page.goto('/report.html?report=manual-load')
  await expect(page.locator('.no-data')).toContainText('нажмите «Обновить»')
  // блоки данных скрыты, пока данных нет; текстовый блок описания виден
  await expect(page.locator('.rt-table').first()).toBeHidden()
  expect(dataCalls.length).toBe(0)

  const btn = page.locator('.params button[type=submit]')
  await expect(btn).toHaveText('Обновить')
  await btn.click()
  // длительная загрузка видна и на кнопке, и в шапке
  await expect(btn).toHaveText('Загрузка…')
  await expect(btn).toBeDisabled()
  await expect(page.locator('.loader')).toBeVisible()

  await expect(page.locator('.meta').first()).toContainText('строк: 50000', { timeout: 20000 })
  await expect(page.locator('.rt-table').first()).toBeVisible()
  await expect(page.locator('.no-data')).toHaveCount(0)
  expect(dataCalls.length).toBeGreaterThan(0)
})

test('collapsible: false — группы всегда раскрыты, кнопок и шевронов нет', async ({ page }) => {
  await page.goto('/report.html?report=fixed-groups&from=2026-03-01&to=2026-03-10')
  await expect(page.locator('.meta')).toContainText('строк')

  // ни кнопок сворачивания, ни шевронов, ни пустой полосы над таблицей
  await expect(page.getByRole('button', { name: /Свернуть всё|Развернуть всё/ })).toHaveCount(0)
  await expect(page.locator('.rt-table .chev')).toHaveCount(0)
  await expect(page.locator('.tv-bar')).toHaveCount(0)

  // ячейки групп объединены, как и при обычной группировке
  expect(await page.locator('.rt-table tbody td[rowspan]').count()).toBeGreaterThan(0)

  // щелчок по подписи группы ничего не сворачивает
  const meta = page.locator('.meta')
  const before = await meta.innerText()
  await page.locator('.rt-table tbody tr.detail td').first().click()
  await page.waitForTimeout(400)
  expect(await meta.innerText()).toBe(before)
  await expect(page.locator('.rt-table tbody tr.detail').first()).toBeVisible()
})

test('два набора — два стиля: datasets.<ключ>.styles задаёт вид каждой таблицы', async ({ page }) => {
  await page.goto('/report.html?report=two-sets-styles')
  await expect(page.locator('.meta')).toContainText('строк')

  const looks = await page.evaluate(() =>
    [...document.querySelectorAll('.rt-table')].map((t) => {
      const cs = getComputedStyle(t)
      const th = t.querySelector('th')
      const stripe = t.querySelector('tbody tr.stripe td')
      const total = t.querySelector('tbody tr.grandtotal td')
      return {
        font: cs.fontFamily.split(',')[0].replace(/"/g, ''),
        size: cs.fontSize,
        headerBg: getComputedStyle(th).backgroundColor,
        headerColor: getComputedStyle(th).color,
        stripe: stripe ? getComputedStyle(stripe).backgroundColor : null,
        totalBg: total ? getComputedStyle(total).backgroundColor : null
      }
    })
  )

  // продажи — светлая тема с зеброй и синей шапкой
  expect(looks[0]).toEqual({
    font: 'Inter',
    size: '13px',
    headerBg: 'rgb(230, 239, 255)',
    headerColor: 'rgb(18, 50, 79)',
    stripe: 'rgb(242, 246, 255)',
    totalBg: 'rgb(219, 231, 255)'
  })
  // сервисы — тёмная тема, моноширинный шрифт, без зебры, свой итог
  expect(looks[1]).toEqual({
    font: 'monospace',
    size: '12px',
    headerBg: 'rgb(29, 39, 51)',
    headerColor: 'rgb(159, 196, 255)',
    stripe: null,
    totalBg: 'rgb(29, 39, 51)'
  })
})

test('арифметика дат в default: считается от текущего момента, * пересчитывается (ADR-92)', async ({ page }) => {
  // часы браузера ставятся до навигации: воскресенье, 17 мая 2026, 14:37
  await page.clock.install({ time: new Date('2026-05-17T14:37:00') })
  await page.goto('/report.html?report=period-defaults')
  await page.clock.runFor(3000)

  const inputs = page.locator('.params input')
  await expect(inputs.nth(0)).toHaveValue('2026-01-01') // year(*)
  await expect(inputs.nth(1)).toHaveValue('2026-05-17') // *
  await expect(inputs.nth(2)).toHaveValue('2026-05-17T09:37') // *-5h
  await expect(page.locator('.meta')).toContainText('строк')

  // время ушло на три часа: при обновлении * и смещения пересчитываются
  await page.clock.setFixedTime(new Date('2026-05-17T17:37:00'))
  await page.locator('.params button[type=submit]').click()
  await page.clock.runFor(2000)
  await expect(inputs.nth(2)).toHaveValue('2026-05-17T12:37')
  await expect(inputs.nth(1)).toHaveValue('2026-05-17')

  // выбранное вручную значение выражение не перезаписывает
  await inputs.nth(1).fill('2026-03-15')
  await page.clock.runFor(1500)
  await page.clock.setFixedTime(new Date('2026-05-18T10:00:00'))
  await page.locator('.params button[type=submit]').click()
  await page.clock.runFor(2000)
  await expect(inputs.nth(1)).toHaveValue('2026-03-15')
  await expect(inputs.nth(2)).toHaveValue('2026-05-18T05:00')
})

test('мультивыбор: value — число, label — текст; фильтр по int-списку (multi-int)', async ({ page }) => {
  await page.goto('/report.html?report=multi-int')
  await expect(page.locator('.meta')).toContainText('строк: 2')

  // в тексте печатаются подписи выбранных значений, а не числовые value
  await expect(page.locator('.block p').first()).toContainText('Север, Запад')
  const summary = page.locator('[data-testid=ms-trigger] .ms-summary')
  await expect(summary).toHaveText('Север, Запад')

  // справочник отдаёт id как value (число в SQL, строка в форме) и название как label
  await page.locator('[data-testid=ms-trigger]').click()
  const boxes = page.locator('.ms-item input')
  await expect(boxes).toHaveCount(5)
  expect(await boxes.nth(0).getAttribute('value')).toBe('1')
  expect(await page.locator('.ms-item').nth(0).innerText()).toBe('Север')

  // добавляем Центр (id 5) — SQL фильтрует по int-списку через ANY(string_to_array(...)::int[])
  await page.locator('.ms-item input[value="5"]').check()
  await expect(page.locator('.meta')).toContainText('строк: 3')
  await expect(summary).toHaveText('Север, Запад +1')
  await expect(page).toHaveURL(/regions=1%2C3%2C5/)
})


test('Excel: collapsible: false убирает структуру групп, но не подытоги (ADR-94)', async ({ page }) => {
  const levelsOf = async (file) => {
    const zip = await JSZip.loadAsync(readFileSync(file))
    const xml = await zip.file('xl/worksheets/sheet1.xml').async('string')
    return [...xml.matchAll(/outlineLevel="(\d+)"/g)].map((m) => Number(m[1]))
  }

  // группы со сворачиванием: строки получают outline-уровни
  await page.goto('/report.html?report=sales-summary')
  await expect(page.locator('.meta')).toContainText('строк')
  const [grouped] = await Promise.all([
    page.waitForEvent('download'),
    page.locator('button.tool', { hasText: 'Excel' }).first().click()
  ])
  const groupedFile = await grouped.path()
  expect((await levelsOf(groupedFile)).length).toBeGreaterThan(0)

  // collapsible: false: структуры нет, подытоги и ИТОГО остаются
  await page.goto('/report.html?report=fixed-groups&from=2026-01-01&to=2026-11-12')
  await expect(page.locator('.meta')).toContainText('строк')
  const [flat] = await Promise.all([
    page.waitForEvent('download'),
    page.locator('button.tool', { hasText: 'Excel' }).first().click()
  ])
  const flatFile = await flat.path()
  const levels = await levelsOf(flatFile)
  expect(levels.length).toBe(0)
  // и на экране у этого отчёта тоже нет ни кнопок сворачивания, ни шевронов
  await expect(page.getByRole('button', { name: /Свернуть всё|Развернуть всё/ })).toHaveCount(0)
  await expect(page.locator('.rt-table .chev')).toHaveCount(0)
})

test('Excel: плоская таблица без групп не получает сворачивания (дефект sales-drill)', async ({ page }) => {
  const levelsOf = async (file) => {
    const zip = await JSZip.loadAsync(readFileSync(file))
    const xml = await zip.file('xl/worksheets/sheet1.xml').async('string')
    return [...xml.matchAll(/outlineLevel="(\d+)"/g)].map((m) => Number(m[1]))
  }

  await page.goto('/report.html?report=sales-drill')
  await expect(page.locator('.meta')).toContainText('строк')
  // на экране группировки нет — шевронов и кнопок сворачивания быть не должно
  await expect(page.locator('.rt-table .chev')).toHaveCount(0)
  await expect(page.getByRole('button', { name: /Свернуть всё|Развернуть всё/ })).toHaveCount(0)

  const [dl] = await Promise.all([
    page.waitForEvent('download'),
    page.locator('button.tool', { hasText: 'Excel' }).first().click()
  ])
  // и в книге тоже: структуры нет, иначе Excel показывает сворачивание, которого нет на экране
  expect((await levelsOf(await dl.path())).length).toBe(0)
})

test('Excel: плоская таблица выгружается целиком, не только шапка и ИТОГО (ADR-95)', async ({ page }) => {
  const sheetOf = async (file) => {
    const zip = await JSZip.loadAsync(readFileSync(file))
    const sheet = await zip.file('xl/worksheets/sheet1.xml').async('string')
    const sharedFile = zip.file('xl/sharedStrings.xml')
    const shared = sharedFile ? await sharedFile.async('string') : ''
    return { sheet, shared }
  }

  // sales-drill: таблица без группировки, 2746 строк данных
  await page.goto('/report.html?report=sales-drill')
  await expect(page.locator('.meta')).toContainText('строк: 2746')
  const [drill] = await Promise.all([
    page.waitForEvent('download'),
    page.locator('button.tool', { hasText: 'Excel' }).first().click()
  ])
  const { sheet: xml, shared } = await sheetOf(await drill.path())
  const rowCount = (xml.match(/<row /g) || []).length
  expect(rowCount).toBe(2748) // шапка + 2746 данных + ИТОГО
  expect(shared).toContain('ИТОГО')
  // строки данных действительно заполнены: в книге есть регионы и даты периода
  expect(shared).toMatch(/Запад|Восток|Север|Юг|Центр/)

  // плоская таблица без grandTotal: только шапка и строки данных
  await page.goto('/report.html?report=date-formats')
  await expect(page.locator('.meta')).toContainText('строк: 6')
  const [dates] = await Promise.all([
    page.waitForEvent('download'),
    page.locator('button.tool', { hasText: 'Excel' }).first().click()
  ])
  const { sheet: xml2 } = await sheetOf(await dates.path())
  expect((xml2.match(/<row /g) || []).length).toBe(7) // шапка + 6 строк
})

test('раскладка блоков: span в 12-колоночной сетке, одна колонка на узком экране и в печати (ADR-96)', async ({ page }) => {
  const boxes = () =>
    page.evaluate(() =>
      [...document.querySelectorAll('.block')].map((el) => {
        const r = el.getBoundingClientRect()
        return { top: Math.round(r.top), left: Math.round(r.left), width: Math.round(r.width), height: Math.round(r.height) }
      })
    )

  await page.setViewportSize({ width: 1400, height: 1000 })
  await page.goto('/report.html?report=layout-horizontal')
  await expect(page.locator('.meta')).toContainText('строк')

  const wide = await boxes()
  expect(wide).toHaveLength(5)
  // таблица (span 7) и диаграмма (span 5) стоят в одном ряду
  expect(wide[1].top).toBe(wide[2].top)
  expect(wide[1].left).toBeLessThan(wide[2].left)
  expect(wide[1].width).toBeGreaterThan(wide[2].width)
  // второй ряд — две половины одинаковой ширины
  expect(wide[3].top).toBe(wide[4].top)
  expect(Math.abs(wide[3].width - wide[4].width)).toBeLessThan(4)
  expect(wide[3].width).toBeLessThan(wide[1].width)
  // высота в ряду не выравнивается: у каждого блока своя
  expect(wide[1].height).not.toBe(wide[2].height)

  // узкий экран: одна колонка
  await page.setViewportSize({ width: 860, height: 1000 })
  await page.waitForTimeout(500)
  const narrow = await boxes()
  expect(new Set(narrow.map((b) => b.left)).size).toBe(1)
  for (let i = 1; i < narrow.length; i++) expect(narrow[i].top).toBeGreaterThan(narrow[i - 1].top)

  // печать: тоже одна колонка
  await page.setViewportSize({ width: 1400, height: 1000 })
  await page.emulateMedia({ media: 'print' })
  await page.waitForTimeout(400)
  const printed = await boxes()
  expect(new Set(printed.map((b) => b.left)).size).toBe(1)
  for (let i = 1; i < printed.length; i++) expect(printed[i].top).toBeGreaterThanOrEqual(printed[i - 1].top)
})

test('Excel: в отчёте с двумя таблицами каждая идёт на свой лист (ADR-97)', async ({ page }) => {
  await page.goto('/report.html?report=two-sets-styles')
  await expect(page.locator('.meta')).toContainText('строк')

  const [dl] = await Promise.all([
    page.waitForEvent('download'),
    page.locator('button.tool', { hasText: 'Excel' }).first().click()
  ])
  const zip = await JSZip.loadAsync(readFileSync(await dl.path()))
  const book = await zip.file('xl/workbook.xml').async('string')
  const names = [...book.matchAll(/<sheet [^>]*name="([^"]+)"/g)].map((m) => m[1])
  // имя листа в Excel ограничено 31 символом — длинное сокращается с «…»
  expect(names).toEqual(['Продажи по регионам — светлая…', 'Сервисы — тёмная тема', 'Параметры'])

  // оба листа данных заполнены, а не пустые
  const rowsOf = async (file) => ((await zip.file(file).async('string')).match(/<row /g) || []).length
  expect(await rowsOf('xl/worksheets/sheet1.xml')).toBeGreaterThan(1)
  expect(await rowsOf('xl/worksheets/sheet2.xml')).toBeGreaterThan(1)

  // список таблиц в отчёте совпадает с числом листов данных
  const tables = await page.evaluate(() => document.querySelectorAll('.rt-table').length)
  expect(tables).toBe(2)
})

test('печать: таблица начинается на первой странице, а не со второй (ADR-98)', async ({ page }) => {
  // печать не должна закрываться: window.print — пустышка, печатный слой остаётся в DOM
  await page.addInitScript(() => {
    window.print = () => {}
  })
  await page.goto('/report.html?report=finance-print')
  await expect(page.locator('.meta')).toContainText('строк')

  await page.locator('button.tool', { hasText: 'Печать' }).first().click()
  await expect(page.locator('.rt-print')).toHaveCount(1)

  const chunks = await page.evaluate(() => {
    const pageEl = document.querySelector('.page')
    const top = pageEl ? pageEl.getBoundingClientRect().top : 0
    return [...document.querySelectorAll('.rt-print table.rt-table')].map((t) => {
      const r = t.getBoundingClientRect()
      return { rows: t.querySelectorAll('tbody tr').length, bottom: Math.round(r.bottom - top) }
    })
  })

  expect(chunks.length).toBeGreaterThan(3)
  // первый кусок короче остальных — сверху заголовок и текст отчёта
  expect(chunks[0].rows).toBeLessThan(chunks[1].rows)
  expect(chunks[0].rows).toBeGreaterThanOrEqual(8)
  // и вместе с заголовком он укладывается в печатную полосу 660 px — иначе браузер
  // перенёс бы таблицу на следующую страницу и первая осталась бы пустой
  expect(chunks[0].bottom).toBeLessThanOrEqual(660)
})

test('печать: шапка таблицы только вверху страницы, а не в середине (ADR-99)', async ({ page }) => {
  await page.addInitScript(() => {
    window.print = () => {}
  })

  // плоская таблица печатается одним куском: браузер сам рвёт её по страницам,
  // повторяя thead только сверху
  await page.goto('/report.html?report=sales-detail')
  await expect(page.locator('.meta')).toContainText('строк')
  await page.locator('button.tool', { hasText: 'Печать' }).first().click()
  await expect(page.locator('.rt-print')).toHaveCount(1)
  const flat = await page.evaluate(() => {
    const pr = document.querySelector('.rt-print')
    return { tables: pr.querySelectorAll('table.rt-table').length, heads: pr.querySelectorAll('thead').length }
  })
  expect(flat.tables).toBe(1)
  expect(flat.heads).toBe(1)

  // сгруппированная режется на куски, но кусок нельзя разорвать: иначе его шапка
  // окажется в середине страницы
  await page.goto('/report.html?report=finance-print')
  await expect(page.locator('.meta')).toContainText('строк')
  await page.locator('button.tool', { hasText: 'Печать' }).first().click()
  await expect(page.locator('.rt-print')).toHaveCount(1)
  const grouped = await page.evaluate(() => {
    const pr = document.querySelector('.rt-print')
    return {
      tables: pr.querySelectorAll('table.rt-table').length,
      noBreak: pr.querySelectorAll('table.no-break').length
    }
  })
  expect(grouped.tables).toBeGreaterThan(1)
  expect(grouped.noBreak).toBe(grouped.tables)
})

test('условное форматирование: цвета есть в печати и в Excel (ADR-100)', async ({ page }) => {
  await page.addInitScript(() => {
    window.print = () => {}
  })
  await page.goto('/report.html?report=sales-rules')
  await expect(page.locator('.meta')).toContainText('строк')

  // --- печать: правила красят фон и шрифт, браузер просим печатать фоны как есть
  await page.locator('button.tool', { hasText: 'Печать' }).first().click()
  await expect(page.locator('.rt-print')).toHaveCount(1)
  // печатная медиа: цвета и print-color-adjust считаются именно в ней
  await page.emulateMedia({ media: 'print' })
  const printed = await page.evaluate(() => {
    const cells = [...document.querySelectorAll('.rt-print tbody td')]
    const count = (pred) => cells.filter(pred).length
    const probe = cells[0] ? getComputedStyle(cells[0]) : null
    return {
      total: cells.length,
      red: count((td) => getComputedStyle(td).color === 'rgb(180, 35, 24)'),
      warnBg: count((td) => getComputedStyle(td).backgroundColor === 'rgb(255, 244, 214)'),
      rowBg: count((td) => getComputedStyle(td).backgroundColor === 'rgb(255, 247, 230)'),
      colorAdjust: probe ? probe.printColorAdjust || probe.webkitPrintColorAdjust : null
    }
  })
  expect(printed.total).toBeGreaterThan(100)
  expect(printed.red).toBeGreaterThan(0)
  expect(printed.warnBg).toBeGreaterThan(0)
  expect(printed.rowBg).toBeGreaterThan(0)
  expect(printed.colorAdjust).toBe('exact')
  await page.emulateMedia({ media: 'screen' })

  // --- Excel: те же цвета в стилях книги, а не только в правилах на экране
  await page.goto('/report.html?report=sales-rules')
  await expect(page.locator('.meta')).toContainText('строк')
  const [dl] = await Promise.all([
    page.waitForEvent('download'),
    page.locator('button.tool', { hasText: 'Excel' }).first().click()
  ])
  const zip = await JSZip.loadAsync(readFileSync(await dl.path()))
  const styles = await zip.file('xl/styles.xml').async('string')
  // #fff7e6 (вся строка), #fff4d6 (маржа), #b42318 (отрицательная прибыль)
  expect(styles).toContain('FFFFF7E6')
  expect(styles).toContain('FFFFF4D6')
  expect(styles).toContain('FFB42318')
})

test('текстовый блок: данные из запроса и гиперссылки (ADR-101)', async ({ page, request }) => {
  await page.goto('/report.html?report=text-data')
  await expect(page.locator('.meta')).toContainText('строк')

  const text = page.locator('.block p').first()
  // {{data.<колонка>}} из строки набора summary
  await expect(text).toContainText('24723')
  await expect(text).toContainText('2026-03-31')
  // обычные параметры продолжают работать
  await expect(text).toContainText('2026-01-01')

  // ссылки: внутренняя и внешняя, обе в новой вкладке
  const links = text.locator('a')
  await expect(links).toHaveCount(2)
  await expect(links.nth(0)).toHaveAttribute('href', 'report.html?report=sales-drill')
  await expect(links.nth(0)).toHaveAttribute('target', '_blank')
  await expect(links.nth(0)).toHaveAttribute('rel', 'noopener noreferrer')
  await expect(links.nth(1)).toHaveAttribute('href', /^https:\/\/www\.postgresql\.org/)

  // запрос больше одной строки — 422, отчёт с таким блоком не строится
  const many = await request.post('/api/report/text', {
    data: { reportId: 'text-data', dataset: 'sales', params: { from: '2026-01-01', to: '2026-11-14' } }
  })
  expect(many.status()).toBe(422)
  expect((await many.json()).error.code).toBe('TEXT_ROW_MULTIPLE')

  // ноль строк — пустые подстановки, без ошибки
  const empty = await request.post('/api/report/text', {
    data: { reportId: 'text-data', dataset: 'sales', params: { from: '2030-01-01', to: '2030-01-02' } }
  })
  expect(empty.ok()).toBeTruthy()
  expect((await empty.json()).row).toBeNull()
})

test('доступ к отчётам по ролям: каталог скрывает, API отдаёт 403 (ADR-102)', async ({ request, baseURL }) => {
  const ensureRole = async (key, title) => {
    const res = await request.post('/api/admin/access-roles', { data: { key, title } })
    if (res.ok()) return (await res.json()).id
    const list = await (await request.get('/api/admin/access-roles')).json()
    return list.items.find((r) => r.key === key)?.id
  }
  await ensureRole('sales', 'Продажи')
  await ensureRole('finance', 'Финансы')

  const login = 'roles_e2e_user'
  const password = 'roles-pass-123'
  const users = await (await request.get('/api/admin/users')).json()
  for (const u of users.items.filter((x) => x.login === login)) {
    await request.delete(`/api/admin/users/${u.id}`)
  }
  const created = await request.post('/api/admin/users', {
    data: { login, role: 'viewer', password, roles: ['sales'] }
  })
  expect(created.ok()).toBeTruthy()
  const { id } = await created.json()

  const ctx = await apiRequest.newContext({ baseURL })
  expect((await ctx.post('/api/auth/login', { data: { login, password } })).ok()).toBeTruthy()

  // каталог: отчёт с ролью sales виден, с ролью finance — скрыт; без ролей виден всем
  const catalog = (await (await ctx.get('/api/reports')).json()).map((r) => r.id)
  expect(catalog).toContain('sales-summary')
  expect(catalog).toContain('sales-drill')
  expect(catalog).not.toContain('finance-plan-fact')
  expect(catalog).not.toContain('ops-events')

  // прямой доступ к шаблону и к данным — 403 REPORT_FORBIDDEN
  expect((await ctx.get('/api/reports/finance-plan-fact')).status()).toBe(403)
  const denied = await ctx.post('/api/report/data', {
    data: { reportId: 'finance-plan-fact', params: {} }
  })
  expect(denied.status()).toBe(403)
  expect((await denied.json()).error.code).toBe('REPORT_FORBIDDEN')
  // доступный отчёт открывается
  expect((await ctx.get('/api/reports/sales-summary')).ok()).toBeTruthy()

  // администратор видит все отчёты
  const adminCatalog = (await (await request.get('/api/reports')).json()).map((r) => r.id)
  expect(adminCatalog).toContain('finance-plan-fact')

  // выдали вторую роль — отчёт стал доступен
  expect(
    (await request.put(`/api/admin/users/${id}`, {
      data: { role: 'viewer', enabled: true, roles: ['sales', 'finance'] }
    })).ok()
  ).toBeTruthy()
  expect((await ctx.get('/api/reports/finance-plan-fact')).ok()).toBeTruthy()

  await ctx.dispose()
  expect((await request.delete(`/api/admin/users/${id}`)).ok()).toBeTruthy()
})

test('публичный отчёт открывается без входа, остальные — нет (ADR-103)', async ({ browser, baseURL }) => {
  // анонимный контекст: намеренно пустое состояние (в проекте задан storageState admin)
  const anonState = { cookies: [], origins: [] }
  const anonCtx = await apiRequest.newContext({ baseURL, storageState: anonState })
  const anon = await browser.newContext({ baseURL, storageState: anonState })

  // шаблон публичного отчёта отдаётся без входа и помечен public
  const tplRes = await anonCtx.get('/api/reports/public-demo')
  expect(tplRes.ok()).toBeTruthy()
  expect((await tplRes.json()).public).toBe(true)

  // данные и экспорт — тоже
  const data = await anonCtx.post('/api/report/data', {
    data: { reportId: 'public-demo', params: { from: '2026-01-01', to: '2026-11-14' } }
  })
  expect(data.ok()).toBeTruthy()
  expect((await data.json()).rowCount).toBeGreaterThan(0)
  const xlsx = await anonCtx.post('/api/report/export.xlsx', {
    data: { reportId: 'public-demo', params: { from: '2026-01-01', to: '2026-11-14' } }
  })
  expect(xlsx.ok()).toBeTruthy()

  // закрытый отчёт и каталог без входа — 401
  expect((await anonCtx.get('/api/reports/sales-drill')).status()).toBe(401)
  expect((await anonCtx.get('/api/reports')).status()).toBe(401)

  // страница публичного отчёта открывается без сессии и строит таблицу
  const page = await anon.newPage()
  await page.goto('/report.html?report=public-demo')
  await expect(page.locator('h1')).toContainText('Открытый отчёт')
  await expect(page.locator('.meta')).toContainText('строк: 5')
  await expect(page.locator('.rt-table tbody tr.detail')).toHaveCount(5)
  await expect(page.locator('.params')).toHaveCount(1)

  // обычный отчёт без входа уводит на страницу входа
  await page.goto('/report.html?report=sales-drill')
  await page.waitForURL(/login\.html/)

  await anon.close()
  await anonCtx.dispose()
})

test('объединение источников: PostgreSQL + REST в одной таблице (ADR-104)', async ({ page, request }) => {
  // REST-демо (scripts/rest-demo.mjs) должен быть запущен на :8099, сервер — с REP_REST_ALLOW_HOSTS.
  await page.goto('/report.html?report=sales-crm')
  await expect(page.locator('.meta')).toContainText('строк')
  const headers = await page.locator('.rt-table thead th').allInnerTexts()
  expect(headers.join(' | ')).toContain('Менеджер')
  expect(headers.join(' | ')).toContain('Регион')
  // первая строка содержит менеджера из REST-справочника
  const first = await page.locator('.rt-table tbody tr.detail').first().innerText()
  expect(first).toMatch(/Иванов|Петрова|Сидоров|Кузнецова|Смирнов/)

  // агрегация по объединённому набору: 5 регионов + группа без пары (left join)
  const agg = await request.post('/api/report/aggregate', {
    data: {
      reportId: 'sales-crm',
      dataset: 'orders_by_region',
      params: { from: '2025-11-01', to: '2026-11-14' },
      groupBy: ['region_name', 'manager'],
      measures: [{ field: 'amount', agg: 'sum' }]
    }
  })
  expect(agg.ok()).toBeTruthy()
  const body = await agg.json()
  expect(body.rowCount).toBeGreaterThanOrEqual(5)

  // клиентский Excel: колонка правой стороны на месте, строки не потеряны
  const [dl] = await Promise.all([
    page.waitForEvent('download'),
    page.locator('button.tool', { hasText: 'Excel' }).first().click()
  ])
  const zip = await JSZip.loadAsync(readFileSync(await dl.path()))
  const shared = await zip.file('xl/sharedStrings.xml').async('string')
  expect(shared).toContain('Менеджер')
})

test('публичный отчёт с REST: справочник, шаблон и данные без входа (ADR-104)', async ({ browser, baseURL }) => {
  // аноним: пустое состояние, сервер запущен с REP_REST_ALLOW_HOSTS и CRM_BASE_URL
  const state = { cookies: [], origins: [] }
  const ctx = await apiRequest.newContext({ baseURL, storageState: state })

  // параметр-справочник приходит из REST-набора
  const lookups = await ctx.get('/api/reports/crm-public-filter/lookups/region')
  expect(lookups.ok()).toBeTruthy()
  const items = await lookups.json()
  expect(items.length).toBeGreaterThanOrEqual(5)
  expect(items.map((i) => i.label)).toContain('Север')

  // шаблон отдаётся без входа и без SQL/заголовков источников
  const tpl = await ctx.get('/api/reports/crm-public-filter')
  expect(tpl.ok()).toBeTruthy()
  const raw = await tpl.text()
  expect(raw).not.toContain('passwordEnv')
  expect(raw).not.toContain('Authorization')
  expect(raw).not.toContain('"sql"')

  // данные объединённого набора — тоже без входа
  const data = await ctx.post('/api/report/data', {
    data: {
      reportId: 'crm-public-filter',
      dataset: 'orders_by_region',
      params: { region: '1', from: '2025-11-01', to: '2026-11-14' }
    }
  })
  expect(data.ok()).toBeTruthy()
  const body = await data.json()
  expect(body.rowCount).toBeGreaterThan(0)
  expect(body.columns.map((c) => c.name)).toContain('region_name')
  expect(body.columns.map((c) => c.name)).toContain('manager')

  // страница открывается без сессии и строит таблицу
  const page = await (await browser.newContext({ baseURL, storageState: state })).newPage()
  await page.goto('/report.html?report=crm-public-filter')
  await expect(page.locator('h1')).toContainText('Публичный отчёт')
  await expect(page.locator('.rt-table tbody tr.detail').first()).toBeVisible()

  await page.context().close()
  await ctx.dispose()
})

test('постраничный REST: набор собирает страницы до неполной (ADR-106)', async ({ page, request }) => {
  const res = await request.post('/api/report/data', {
    data: { reportId: 'crm-paged', dataset: 'rows', params: {} }
  })
  expect(res.ok()).toBeTruthy()
  const body = await res.json()
  // 2500 записей при size=1000: страницы 1000 + 1000 + 500
  expect(body.rowCount).toBe(2500)
  expect(body.rows[0].id).toBe(1)
  expect(body.rows.at(-1).id).toBe(2500)

  await page.goto('/report.html?report=crm-paged')
  await expect(page.locator('.meta')).toContainText('строк: 2500')
  await expect(page.locator('.rt-table tbody tr.detail').first()).toBeVisible()
})

// ADR-111: offset-режим пагинации — значение = start + страница × size.
test('постраничный REST в режиме offset: страницы идут по смещению (ADR-111)', async ({ page, request }) => {
  const res = await request.post('/api/report/data', {
    data: { reportId: 'crm-paged-offset', dataset: 'rows', params: {} }
  })
  expect(res.ok()).toBeTruthy()
  const body = await res.json()
  // 2500 записей при size=1000 и start=0: offset=0, 1000, 2000
  expect(body.rowCount).toBe(2500)
  expect(body.rows[0].id).toBe(1)
  expect(body.rows.at(-1).id).toBe(2500)

  await page.goto('/report.html?report=crm-paged-offset')
  await expect(page.locator('.meta')).toContainText('строк: 2500')
  await expect(page.locator('.rt-table tbody tr.detail').first()).toBeVisible()
})
