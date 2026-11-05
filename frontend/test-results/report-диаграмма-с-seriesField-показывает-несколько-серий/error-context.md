# Instructions

- Following Playwright test failed.
- Explain why, be concise, respect Playwright best practices.
- Provide a snippet of code with the fix, if possible.

# Test info

- Name: report.spec.js >> диаграмма с seriesField показывает несколько серий
- Location: e2e/report.spec.js:209:1

# Error details

```
Error: expect(received).toBeGreaterThan(expected)

Expected: > 1
Received:   0
```

# Page snapshot

```yaml
- generic [ref=e3]:
  - banner [ref=e4]:
    - heading "Сводная таблица продаж" [level=1] [ref=e5]
    - generic [ref=e6]: "строк: 2748 · запрос: 123 мс · шаблон v1"
  - generic [ref=e7]:
    - generic [ref=e8]:
      - generic [ref=e9]: Период с
      - textbox "Период с" [ref=e10]: 2026-03-01
    - generic [ref=e11]:
      - generic [ref=e12]: по
      - textbox "по" [ref=e13]: 2026-03-10
    - button "Обновить" [ref=e14] [cursor=pointer]
  - paragraph [ref=e16]: Отчёт за период 2026-03-01 — 2026-03-10
  - generic [ref=e17]:
    - generic [ref=e18]:
      - button "Свернуть всё" [ref=e19] [cursor=pointer]
      - button "Развернуть всё" [ref=e20] [cursor=pointer]
      - generic [ref=e21]: 0 свёрнуто
    - table [ref=e23]:
      - rowgroup [ref=e31]:
        - row [ref=e32]:
          - columnheader "Регион" [ref=e33]
          - columnheader "Категория" [ref=e34]
          - columnheader "Продукт" [ref=e35]
          - columnheader "Финансы" [ref=e36]
          - columnheader "Рентабельность" [ref=e37]
        - row [ref=e38]:
          - columnheader "Сумма" [ref=e39]
          - columnheader "Прибыль" [ref=e40]
      - rowgroup [ref=e41]:
        - row [ref=e42]:
          - cell "▾ Восток" [ref=e43]
          - cell "▾ Аксессуары" [ref=e44]
          - cell "Товар-12" [ref=e45]
          - cell "58 300" [ref=e46]
          - cell "9 675" [ref=e47]
          - cell "—" [ref=e48]
        - row [ref=e49]:
          - cell "Товар-12" [ref=e50]
          - cell "80 300" [ref=e51]
          - cell "19 175" [ref=e52]
          - cell "—" [ref=e53]
        - row [ref=e54]:
          - cell "Товар-12" [ref=e55]
          - cell "12 300" [ref=e56]
          - cell "1 175" [ref=e57]
          - cell "—" [ref=e58]
        - row [ref=e59]:
          - cell "Товар-12" [ref=e60]
          - cell "72 300" [ref=e61]
          - cell "16 175" [ref=e62]
          - cell "—" [ref=e63]
        - row [ref=e64]:
          - cell "Товар-12" [ref=e65]
          - cell "94 300" [ref=e66]
          - cell "20 675" [ref=e67]
          - cell "—" [ref=e68]
        - row [ref=e69]:
          - cell "Товар-12" [ref=e70]
          - cell "26 300" [ref=e71]
          - cell "2 675" [ref=e72]
          - cell "—" [ref=e73]
        - row [ref=e74]:
          - cell "Товар-12" [ref=e75]
          - cell "48 300" [ref=e76]
          - cell "7 175" [ref=e77]
          - cell "—" [ref=e78]
        - row [ref=e79]:
          - cell "Товар-12" [ref=e80]
          - cell "70 300" [ref=e81]
          - cell "16 675" [ref=e82]
          - cell "—" [ref=e83]
        - row [ref=e84]:
          - cell "Товар-12" [ref=e85]
          - cell "40 300" [ref=e86]
          - cell "9 175" [ref=e87]
          - cell "—" [ref=e88]
        - row [ref=e89]:
          - cell "Товар-12" [ref=e90]
          - cell "62 300" [ref=e91]
          - cell "13 675" [ref=e92]
          - cell "—" [ref=e93]
        - row [ref=e94]:
          - cell "Товар-12" [ref=e95]
          - cell "84 300" [ref=e96]
          - cell "18 175" [ref=e97]
          - cell "—" [ref=e98]
        - row [ref=e99]:
          - cell "Товар-12" [ref=e100]
          - cell "16 300" [ref=e101]
          - cell "175" [ref=e102]
          - cell "—" [ref=e103]
        - row [ref=e104]:
          - cell "Товар-12" [ref=e105]
          - cell "38 300" [ref=e106]
          - cell "4 675" [ref=e107]
          - cell "—" [ref=e108]
        - row [ref=e109]:
          - cell "Товар-12" [ref=e110]
          - cell "98 300" [ref=e111]
          - cell "19 675" [ref=e112]
          - cell "—" [ref=e113]
        - row [ref=e114]:
          - cell "Товар-12" [ref=e115]
          - cell "30 300" [ref=e116]
          - cell "6 675" [ref=e117]
          - cell "—" [ref=e118]
        - row [ref=e119]:
          - cell "Товар-12" [ref=e120]
          - cell "52 300" [ref=e121]
          - cell "11 175" [ref=e122]
          - cell "—" [ref=e123]
        - row [ref=e124]:
          - cell "Товар-12" [ref=e125]
          - cell "74 300" [ref=e126]
          - cell "15 675" [ref=e127]
          - cell "—" [ref=e128]
        - row [ref=e129]:
          - cell "Товар-12" [ref=e130]
          - cell "96 300" [ref=e131]
          - cell "20 175" [ref=e132]
          - cell "—" [ref=e133]
        - row [ref=e134]:
          - cell "Товар-12" [ref=e135]
          - cell "66 300" [ref=e136]
          - cell "12 675" [ref=e137]
          - cell "—" [ref=e138]
        - row [ref=e139]:
          - cell "Товар-12" [ref=e140]
          - cell "88 300" [ref=e141]
          - cell "17 175" [ref=e142]
          - cell "—" [ref=e143]
        - row [ref=e144]:
          - cell "Товар-12" [ref=e145]
          - cell "20 300" [ref=e146]
          - cell "4 175" [ref=e147]
          - cell "—" [ref=e148]
        - row [ref=e149]:
          - cell "Товар-12" [ref=e150]
          - cell "42 300" [ref=e151]
          - cell "8 675" [ref=e152]
          - cell "—" [ref=e153]
        - row [ref=e154]:
          - cell "Товар-12" [ref=e155]
          - cell "64 300" [ref=e156]
          - cell "13 175" [ref=e157]
          - cell "—" [ref=e158]
        - row [ref=e159]:
          - cell "Товар-12" [ref=e160]
          - cell "34 300" [ref=e161]
          - cell "5 675" [ref=e162]
          - cell "—" [ref=e163]
        - row [ref=e164]:
          - cell "Товар-12" [ref=e165]
          - cell "56 300" [ref=e166]
          - cell "10 175" [ref=e167]
          - cell "—" [ref=e168]
        - row [ref=e169]:
          - cell "Товар-12" [ref=e170]
          - cell "78 300" [ref=e171]
          - cell "14 675" [ref=e172]
          - cell "—" [ref=e173]
        - row [ref=e174]:
          - cell "Товар-12" [ref=e175]
          - cell "10 300" [ref=e176]
          - cell "1 675" [ref=e177]
          - cell "—" [ref=e178]
        - row [ref=e179]:
          - cell "Товар-12" [ref=e180]
          - cell "32 300" [ref=e181]
          - cell "6 175" [ref=e182]
          - cell "—" [ref=e183]
        - row [ref=e184]:
          - cell "Товар-12" [ref=e185]
          - cell "92 300" [ref=e186]
          - cell "21 175" [ref=e187]
          - cell "—" [ref=e188]
        - row [ref=e189]:
          - cell "Товар-12" [ref=e190]
          - cell "24 300" [ref=e191]
          - cell "3 175" [ref=e192]
          - cell "—" [ref=e193]
        - row [ref=e194]:
          - cell "Товар-12" [ref=e195]
          - cell "46 300" [ref=e196]
          - cell "7 675" [ref=e197]
          - cell "—" [ref=e198]
        - row [ref=e199]:
          - cell "Товар-12" [ref=e200]
          - cell "68 300" [ref=e201]
          - cell "12 175" [ref=e202]
          - cell "—" [ref=e203]
        - row [ref=e204]:
          - cell "Товар-12" [ref=e205]
          - cell "90 300" [ref=e206]
          - cell "21 675" [ref=e207]
          - cell "—" [ref=e208]
        - row [ref=e209]:
          - cell "Товар-12" [ref=e210]
          - cell "60 300" [ref=e211]
          - cell "14 175" [ref=e212]
          - cell "—" [ref=e213]
        - row [ref=e214]:
          - cell [ref=e215]
  - heading "Доля категорий в сумме продаж" [level=2] [ref=e223]
  - heading "Сумма по категориям в разрезе регионов" [level=2] [ref=e229]
```

