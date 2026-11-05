# Instructions

- Following Playwright test failed.
- Explain why, be concise, respect Playwright best practices.
- Provide a snippet of code with the fix, if possible.

# Test info

- Name: report.spec.js >> диаграмма отрисована на canvas
- Location: e2e/report.spec.js:50:1

# Error details

```
Error: expect(locator).toHaveCount(expected) failed

Locator:  locator('.chart canvas')
Expected: 1
Received: 2
Timeout:  5000ms

Call log:
  - Expect "toHaveCount" locator('.chart canvas') with timeout 5000ms
  - waiting for locator('.chart canvas')
    12 × locator resolved to 2 elements
       - unexpected value "2"

```

# Page snapshot

```yaml
- generic [ref=e3]:
  - banner [ref=e4]:
    - heading "Сводная таблица продаж" [level=1] [ref=e5]
    - generic [ref=e6]: "строк: 24723 · запрос: 438 мс · шаблон v1"
  - generic [ref=e7]:
    - generic [ref=e8]:
      - generic [ref=e9]: Период с
      - textbox "Период с" [ref=e10]: 2026-01-01
    - generic [ref=e11]:
      - generic [ref=e12]: по
      - textbox "по" [ref=e13]: 2026-03-31
    - button "Обновить" [ref=e14] [cursor=pointer]
  - paragraph [ref=e16]: Отчёт за период 2026-01-01 — 2026-03-31
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
          - cell "64 300" [ref=e46]
          - cell "13 175" [ref=e47]
          - cell "—" [ref=e48]
        - row [ref=e49]:
          - cell "Товар-12" [ref=e50]
          - cell "58 300" [ref=e51]
          - cell "9 675" [ref=e52]
          - cell "—" [ref=e53]
        - row [ref=e54]:
          - cell "Товар-12" [ref=e55]
          - cell "26 300" [ref=e56]
          - cell "2 675" [ref=e57]
          - cell "—" [ref=e58]
        - row [ref=e59]:
          - cell "Товар-12" [ref=e60]
          - cell "20 300" [ref=e61]
          - cell "4 175" [ref=e62]
          - cell "—" [ref=e63]
        - row [ref=e64]:
          - cell "Товар-12" [ref=e65]
          - cell "78 300" [ref=e66]
          - cell "14 675" [ref=e67]
          - cell "—" [ref=e68]
        - row [ref=e69]:
          - cell "Товар-12" [ref=e70]
          - cell "72 300" [ref=e71]
          - cell "16 175" [ref=e72]
          - cell "—" [ref=e73]
        - row [ref=e74]:
          - cell "Товар-12" [ref=e75]
          - cell "66 300" [ref=e76]
          - cell "12 675" [ref=e77]
          - cell "—" [ref=e78]
        - row [ref=e79]:
          - cell "Товар-12" [ref=e80]
          - cell "34 300" [ref=e81]
          - cell "5 675" [ref=e82]
          - cell "—" [ref=e83]
        - row [ref=e84]:
          - cell "Товар-12" [ref=e85]
          - cell "28 300" [ref=e86]
          - cell "2 175" [ref=e87]
          - cell "—" [ref=e88]
        - row [ref=e89]:
          - cell "Товар-12" [ref=e90]
          - cell "86 300" [ref=e91]
          - cell "17 675" [ref=e92]
          - cell "—" [ref=e93]
        - row [ref=e94]:
          - cell "Товар-12" [ref=e95]
          - cell "80 300" [ref=e96]
          - cell "19 175" [ref=e97]
          - cell "—" [ref=e98]
        - row [ref=e99]:
          - cell "Товар-12" [ref=e100]
          - cell "48 300" [ref=e101]
          - cell "7 175" [ref=e102]
          - cell "—" [ref=e103]
        - row [ref=e104]:
          - cell "Товар-12" [ref=e105]
          - cell "42 300" [ref=e106]
          - cell "8 675" [ref=e107]
          - cell "—" [ref=e108]
        - row [ref=e109]:
          - cell "Товар-12" [ref=e110]
          - cell "10 300" [ref=e111]
          - cell "1 675" [ref=e112]
          - cell "—" [ref=e113]
        - row [ref=e114]:
          - cell "Товар-12" [ref=e115]
          - cell "36 300" [ref=e116]
          - cell "5 175" [ref=e117]
          - cell "—" [ref=e118]
        - row [ref=e119]:
          - cell "Товар-12" [ref=e120]
          - cell "94 300" [ref=e121]
          - cell "20 675" [ref=e122]
          - cell "—" [ref=e123]
        - row [ref=e124]:
          - cell "Товар-12" [ref=e125]
          - cell "88 300" [ref=e126]
          - cell "17 175" [ref=e127]
          - cell "—" [ref=e128]
        - row [ref=e129]:
          - cell "Товар-12" [ref=e130]
          - cell "56 300" [ref=e131]
          - cell "10 175" [ref=e132]
          - cell "—" [ref=e133]
        - row [ref=e134]:
          - cell "Товар-12" [ref=e135]
          - cell "50 300" [ref=e136]
          - cell "11 675" [ref=e137]
          - cell "—" [ref=e138]
        - row [ref=e139]:
          - cell "Товар-12" [ref=e140]
          - cell "18 300" [ref=e141]
          - cell "-325" [ref=e142]
          - cell "—" [ref=e143]
        - row [ref=e144]:
          - cell "Товар-12" [ref=e145]
          - cell "12 300" [ref=e146]
          - cell "1 175" [ref=e147]
          - cell "—" [ref=e148]
        - row [ref=e149]:
          - cell "Товар-12" [ref=e150]
          - cell "70 300" [ref=e151]
          - cell "16 675" [ref=e152]
          - cell "—" [ref=e153]
        - row [ref=e154]:
          - cell "Товар-12" [ref=e155]
          - cell "64 300" [ref=e156]
          - cell "13 175" [ref=e157]
          - cell "—" [ref=e158]
        - row [ref=e159]:
          - cell "Товар-12" [ref=e160]
          - cell "32 300" [ref=e161]
          - cell "6 175" [ref=e162]
          - cell "—" [ref=e163]
        - row [ref=e164]:
          - cell "Товар-12" [ref=e165]
          - cell "58 300" [ref=e166]
          - cell "9 675" [ref=e167]
          - cell "—" [ref=e168]
        - row [ref=e169]:
          - cell "Товар-12" [ref=e170]
          - cell "26 300" [ref=e171]
          - cell "2 675" [ref=e172]
          - cell "—" [ref=e173]
        - row [ref=e174]:
          - cell "Товар-12" [ref=e175]
          - cell "20 300" [ref=e176]
          - cell "4 175" [ref=e177]
          - cell "—" [ref=e178]
        - row [ref=e179]:
          - cell "Товар-12" [ref=e180]
          - cell "78 300" [ref=e181]
          - cell "14 675" [ref=e182]
          - cell "—" [ref=e183]
        - row [ref=e184]:
          - cell "Товар-12" [ref=e185]
          - cell "72 300" [ref=e186]
          - cell "16 175" [ref=e187]
          - cell "—" [ref=e188]
        - row [ref=e189]:
          - cell "Товар-12" [ref=e190]
          - cell "40 300" [ref=e191]
          - cell "9 175" [ref=e192]
          - cell "—" [ref=e193]
        - row [ref=e194]:
          - cell "Товар-12" [ref=e195]
          - cell "34 300" [ref=e196]
          - cell "5 675" [ref=e197]
          - cell "—" [ref=e198]
        - row [ref=e199]:
          - cell "Товар-12" [ref=e200]
          - cell "92 300" [ref=e201]
          - cell "21 175" [ref=e202]
          - cell "—" [ref=e203]
        - row [ref=e204]:
          - cell "Товар-12" [ref=e205]
          - cell "86 300" [ref=e206]
          - cell "17 675" [ref=e207]
          - cell "—" [ref=e208]
        - row [ref=e209]:
          - cell "Товар-12" [ref=e210]
          - cell "80 300" [ref=e211]
          - cell "19 175" [ref=e212]
          - cell "—" [ref=e213]
        - row [ref=e214]:
          - cell [ref=e215]
  - heading "Доля категорий в сумме продаж" [level=2] [ref=e223]
```

