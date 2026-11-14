# Instructions

- Following Playwright test failed.
- Explain why, be concise, respect Playwright best practices.
- Provide a snippet of code with the fix, if possible.

# Test info

- Name: report.spec.js >> постраничный REST: набор собирает страницы до неполной (ADR-106)
- Location: e2e/report.spec.js:2974:1

# Error details

```
Test timeout of 30000ms exceeded.
```

```
Error: apiRequestContext.post: Request context disposed.
Call log:
  - → POST http://127.0.0.1:8080/api/report/data
    - user-agent: Playwright/1.65.0 (x64; astra 1.8_x86-64) node/24.22
    - accept: */*
    - accept-encoding: gzip,deflate,br
    - content-type: application/json
    - content-length: 53
    - cookie: rep_session=9b457d7e99ee2c46c37c59209ed31f22c97e5b996d8ca174a9f46f4f57d1eb6c

```

# Test source

```ts
  2875 |   // закрытый отчёт и каталог без входа — 401
  2876 |   expect((await anonCtx.get('/api/reports/sales-drill')).status()).toBe(401)
  2877 |   expect((await anonCtx.get('/api/reports')).status()).toBe(401)
  2878 | 
  2879 |   // страница публичного отчёта открывается без сессии и строит таблицу
  2880 |   const page = await anon.newPage()
  2881 |   await page.goto('/report.html?report=public-demo')
  2882 |   await expect(page.locator('h1')).toContainText('Открытый отчёт')
  2883 |   await expect(page.locator('.meta')).toContainText('строк: 5')
  2884 |   await expect(page.locator('.rt-table tbody tr.detail')).toHaveCount(5)
  2885 |   await expect(page.locator('.params')).toHaveCount(1)
  2886 | 
  2887 |   // обычный отчёт без входа уводит на страницу входа
  2888 |   await page.goto('/report.html?report=sales-drill')
  2889 |   await page.waitForURL(/login\.html/)
  2890 | 
  2891 |   await anon.close()
  2892 |   await anonCtx.dispose()
  2893 | })
  2894 | 
  2895 | test('объединение источников: PostgreSQL + REST в одной таблице (ADR-104)', async ({ page, request }) => {
  2896 |   // REST-демо (scripts/rest-demo.mjs) должен быть запущен на :8099, сервер — с REP_REST_ALLOW_HOSTS.
  2897 |   await page.goto('/report.html?report=sales-crm')
  2898 |   await expect(page.locator('.meta')).toContainText('строк')
  2899 |   const headers = await page.locator('.rt-table thead th').allInnerTexts()
  2900 |   expect(headers.join(' | ')).toContain('Менеджер')
  2901 |   expect(headers.join(' | ')).toContain('Регион')
  2902 |   // первая строка содержит менеджера из REST-справочника
  2903 |   const first = await page.locator('.rt-table tbody tr.detail').first().innerText()
  2904 |   expect(first).toMatch(/Иванов|Петрова|Сидоров|Кузнецова|Смирнов/)
  2905 | 
  2906 |   // агрегация по объединённому набору: 5 регионов + группа без пары (left join)
  2907 |   const agg = await request.post('/api/report/aggregate', {
  2908 |     data: {
  2909 |       reportId: 'sales-crm',
  2910 |       dataset: 'orders_by_region',
  2911 |       params: { from: '2025-11-01', to: '2026-11-14' },
  2912 |       groupBy: ['region_name', 'manager'],
  2913 |       measures: [{ field: 'amount', agg: 'sum' }]
  2914 |     }
  2915 |   })
  2916 |   expect(agg.ok()).toBeTruthy()
  2917 |   const body = await agg.json()
  2918 |   expect(body.rowCount).toBeGreaterThanOrEqual(5)
  2919 | 
  2920 |   // клиентский Excel: колонка правой стороны на месте, строки не потеряны
  2921 |   const [dl] = await Promise.all([
  2922 |     page.waitForEvent('download'),
  2923 |     page.locator('button.tool', { hasText: 'Excel' }).first().click()
  2924 |   ])
  2925 |   const zip = await JSZip.loadAsync(readFileSync(await dl.path()))
  2926 |   const shared = await zip.file('xl/sharedStrings.xml').async('string')
  2927 |   expect(shared).toContain('Менеджер')
  2928 | })
  2929 | 
  2930 | test('публичный отчёт с REST: справочник, шаблон и данные без входа (ADR-104)', async ({ browser, baseURL }) => {
  2931 |   // аноним: пустое состояние, сервер запущен с REP_REST_ALLOW_HOSTS и CRM_BASE_URL
  2932 |   const state = { cookies: [], origins: [] }
  2933 |   const ctx = await apiRequest.newContext({ baseURL, storageState: state })
  2934 | 
  2935 |   // параметр-справочник приходит из REST-набора
  2936 |   const lookups = await ctx.get('/api/reports/crm-public-filter/lookups/region')
  2937 |   expect(lookups.ok()).toBeTruthy()
  2938 |   const items = await lookups.json()
  2939 |   expect(items.length).toBeGreaterThanOrEqual(5)
  2940 |   expect(items.map((i) => i.label)).toContain('Север')
  2941 | 
  2942 |   // шаблон отдаётся без входа и без SQL/заголовков источников
  2943 |   const tpl = await ctx.get('/api/reports/crm-public-filter')
  2944 |   expect(tpl.ok()).toBeTruthy()
  2945 |   const raw = await tpl.text()
  2946 |   expect(raw).not.toContain('passwordEnv')
  2947 |   expect(raw).not.toContain('Authorization')
  2948 |   expect(raw).not.toContain('"sql"')
  2949 | 
  2950 |   // данные объединённого набора — тоже без входа
  2951 |   const data = await ctx.post('/api/report/data', {
  2952 |     data: {
  2953 |       reportId: 'crm-public-filter',
  2954 |       dataset: 'orders_by_region',
  2955 |       params: { region: '1', from: '2025-11-01', to: '2026-11-14' }
  2956 |     }
  2957 |   })
  2958 |   expect(data.ok()).toBeTruthy()
  2959 |   const body = await data.json()
  2960 |   expect(body.rowCount).toBeGreaterThan(0)
  2961 |   expect(body.columns.map((c) => c.name)).toContain('region_name')
  2962 |   expect(body.columns.map((c) => c.name)).toContain('manager')
  2963 | 
  2964 |   // страница открывается без сессии и строит таблицу
  2965 |   const page = await (await browser.newContext({ baseURL, storageState: state })).newPage()
  2966 |   await page.goto('/report.html?report=crm-public-filter')
  2967 |   await expect(page.locator('h1')).toContainText('Публичный отчёт')
  2968 |   await expect(page.locator('.rt-table tbody tr.detail').first()).toBeVisible()
  2969 | 
  2970 |   await page.context().close()
  2971 |   await ctx.dispose()
  2972 | })
  2973 | 
  2974 | test('постраничный REST: набор собирает страницы до неполной (ADR-106)', async ({ page, request }) => {
> 2975 |   const res = await request.post('/api/report/data', {
       |                             ^ Error: apiRequestContext.post: Request context disposed.
  2976 |     data: { reportId: 'crm-paged', dataset: 'rows', params: {} }
  2977 |   })
  2978 |   expect(res.ok()).toBeTruthy()
  2979 |   const body = await res.json()
  2980 |   // 2500 записей при size=1000: страницы 1000 + 1000 + 500
  2981 |   expect(body.rowCount).toBe(2500)
  2982 |   expect(body.rows[0].id).toBe(1)
  2983 |   expect(body.rows.at(-1).id).toBe(2500)
  2984 | 
  2985 |   await page.goto('/report.html?report=crm-paged')
  2986 |   await expect(page.locator('.meta')).toContainText('строк: 2500')
  2987 |   await expect(page.locator('.rt-table tbody tr.detail').first()).toBeVisible()
  2988 | })
  2989 | 
  2990 | // ADR-111: offset-режим пагинации — значение = start + страница × size.
  2991 | test('постраничный REST в режиме offset: страницы идут по смещению (ADR-111)', async ({ page, request }) => {
  2992 |   const res = await request.post('/api/report/data', {
  2993 |     data: { reportId: 'crm-paged-offset', dataset: 'rows', params: {} }
  2994 |   })
  2995 |   expect(res.ok()).toBeTruthy()
  2996 |   const body = await res.json()
  2997 |   // 2500 записей при size=1000 и start=0: offset=0, 1000, 2000
  2998 |   expect(body.rowCount).toBe(2500)
  2999 |   expect(body.rows[0].id).toBe(1)
  3000 |   expect(body.rows.at(-1).id).toBe(2500)
  3001 | 
  3002 |   await page.goto('/report.html?report=crm-paged-offset')
  3003 |   await expect(page.locator('.meta')).toContainText('строк: 2500')
  3004 |   await expect(page.locator('.rt-table tbody tr.detail').first()).toBeVisible()
  3005 | })
  3006 | 
```