# Test source

```ts
  114 |   // клик по строке итога возвращает детали
  115 |   await total.locator('.chev').first().click()
  116 |   await expect(page.locator('.rt-table tbody tr:not(.spacer)').first()).not.toContainText('Итого')
  117 |   const heightExpanded = await page.locator('.rt').evaluate((el) => el.scrollHeight)
  118 |   expect(heightExpanded).toBe(heightBefore)
  119 | })
  120 | 
  121 | test('сворачивание категории внутри региона', async ({ page }) => {
  122 |   await page.goto(`${BASE}&from=2026-03-01&to=2026-03-10`)
  123 |   const heightBefore = await page.locator('.rt').evaluate((el) => el.scrollHeight)
  124 | 
  125 |   // второй шеврон в строке — колонка «Категория»
  126 |   await page.locator('.rt-table tbody tr:not(.spacer)').first().locator('.chev').nth(1).click()
  127 | 
  128 |   const total = page.locator('.rt-table tbody tr:not(.spacer)').first()
  129 |   await expect(total).toContainText('Итого')
  130 |   await expect(total.locator('td').nth(1)).not.toHaveText('')
  131 |   const heightCollapsed = await page.locator('.rt').evaluate((el) => el.scrollHeight)
  132 |   expect(heightCollapsed).toBeLessThan(heightBefore)
  133 | 
  134 |   // разворачиваем обратно именно категорию (второй шеврон строки итога)
  135 |   await total.locator('.chev').nth(1).click()
  136 |   await expect(page.locator('.rt-table tbody tr:not(.spacer)').first()).not.toContainText('Итого')
  137 | })
  138 | 
  139 | test('свёрнутая категория не меняет шеврон региона (и после прокрутки)', async ({ page }) => {
  140 |   await page.goto(`${BASE}&from=2026-03-01&to=2026-03-10`)
  141 | 
  142 |   const first = page.locator('.rt-table tbody tr:not(.spacer)').first()
  143 |   const regionBefore = (await first.locator('.chev').nth(0).innerText()).trim()
  144 |   await first.locator('.chev').nth(1).click()
  145 | 
  146 |   const row = page.locator('.rt-table tbody tr:not(.spacer)').first()
  147 |   await expect(row.locator('.chev').nth(0)).toHaveText(regionBefore) // регион раскрыт
  148 |   await expect(row.locator('.chev').nth(1)).toHaveText('▸')          // категория свёрнута
  149 | 
  150 |   await page.locator('.rt').evaluate((el) => { el.scrollTop = 20000 })
  151 |   await page.waitForTimeout(300)
  152 |   const scrolled = page.locator('.rt-table tbody tr:not(.spacer)').first()
  153 |   await expect(scrolled.locator('.chev').nth(0)).toHaveText(regionBefore)
  154 | })
  155 | 
  156 | test('свёрнутая категория не перекрашивает ячейку региона (и после прокрутки)', async ({ page }) => {
  157 |   await page.goto(`${BASE}&from=2026-03-01&to=2026-03-10`)
  158 |   const bg = (loc) => loc.evaluate((el) => getComputedStyle(el).backgroundColor)
  159 | 
  160 |   const firstRow = page.locator('.rt-table tbody tr:not(.spacer)').first()
  161 |   await firstRow.locator('.chev').nth(1).click()
  162 | 
  163 |   const row = page.locator('.rt-table tbody tr.subtotal').first()
  164 |   const regionCell = row.locator('td').nth(0)
  165 |   const categoryCell = row.locator('td').nth(1)
  166 | 
  167 |   const GREY = 'rgb(247, 249, 252)' // фон строки итога
  168 |   // ячейка-предок осталась обычной (белая/прозрачная), а подпись итога — подсвечена
  169 |   expect(await bg(regionCell)).not.toBe(GREY)
  170 |   expect(await bg(categoryCell)).toBe(GREY)
  171 | 
  172 |   await page.locator('.rt').evaluate((el) => { el.scrollTop = 20000 })
  173 |   await page.waitForTimeout(300)
  174 |   const scrolledRegion = page.locator('.rt-table tbody tr:not(.spacer)').first().locator('td').nth(0)
  175 |   expect(await bg(scrolledRegion)).not.toBe(GREY)
  176 | })
  177 | 
  178 | test('«свернуть всё» оставляет только итоги регионов, «развернуть всё» возвращает детали', async ({ page }) => {
  179 |   await page.goto(`${BASE}&from=2026-03-01&to=2026-03-10`)
  180 |   const heightBefore = await page.locator('.rt').evaluate((el) => el.scrollHeight)
  181 | 
  182 |   await page.getByRole('button', { name: 'Свернуть всё' }).click()
  183 |   await expect(page.locator('.rt-table tbody tr.subtotal').first()).toContainText('Итого')
  184 |   const heightCollapsed = await page.locator('.rt').evaluate((el) => el.scrollHeight)
  185 |   expect(heightCollapsed).toBeLessThan(heightBefore / 2)
  186 | 
  187 |   await page.getByRole('button', { name: 'Развернуть всё' }).click()
  188 |   await expect(page.locator('.rt-table tbody tr.detail').first()).toBeVisible()
  189 |   const heightExpanded = await page.locator('.rt').evaluate((el) => el.scrollHeight)
  190 |   expect(heightExpanded).toBe(heightBefore)
  191 | })
  192 | 
  193 | test('диаграмма получает агрегаты из того же движка', async ({ page }) => {
  194 |   await page.goto(`${BASE}&from=2026-03-01&to=2026-03-10`)
  195 |   expect(await page.locator('.chart canvas').count()).toBeGreaterThanOrEqual(2)
  196 | 
  197 |   const info = await page.evaluate(() => window.__chart)
  198 |   expect(['line', 'bar', 'pie']).toContain(info.type)
  199 |   expect(info.series).toBeGreaterThanOrEqual(1)
  200 | 
  201 |   // подпись оси отформатирована по-русски (пробел как разделитель тысяч)
  202 |   const labels = await page.evaluate(() => {
  203 |     const canvas = document.querySelector('.chart canvas')
  204 |     return canvas ? canvas.width > 0 : false
  205 |   })
  206 |   expect(labels).toBe(true)
  207 | })
  208 | 
  209 | test('диаграмма с seriesField показывает несколько серий', async ({ page }) => {
  210 |   await page.goto(`${BASE}&from=2026-03-01&to=2026-03-10`)
  211 |   const info = await page.evaluate(() => window.__chart)
  212 |   // последний блок шаблона — столбцы по категориям в разрезе регионов
  213 |   expect(info.type).toBe('bar')
> 214 |   expect(info.series).toBeGreaterThan(1)
      |                       ^ Error: expect(received).toBeGreaterThan(expected)
  215 | })
  216 | 
  217 | test('скриншот диаграммы стабилен между прогонами', async ({ page }) => {
  218 |   await page.goto(`${BASE}&from=2026-03-01&to=2026-03-10`)
  219 |   const chart = page.locator('.chart-box').first()
  220 |   await chart.scrollIntoViewIfNeeded()
  221 |   await expect(chart).toHaveScreenshot('chart-line.png')
  222 | })
  223 | 
```