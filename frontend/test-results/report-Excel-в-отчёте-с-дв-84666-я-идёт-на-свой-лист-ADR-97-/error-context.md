# Instructions

- Following Playwright test failed.
- Explain why, be concise, respect Playwright best practices.
- Provide a snippet of code with the fix, if possible.

# Test info

- Name: report.spec.js >> Excel: в отчёте с двумя таблицами каждая идёт на свой лист (ADR-97)
- Location: e2e/report.spec.js:2627:1

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

# Test source

```ts
  2529 | 
  2530 | test('Excel: плоская таблица без групп не получает сворачивания (дефект sales-drill)', async ({ page }) => {
  2531 |   const levelsOf = async (file) => {
  2532 |     const zip = await JSZip.loadAsync(readFileSync(file))
  2533 |     const xml = await zip.file('xl/worksheets/sheet1.xml').async('string')
  2534 |     return [...xml.matchAll(/outlineLevel="(\d+)"/g)].map((m) => Number(m[1]))
  2535 |   }
  2536 | 
  2537 |   await page.goto('/report.html?report=sales-drill')
  2538 |   await expect(page.locator('.meta')).toContainText('строк')
  2539 |   // на экране группировки нет — шевронов и кнопок сворачивания быть не должно
  2540 |   await expect(page.locator('.rt-table .chev')).toHaveCount(0)
  2541 |   await expect(page.getByRole('button', { name: /Свернуть всё|Развернуть всё/ })).toHaveCount(0)
  2542 | 
  2543 |   const [dl] = await Promise.all([
  2544 |     page.waitForEvent('download'),
  2545 |     page.locator('button.tool', { hasText: 'Excel' }).first().click()
  2546 |   ])
  2547 |   // и в книге тоже: структуры нет, иначе Excel показывает сворачивание, которого нет на экране
  2548 |   expect((await levelsOf(await dl.path())).length).toBe(0)
  2549 | })
  2550 | 
  2551 | test('Excel: плоская таблица выгружается целиком, не только шапка и ИТОГО (ADR-95)', async ({ page }) => {
  2552 |   const sheetOf = async (file) => {
  2553 |     const zip = await JSZip.loadAsync(readFileSync(file))
  2554 |     const sheet = await zip.file('xl/worksheets/sheet1.xml').async('string')
  2555 |     const sharedFile = zip.file('xl/sharedStrings.xml')
  2556 |     const shared = sharedFile ? await sharedFile.async('string') : ''
  2557 |     return { sheet, shared }
  2558 |   }
  2559 | 
  2560 |   // sales-drill: таблица без группировки, 2746 строк данных
  2561 |   await page.goto('/report.html?report=sales-drill')
  2562 |   await expect(page.locator('.meta')).toContainText('строк: 2746')
  2563 |   const [drill] = await Promise.all([
  2564 |     page.waitForEvent('download'),
  2565 |     page.locator('button.tool', { hasText: 'Excel' }).first().click()
  2566 |   ])
  2567 |   const { sheet: xml, shared } = await sheetOf(await drill.path())
  2568 |   const rowCount = (xml.match(/<row /g) || []).length
  2569 |   expect(rowCount).toBe(2748) // шапка + 2746 данных + ИТОГО
  2570 |   expect(shared).toContain('ИТОГО')
  2571 |   // строки данных действительно заполнены: в книге есть регионы и даты периода
  2572 |   expect(shared).toMatch(/Запад|Восток|Север|Юг|Центр/)
  2573 | 
  2574 |   // плоская таблица без grandTotal: только шапка и строки данных
  2575 |   await page.goto('/report.html?report=date-formats')
  2576 |   await expect(page.locator('.meta')).toContainText('строк: 6')
  2577 |   const [dates] = await Promise.all([
  2578 |     page.waitForEvent('download'),
  2579 |     page.locator('button.tool', { hasText: 'Excel' }).first().click()
  2580 |   ])
  2581 |   const { sheet: xml2 } = await sheetOf(await dates.path())
  2582 |   expect((xml2.match(/<row /g) || []).length).toBe(7) // шапка + 6 строк
  2583 | })
  2584 | 
  2585 | test('раскладка блоков: span в 12-колоночной сетке, одна колонка на узком экране и в печати (ADR-96)', async ({ page }) => {
  2586 |   const boxes = () =>
  2587 |     page.evaluate(() =>
  2588 |       [...document.querySelectorAll('.block')].map((el) => {
  2589 |         const r = el.getBoundingClientRect()
  2590 |         return { top: Math.round(r.top), left: Math.round(r.left), width: Math.round(r.width), height: Math.round(r.height) }
  2591 |       })
  2592 |     )
  2593 | 
  2594 |   await page.setViewportSize({ width: 1400, height: 1000 })
  2595 |   await page.goto('/report.html?report=layout-horizontal')
  2596 |   await expect(page.locator('.meta')).toContainText('строк')
  2597 | 
  2598 |   const wide = await boxes()
  2599 |   expect(wide).toHaveLength(5)
  2600 |   // таблица (span 7) и диаграмма (span 5) стоят в одном ряду
  2601 |   expect(wide[1].top).toBe(wide[2].top)
  2602 |   expect(wide[1].left).toBeLessThan(wide[2].left)
  2603 |   expect(wide[1].width).toBeGreaterThan(wide[2].width)
  2604 |   // второй ряд — две половины одинаковой ширины
  2605 |   expect(wide[3].top).toBe(wide[4].top)
  2606 |   expect(Math.abs(wide[3].width - wide[4].width)).toBeLessThan(4)
  2607 |   expect(wide[3].width).toBeLessThan(wide[1].width)
  2608 |   // высота в ряду не выравнивается: у каждого блока своя
  2609 |   expect(wide[1].height).not.toBe(wide[2].height)
  2610 | 
  2611 |   // узкий экран: одна колонка
  2612 |   await page.setViewportSize({ width: 860, height: 1000 })
  2613 |   await page.waitForTimeout(500)
  2614 |   const narrow = await boxes()
  2615 |   expect(new Set(narrow.map((b) => b.left)).size).toBe(1)
  2616 |   for (let i = 1; i < narrow.length; i++) expect(narrow[i].top).toBeGreaterThan(narrow[i - 1].top)
  2617 | 
  2618 |   // печать: тоже одна колонка
  2619 |   await page.setViewportSize({ width: 1400, height: 1000 })
  2620 |   await page.emulateMedia({ media: 'print' })
  2621 |   await page.waitForTimeout(400)
  2622 |   const printed = await boxes()
  2623 |   expect(new Set(printed.map((b) => b.left)).size).toBe(1)
  2624 |   for (let i = 1; i < printed.length; i++) expect(printed[i].top).toBeGreaterThanOrEqual(printed[i - 1].top)
  2625 | })
  2626 | 
  2627 | test('Excel: в отчёте с двумя таблицами каждая идёт на свой лист (ADR-97)', async ({ page }) => {
  2628 |   await page.goto('/report.html?report=two-sets-styles')
> 2629 |   await expect(page.locator('.meta')).toContainText('строк')
       |                                       ^ Error: expect(locator).toContainText(expected) failed
  2630 | 
  2631 |   const [dl] = await Promise.all([
  2632 |     page.waitForEvent('download'),
  2633 |     page.locator('button.tool', { hasText: 'Excel' }).first().click()
  2634 |   ])
  2635 |   const zip = await JSZip.loadAsync(readFileSync(await dl.path()))
  2636 |   const book = await zip.file('xl/workbook.xml').async('string')
  2637 |   const names = [...book.matchAll(/<sheet [^>]*name="([^"]+)"/g)].map((m) => m[1])
  2638 |   // имя листа в Excel ограничено 31 символом — длинное сокращается с «…»
  2639 |   expect(names).toEqual(['Продажи по регионам — светлая…', 'Сервисы — тёмная тема', 'Параметры'])
  2640 | 
  2641 |   // оба листа данных заполнены, а не пустые
  2642 |   const rowsOf = async (file) => ((await zip.file(file).async('string')).match(/<row /g) || []).length
  2643 |   expect(await rowsOf('xl/worksheets/sheet1.xml')).toBeGreaterThan(1)
  2644 |   expect(await rowsOf('xl/worksheets/sheet2.xml')).toBeGreaterThan(1)
  2645 | 
  2646 |   // список таблиц в отчёте совпадает с числом листов данных
  2647 |   const tables = await page.evaluate(() => document.querySelectorAll('.rt-table').length)
  2648 |   expect(tables).toBe(2)
  2649 | })
  2650 | 
  2651 | test('печать: таблица начинается на первой странице, а не со второй (ADR-98)', async ({ page }) => {
  2652 |   // печать не должна закрываться: window.print — пустышка, печатный слой остаётся в DOM
  2653 |   await page.addInitScript(() => {
  2654 |     window.print = () => {}
  2655 |   })
  2656 |   await page.goto('/report.html?report=finance-print')
  2657 |   await expect(page.locator('.meta')).toContainText('строк')
  2658 | 
  2659 |   await page.locator('button.tool', { hasText: 'Печать' }).first().click()
  2660 |   await expect(page.locator('.rt-print')).toHaveCount(1)
  2661 | 
  2662 |   const chunks = await page.evaluate(() => {
  2663 |     const pageEl = document.querySelector('.page')
  2664 |     const top = pageEl ? pageEl.getBoundingClientRect().top : 0
  2665 |     return [...document.querySelectorAll('.rt-print table.rt-table')].map((t) => {
  2666 |       const r = t.getBoundingClientRect()
  2667 |       return { rows: t.querySelectorAll('tbody tr').length, bottom: Math.round(r.bottom - top) }
  2668 |     })
  2669 |   })
  2670 | 
  2671 |   expect(chunks.length).toBeGreaterThan(3)
  2672 |   // первый кусок короче остальных — сверху заголовок и текст отчёта
  2673 |   expect(chunks[0].rows).toBeLessThan(chunks[1].rows)
  2674 |   expect(chunks[0].rows).toBeGreaterThanOrEqual(8)
  2675 |   // и вместе с заголовком он укладывается в печатную полосу 660 px — иначе браузер
  2676 |   // перенёс бы таблицу на следующую страницу и первая осталась бы пустой
  2677 |   expect(chunks[0].bottom).toBeLessThanOrEqual(660)
  2678 | })
  2679 | 
  2680 | test('печать: шапка таблицы только вверху страницы, а не в середине (ADR-99)', async ({ page }) => {
  2681 |   await page.addInitScript(() => {
  2682 |     window.print = () => {}
  2683 |   })
  2684 | 
  2685 |   // плоская таблица печатается одним куском: браузер сам рвёт её по страницам,
  2686 |   // повторяя thead только сверху
  2687 |   await page.goto('/report.html?report=sales-detail')
  2688 |   await expect(page.locator('.meta')).toContainText('строк')
  2689 |   await page.locator('button.tool', { hasText: 'Печать' }).first().click()
  2690 |   await expect(page.locator('.rt-print')).toHaveCount(1)
  2691 |   const flat = await page.evaluate(() => {
  2692 |     const pr = document.querySelector('.rt-print')
  2693 |     return { tables: pr.querySelectorAll('table.rt-table').length, heads: pr.querySelectorAll('thead').length }
  2694 |   })
  2695 |   expect(flat.tables).toBe(1)
  2696 |   expect(flat.heads).toBe(1)
  2697 | 
  2698 |   // сгруппированная режется на куски, но кусок нельзя разорвать: иначе его шапка
  2699 |   // окажется в середине страницы
  2700 |   await page.goto('/report.html?report=finance-print')
  2701 |   await expect(page.locator('.meta')).toContainText('строк')
  2702 |   await page.locator('button.tool', { hasText: 'Печать' }).first().click()
  2703 |   await expect(page.locator('.rt-print')).toHaveCount(1)
  2704 |   const grouped = await page.evaluate(() => {
  2705 |     const pr = document.querySelector('.rt-print')
  2706 |     return {
  2707 |       tables: pr.querySelectorAll('table.rt-table').length,
  2708 |       noBreak: pr.querySelectorAll('table.no-break').length
  2709 |     }
  2710 |   })
  2711 |   expect(grouped.tables).toBeGreaterThan(1)
  2712 |   expect(grouped.noBreak).toBe(grouped.tables)
  2713 | })
  2714 | 
  2715 | test('условное форматирование: цвета есть в печати и в Excel (ADR-100)', async ({ page }) => {
  2716 |   await page.addInitScript(() => {
  2717 |     window.print = () => {}
  2718 |   })
  2719 |   await page.goto('/report.html?report=sales-rules')
  2720 |   await expect(page.locator('.meta')).toContainText('строк')
  2721 | 
  2722 |   // --- печать: правила красят фон и шрифт, браузер просим печатать фоны как есть
  2723 |   await page.locator('button.tool', { hasText: 'Печать' }).first().click()
  2724 |   await expect(page.locator('.rt-print')).toHaveCount(1)
  2725 |   // печатная медиа: цвета и print-color-adjust считаются именно в ней
  2726 |   await page.emulateMedia({ media: 'print' })
  2727 |   const printed = await page.evaluate(() => {
  2728 |     const cells = [...document.querySelectorAll('.rt-print tbody td')]
  2729 |     const count = (pred) => cells.filter(pred).length
```