# Test source

```ts
  1   | import { expect, test } from '@playwright/test'
  2   | 
  3   | const BASE = '/report.html?report=sales-summary'
  4   | 
  5   | test('отчёт открывается с параметрами из URL и показывает строки', async ({ page }) => {
  6   |   await page.goto(`${BASE}&from=2026-01-01&to=2026-03-31`)
  7   | 
  8   |   await expect(page.locator('h1')).toHaveText('Сводная таблица продаж')
  9   |   await expect(page.locator('.error')).toHaveCount(0)
  10  |   await expect(page.locator('.text')).toContainText('2026-01-01')
  11  |   await expect(page.locator('.text')).toContainText('2026-03-31')
  12  | 
  13  |   // meta: «строк: 24723 · запрос: N мс · шаблон v1»
  14  |   const meta = page.locator('.meta')
  15  |   await expect(meta).toContainText('строк')
  16  |   const rows = Number((await meta.innerText()).match(/строк:\s*(\d+)/)[1])
  17  |   expect(rows).toBeGreaterThan(20000)
  18  | 
  19  |   // витрина рендерит только окно, а не все строки
  20  |   const domRows = await page.locator('.rt-table tbody tr:not(.spacer)').count()
  21  |   expect(domRows).toBeGreaterThan(5)
  22  |   expect(domRows).toBeLessThan(200)
  23  | })
  24  | 
  25  | test('смена параметра меняет данные и попадает в URL', async ({ page }) => {
  26  |   await page.goto(`${BASE}&from=2026-01-01&to=2026-03-31`)
  27  |   const meta = page.locator('.meta')
  28  |   const before = Number((await meta.innerText()).match(/строк:\s*(\d+)/)[1])
  29  | 
  30  |   await page.fill('input[type="date"] >> nth=0', '2026-03-01')
  31  |   await page.click('button[type="submit"]')
  32  |   await expect(meta).toContainText('строк')
  33  | 
  34  |   await expect.poll(async () => {
  35  |     const text = await meta.innerText()
  36  |     return Number(text.match(/строк:\s*(\d+)/)[1])
  37  |   }).toBeLessThan(before)
  38  | 
  39  |   expect(page.url()).toContain('from=2026-03-01')
  40  | })
  41  | 
  42  | test('битый параметр показывает типизированную ошибку', async ({ page }) => {
  43  |   await page.goto(`${BASE}&from=01.01.2026&to=2026-03-31`)
  44  |   const error = page.locator('.error')
  45  |   await expect(error).toBeVisible()
  46  |   await expect(error).toContainText('INVALID_PARAMS')
  47  |   await expect(error).toContainText('from')
  48  | })
  49  | 
  50  | test('диаграмма отрисована на canvas', async ({ page }) => {
  51  |   await page.goto(`${BASE}&from=2026-01-01&to=2026-03-31`)
> 52  |   await expect(page.locator('.chart canvas')).toHaveCount(1)
      |                                               ^ Error: expect(locator).toHaveCount(expected) failed
  53  | })
  54  | 
  55  | test('сгруппированный отчёт: подписи групп и объединённые ячейки', async ({ page }) => {
  56  |   await page.goto(`${BASE}&from=2026-03-01&to=2026-03-10`)
  57  | 
  58  |   // первая колонка первой строки — значение группы (регион), вторая — категория
  59  |   const firstRowCells = page.locator('.rt-table tbody tr:not(.spacer)').first().locator('td')
  60  |   await expect(firstRowCells.nth(0)).not.toHaveText('')
  61  |   await expect(firstRowCells.nth(1)).not.toHaveText('')
  62  | 
  63  |   // объединения есть, и они не выходят за окно
  64  |   const merges = await page.locator('.rt-table td[rowspan]').count()
  65  |   expect(merges).toBeGreaterThan(0)
  66  | })
  67  | 
  68  | test('многоуровневый заголовок: группа «Финансы» на две колонки', async ({ page }) => {
  69  |   await page.goto(`${BASE}&from=2026-03-01&to=2026-03-10`)
  70  |   const group = page.locator('.rt-table thead th[colspan="2"]')
  71  |   await expect(group).toHaveCount(1)
  72  |   await expect(group).toHaveText('Финансы')
  73  |   // листья под группой остались на своих местах
  74  |   await expect(page.locator('.rt-table thead th', { hasText: 'Сумма' })).toHaveCount(1)
  75  | })
  76  | 
  77  | test('обе строки заголовка остаются видимыми при прокрутке', async ({ page }) => {
  78  |   await page.goto(`${BASE}&from=2026-03-01&to=2026-03-10`)
  79  | 
  80  |   await page.locator('.rt').evaluate((el) => { el.scrollTop = 20000 })
  81  |   await page.waitForTimeout(300)
  82  | 
  83  |   const group = page.locator('.rt-table thead th[colspan="2"]')
  84  |   const leaf = page.locator('.rt-table thead th', { hasText: 'Сумма' })
  85  |   await expect(group).toBeVisible()
  86  |   await expect(leaf).toBeVisible()
  87  | 
  88  |   const box = await page.locator('.rt').boundingBox()
  89  |   const groupBox = await group.boundingBox()
  90  |   const leafBox = await leaf.boundingBox()
  91  |   // группа — верхняя строка заголовка, листья — сразу под ней, обе внутри видимой области
  92  |   expect(groupBox.y).toBeGreaterThanOrEqual(box.y - 1)
  93  |   expect(groupBox.y).toBeLessThan(box.y + 30)
  94  |   expect(leafBox.y).toBeGreaterThan(groupBox.y)
  95  |   expect(leafBox.y + leafBox.height).toBeLessThan(box.y + 70)
  96  | })
  97  | 
  98  | test('сворачивание и разворачивание группы', async ({ page }) => {
  99  |   await page.goto(`${BASE}&from=2026-03-01&to=2026-03-10`)
  100 | 
  101 |   const first = page.locator('.rt-table tbody tr:not(.spacer)').first()
  102 |   const heightBefore = await page.locator('.rt').evaluate((el) => el.scrollHeight)
  103 | 
  104 |   await first.locator('.chev').first().click()
  105 | 
  106 |   const total = page.locator('.rt-table tbody tr:not(.spacer)').first()
  107 |   await expect(total).toContainText('Итого')
  108 |   await expect(total.locator('td').nth(3)).not.toHaveText('')
  109 |   const heightCollapsed = await page.locator('.rt').evaluate((el) => el.scrollHeight)
  110 |   expect(heightCollapsed).toBeLessThan(heightBefore)
  111 | 
  112 |   // клик по строке итога возвращает детали
  113 |   await total.locator('.chev').first().click()
  114 |   await expect(page.locator('.rt-table tbody tr:not(.spacer)').first()).not.toContainText('Итого')
  115 |   const heightExpanded = await page.locator('.rt').evaluate((el) => el.scrollHeight)
  116 |   expect(heightExpanded).toBe(heightBefore)
  117 | })
  118 | 
  119 | test('сворачивание категории внутри региона', async ({ page }) => {
  120 |   await page.goto(`${BASE}&from=2026-03-01&to=2026-03-10`)
  121 |   const heightBefore = await page.locator('.rt').evaluate((el) => el.scrollHeight)
  122 | 
  123 |   // второй шеврон в строке — колонка «Категория»
  124 |   await page.locator('.rt-table tbody tr:not(.spacer)').first().locator('.chev').nth(1).click()
  125 | 
  126 |   const total = page.locator('.rt-table tbody tr:not(.spacer)').first()
  127 |   await expect(total).toContainText('Итого')
  128 |   await expect(total.locator('td').nth(1)).not.toHaveText('')
  129 |   const heightCollapsed = await page.locator('.rt').evaluate((el) => el.scrollHeight)
  130 |   expect(heightCollapsed).toBeLessThan(heightBefore)
  131 | 
  132 |   // разворачиваем обратно именно категорию (второй шеврон строки итога)
  133 |   await total.locator('.chev').nth(1).click()
  134 |   await expect(page.locator('.rt-table tbody tr:not(.spacer)').first()).not.toContainText('Итого')
  135 | })
  136 | 
  137 | test('свёрнутая категория не меняет шеврон региона (и после прокрутки)', async ({ page }) => {
  138 |   await page.goto(`${BASE}&from=2026-03-01&to=2026-03-10`)
  139 | 
  140 |   const first = page.locator('.rt-table tbody tr:not(.spacer)').first()
  141 |   const regionBefore = (await first.locator('.chev').nth(0).innerText()).trim()
  142 |   await first.locator('.chev').nth(1).click()
  143 | 
  144 |   const row = page.locator('.rt-table tbody tr:not(.spacer)').first()
  145 |   await expect(row.locator('.chev').nth(0)).toHaveText(regionBefore) // регион раскрыт
  146 |   await expect(row.locator('.chev').nth(1)).toHaveText('▸')          // категория свёрнута
  147 | 
  148 |   await page.locator('.rt').evaluate((el) => { el.scrollTop = 20000 })
  149 |   await page.waitForTimeout(300)
  150 |   const scrolled = page.locator('.rt-table tbody tr:not(.spacer)').first()
  151 |   await expect(scrolled.locator('.chev').nth(0)).toHaveText(regionBefore)
  152 | })
```