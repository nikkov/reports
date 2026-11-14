# Instructions

- Following Playwright test failed.
- Explain why, be concise, respect Playwright best practices.
- Provide a snippet of code with the fix, if possible.

# Test info

- Name: report.spec.js >> объединение источников: PostgreSQL + REST в одной таблице (ADR-104)
- Location: e2e/report.spec.js:2895:1

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
  2798 |     if (res.ok()) return (await res.json()).id
  2799 |     const list = await (await request.get('/api/admin/access-roles')).json()
  2800 |     return list.items.find((r) => r.key === key)?.id
  2801 |   }
  2802 |   await ensureRole('sales', 'Продажи')
  2803 |   await ensureRole('finance', 'Финансы')
  2804 | 
  2805 |   const login = 'roles_e2e_user'
  2806 |   const password = 'roles-pass-123'
  2807 |   const users = await (await request.get('/api/admin/users')).json()
  2808 |   for (const u of users.items.filter((x) => x.login === login)) {
  2809 |     await request.delete(`/api/admin/users/${u.id}`)
  2810 |   }
  2811 |   const created = await request.post('/api/admin/users', {
  2812 |     data: { login, role: 'viewer', password, roles: ['sales'] }
  2813 |   })
  2814 |   expect(created.ok()).toBeTruthy()
  2815 |   const { id } = await created.json()
  2816 | 
  2817 |   const ctx = await apiRequest.newContext({ baseURL })
  2818 |   expect((await ctx.post('/api/auth/login', { data: { login, password } })).ok()).toBeTruthy()
  2819 | 
  2820 |   // каталог: отчёт с ролью sales виден, с ролью finance — скрыт; без ролей виден всем
  2821 |   const catalog = (await (await ctx.get('/api/reports')).json()).map((r) => r.id)
  2822 |   expect(catalog).toContain('sales-summary')
  2823 |   expect(catalog).toContain('sales-drill')
  2824 |   expect(catalog).not.toContain('finance-plan-fact')
  2825 |   expect(catalog).not.toContain('ops-events')
  2826 | 
  2827 |   // прямой доступ к шаблону и к данным — 403 REPORT_FORBIDDEN
  2828 |   expect((await ctx.get('/api/reports/finance-plan-fact')).status()).toBe(403)
  2829 |   const denied = await ctx.post('/api/report/data', {
  2830 |     data: { reportId: 'finance-plan-fact', params: {} }
  2831 |   })
  2832 |   expect(denied.status()).toBe(403)
  2833 |   expect((await denied.json()).error.code).toBe('REPORT_FORBIDDEN')
  2834 |   // доступный отчёт открывается
  2835 |   expect((await ctx.get('/api/reports/sales-summary')).ok()).toBeTruthy()
  2836 | 
  2837 |   // администратор видит все отчёты
  2838 |   const adminCatalog = (await (await request.get('/api/reports')).json()).map((r) => r.id)
  2839 |   expect(adminCatalog).toContain('finance-plan-fact')
  2840 | 
  2841 |   // выдали вторую роль — отчёт стал доступен
  2842 |   expect(
  2843 |     (await request.put(`/api/admin/users/${id}`, {
  2844 |       data: { role: 'viewer', enabled: true, roles: ['sales', 'finance'] }
  2845 |     })).ok()
  2846 |   ).toBeTruthy()
  2847 |   expect((await ctx.get('/api/reports/finance-plan-fact')).ok()).toBeTruthy()
  2848 | 
  2849 |   await ctx.dispose()
  2850 |   expect((await request.delete(`/api/admin/users/${id}`)).ok()).toBeTruthy()
  2851 | })
  2852 | 
  2853 | test('публичный отчёт открывается без входа, остальные — нет (ADR-103)', async ({ browser, baseURL }) => {
  2854 |   // анонимный контекст: намеренно пустое состояние (в проекте задан storageState admin)
  2855 |   const anonState = { cookies: [], origins: [] }
  2856 |   const anonCtx = await apiRequest.newContext({ baseURL, storageState: anonState })
  2857 |   const anon = await browser.newContext({ baseURL, storageState: anonState })
  2858 | 
  2859 |   // шаблон публичного отчёта отдаётся без входа и помечен public
  2860 |   const tplRes = await anonCtx.get('/api/reports/public-demo')
  2861 |   expect(tplRes.ok()).toBeTruthy()
  2862 |   expect((await tplRes.json()).public).toBe(true)
  2863 | 
  2864 |   // данные и экспорт — тоже
  2865 |   const data = await anonCtx.post('/api/report/data', {
  2866 |     data: { reportId: 'public-demo', params: { from: '2026-01-01', to: '2026-11-14' } }
  2867 |   })
  2868 |   expect(data.ok()).toBeTruthy()
  2869 |   expect((await data.json()).rowCount).toBeGreaterThan(0)
  2870 |   const xlsx = await anonCtx.post('/api/report/export.xlsx', {
  2871 |     data: { reportId: 'public-demo', params: { from: '2026-01-01', to: '2026-11-14' } }
  2872 |   })
  2873 |   expect(xlsx.ok()).toBeTruthy()
  2874 | 
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
> 2898 |   await expect(page.locator('.meta')).toContainText('строк')
       |                                       ^ Error: expect(locator).toContainText(expected) failed
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
  2975 |   const res = await request.post('/api/report/data', {
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
```