// Вход для e2e (ADR-34): один раз на прогон логинимся как admin и viewer,
// состояния сохраняем в .auth/*.json и подставляем через storageState.
import { request } from '@playwright/test'
import { readFileSync, mkdirSync, writeFileSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const here = dirname(fileURLToPath(import.meta.url))

function credential(key) {
  try {
    const env = readFileSync(resolve(here, '../../backend/.env'), 'utf8')
    const line = env.split('\n').find((l) => l.startsWith(key + '='))
    return line ? line.slice(key.length + 1).trim() : ''
  } catch {
    return ''
  }
}

export default async function globalSetup() {
  const api = process.env.REP_API || 'http://127.0.0.1:8080'
  mkdirSync(resolve(here, '../.auth'), { recursive: true })

  const users = [
    ['admin', credential('REP_ADMIN_LOGIN') || 'admin', credential('REP_ADMIN_PASSWORD')],
    ['viewer', credential('REP_VIEWER_LOGIN') || 'viewer', credential('REP_VIEWER_PASSWORD')]
  ]

  for (const [name, login, password] of users) {
    const ctx = await request.newContext({ baseURL: api })
    const res = await ctx.post('/api/auth/login', { data: { login, password } })
    if (res.status() === 404) {
      // аутентификация выключена (REP_AUTH=0) — e2e работают как раньше, без cookie
      writeFileSync(resolve(here, `../.auth/${name}.json`), JSON.stringify({ cookies: [], origins: [] }))
      await ctx.dispose()
      continue
    }
    if (!res.ok()) throw new Error(`вход ${login}: ${res.status()} ${await res.text()}`)
    await ctx.storageState({ path: resolve(here, `../.auth/${name}.json`) })
    await ctx.dispose()
  }
}
