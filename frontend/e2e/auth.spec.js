import { test, expect } from '@playwright/test'
import { readFileSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const here = dirname(fileURLToPath(import.meta.url))

function credential(key) {
  const env = readFileSync(resolve(here, '../../backend/.env'), 'utf8')
  const line = env.split('\n').find((l) => l.startsWith(key + '='))
  return line ? line.slice(key.length + 1).trim() : ''
}

// если сервер запущен с REP_AUTH=0, проверки входа неприменимы
test.beforeEach(async ({ request }) => {
  const me = await request.get('/api/auth/me')
  const payload = await me.json().catch(() => ({}))
  if (me.ok() && payload.auth === false) test.skip(true, 'аутентификация выключена (REP_AUTH=0)')
})

test.describe('без сессии', () => {
  test.use({ storageState: { cookies: [], origins: [] } })

  test('API отвечает 401, страница отчёта уводит на вход', async ({ page, request }) => {
    const res = await request.get('/api/reports')
    expect(res.status()).toBe(401)
    const body = await res.json()
    expect(body.error.code).toBe('UNAUTHORIZED')

    await page.goto('/report.html?report=sales-summary')
    await page.waitForURL(/login\.html/)
    await expect(page.locator('form h1')).toContainText('Сервер отчётов')
    // вернуться после входа туда, куда шли
    expect(page.url()).toContain('next=%2Freport.html')
  })

  test('неверный пароль не пускает', async ({ page }) => {
    await page.goto('/login.html')
    await page.locator('input[type=text]').fill('admin')
    await page.locator('input[type=password]').fill('wrong-password')
    await page.getByRole('button', { name: 'Войти' }).click()
    await expect(page.locator('.error')).toContainText('Неверный логин или пароль')
    expect(page.url()).toContain('login.html')
  })

  test('вход через форму открывает каталог', async ({ page }) => {
    await page.goto('/login.html')
    await page.locator('input[type=text]').fill(credential('REP_ADMIN_LOGIN') || 'admin')
    await page.locator('input[type=password]').fill(credential('REP_ADMIN_PASSWORD'))
    await page.getByRole('button', { name: 'Войти' }).click()
    await page.waitForURL((url) => !url.pathname.includes('login'))
    await expect(page.locator('h1')).toContainText('Сервер отчётов')
  })
})

test.describe('роль viewer', () => {
  test.use({ storageState: '.auth/viewer.json' })

  test('читает отчёты, но не открывает админку', async ({ page }) => {
    await page.goto('/')
    await expect(page.locator('.card').first()).toBeVisible()

    await page.goto('/admin.html')
    await expect(page.locator('body')).toContainText('Недостаточно прав')
  })

  test('публикация через API запрещена', async ({ request }) => {
    const res = await request.post('/api/admin/reports/sales-drill/publish')
    expect(res.status()).toBe(403)
    expect((await res.json()).error.code).toBe('FORBIDDEN')
  })
})

test('admin видит админку и полный шаблон с SQL', async ({ page, request }) => {
  const me = await (await request.get('/api/auth/me')).json()
  expect(me.role).toBe('admin')

  await page.goto('/admin.html')
  await expect(page.locator('.reports')).toBeVisible()

  const tpl = await (await request.get('/api/admin/reports/sales-summary')).json()
  expect(tpl.datasets.sales.sql).toContain('SELECT')
})
