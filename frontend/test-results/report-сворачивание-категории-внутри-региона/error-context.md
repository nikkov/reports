# Instructions

- Following Playwright test failed.
- Explain why, be concise, respect Playwright best practices.
- Provide a snippet of code with the fix, if possible.

# Test info

- Name: report.spec.js >> сворачивание категории внутри региона
- Location: e2e/report.spec.js:119:1

# Error details

```
Error: expect(locator).not.toContainText(expected) failed

Locator: locator('.rt-table tbody tr:not(.spacer)').first()
Expected substring: not "Итого"
Received string: "▸ Итого Восток   30 367 500 6 134 375 20,2 %"
Timeout: 5000ms

Call log:
  - Expect "not toContainText" locator('.rt-table tbody tr:not(.spacer)').first() with timeout 5000ms
  - waiting for locator('.rt-table tbody tr:not(.spacer)').first()
    14 × locator resolved to <tr class="subtotal" data-v-d7502582="">…</tr>
       - unexpected value "▸ Итого Восток   30 367 500 6 134 375 20,2 %"

```

```yaml
- row "▸ Итого Восток 30 367 500 6 134 375 20,2 %":
  - cell "▸ Итого Восток"
  - cell
  - cell
  - cell "30 367 500"
  - cell "6 134 375"
  - cell "20,2 %"
```

# Test source

```ts
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
  52  |   await expect(page.locator('.chart canvas')).toHaveCount(1)
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
  132 |   await total.locator('.chev').first().click()
> 133 |   await expect(page.locator('.rt-table tbody tr:not(.spacer)').first()).not.toContainText('Итого')
      |                                                                             ^ Error: expect(locator).not.toContainText(expected) failed
  134 | })
  135 | 
  136 | test('свёрнутая категория не меняет шеврон региона (и после прокрутки)', async ({ page }) => {
  137 |   await page.goto(`${BASE}&from=2026-03-01&to=2026-03-10`)
  138 | 
  139 |   const first = page.locator('.rt-table tbody tr:not(.spacer)').first()
  140 |   const regionBefore = (await first.locator('.chev').nth(0).innerText()).trim()
  141 |   await first.locator('.chev').nth(1).click()
  142 | 
  143 |   const row = page.locator('.rt-table tbody tr:not(.spacer)').first()
  144 |   await expect(row.locator('.chev').nth(0)).toHaveText(regionBefore) // регион раскрыт
  145 |   await expect(row.locator('.chev').nth(1)).toHaveText('▸')          // категория свёрнута
  146 | 
  147 |   await page.locator('.rt').evaluate((el) => { el.scrollTop = 20000 })
  148 |   await page.waitForTimeout(300)
  149 |   const scrolled = page.locator('.rt-table tbody tr:not(.spacer)').first()
  150 |   await expect(scrolled.locator('.chev').nth(0)).toHaveText(regionBefore)
  151 | })
  152 | 
```