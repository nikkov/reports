# Instructions

- Following Playwright test failed.
- Explain why, be concise, respect Playwright best practices.
- Provide a snippet of code with the fix, if possible.

# Test info

- Name: report.spec.js >> доступ к отчётам по ролям: каталог скрывает, API отдаёт 403 (ADR-102)
- Location: e2e/report.spec.js:2795:1

# Error details

```
Test timeout of 30000ms exceeded.
```

```
Error: apiRequestContext.post: Request context disposed.
Call log:
  - → POST http://127.0.0.1:8080/api/admin/access-roles
    - user-agent: Playwright/1.65.0 (x64; astra 1.8_x86-64) node/24.22
    - accept: */*
    - accept-encoding: gzip,deflate,br
    - content-type: application/json
    - content-length: 40
    - cookie: rep_session=9b457d7e99ee2c46c37c59209ed31f22c97e5b996d8ca174a9f46f4f57d1eb6c

```

# Test source

```ts
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
  2730 |     const probe = cells[0] ? getComputedStyle(cells[0]) : null
  2731 |     return {
  2732 |       total: cells.length,
  2733 |       red: count((td) => getComputedStyle(td).color === 'rgb(180, 35, 24)'),
  2734 |       warnBg: count((td) => getComputedStyle(td).backgroundColor === 'rgb(255, 244, 214)'),
  2735 |       rowBg: count((td) => getComputedStyle(td).backgroundColor === 'rgb(255, 247, 230)'),
  2736 |       colorAdjust: probe ? probe.printColorAdjust || probe.webkitPrintColorAdjust : null
  2737 |     }
  2738 |   })
  2739 |   expect(printed.total).toBeGreaterThan(100)
  2740 |   expect(printed.red).toBeGreaterThan(0)
  2741 |   expect(printed.warnBg).toBeGreaterThan(0)
  2742 |   expect(printed.rowBg).toBeGreaterThan(0)
  2743 |   expect(printed.colorAdjust).toBe('exact')
  2744 |   await page.emulateMedia({ media: 'screen' })
  2745 | 
  2746 |   // --- Excel: те же цвета в стилях книги, а не только в правилах на экране
  2747 |   await page.goto('/report.html?report=sales-rules')
  2748 |   await expect(page.locator('.meta')).toContainText('строк')
  2749 |   const [dl] = await Promise.all([
  2750 |     page.waitForEvent('download'),
  2751 |     page.locator('button.tool', { hasText: 'Excel' }).first().click()
  2752 |   ])
  2753 |   const zip = await JSZip.loadAsync(readFileSync(await dl.path()))
  2754 |   const styles = await zip.file('xl/styles.xml').async('string')
  2755 |   // #fff7e6 (вся строка), #fff4d6 (маржа), #b42318 (отрицательная прибыль)
  2756 |   expect(styles).toContain('FFFFF7E6')
  2757 |   expect(styles).toContain('FFFFF4D6')
  2758 |   expect(styles).toContain('FFB42318')
  2759 | })
  2760 | 
  2761 | test('текстовый блок: данные из запроса и гиперссылки (ADR-101)', async ({ page, request }) => {
  2762 |   await page.goto('/report.html?report=text-data')
  2763 |   await expect(page.locator('.meta')).toContainText('строк')
  2764 | 
  2765 |   const text = page.locator('.block p').first()
  2766 |   // {{data.<колонка>}} из строки набора summary
  2767 |   await expect(text).toContainText('24723')
  2768 |   await expect(text).toContainText('2026-03-31')
  2769 |   // обычные параметры продолжают работать
  2770 |   await expect(text).toContainText('2026-01-01')
  2771 | 
  2772 |   // ссылки: внутренняя и внешняя, обе в новой вкладке
  2773 |   const links = text.locator('a')
  2774 |   await expect(links).toHaveCount(2)
  2775 |   await expect(links.nth(0)).toHaveAttribute('href', 'report.html?report=sales-drill')
  2776 |   await expect(links.nth(0)).toHaveAttribute('target', '_blank')
  2777 |   await expect(links.nth(0)).toHaveAttribute('rel', 'noopener noreferrer')
  2778 |   await expect(links.nth(1)).toHaveAttribute('href', /^https:\/\/www\.postgresql\.org/)
  2779 | 
  2780 |   // запрос больше одной строки — 422, отчёт с таким блоком не строится
  2781 |   const many = await request.post('/api/report/text', {
  2782 |     data: { reportId: 'text-data', dataset: 'sales', params: { from: '2026-01-01', to: '2026-11-14' } }
  2783 |   })
  2784 |   expect(many.status()).toBe(422)
  2785 |   expect((await many.json()).error.code).toBe('TEXT_ROW_MULTIPLE')
  2786 | 
  2787 |   // ноль строк — пустые подстановки, без ошибки
  2788 |   const empty = await request.post('/api/report/text', {
  2789 |     data: { reportId: 'text-data', dataset: 'sales', params: { from: '2030-01-01', to: '2030-01-02' } }
  2790 |   })
  2791 |   expect(empty.ok()).toBeTruthy()
  2792 |   expect((await empty.json()).row).toBeNull()
  2793 | })
  2794 | 
  2795 | test('доступ к отчётам по ролям: каталог скрывает, API отдаёт 403 (ADR-102)', async ({ request, baseURL }) => {
  2796 |   const ensureRole = async (key, title) => {
> 2797 |     const res = await request.post('/api/admin/access-roles', { data: { key, title } })
       |                               ^ Error: apiRequestContext.post: Request context disposed.
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
```