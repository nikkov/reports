#!/usr/bin/env node
// Загрузка шаблонов из файлов в БД (ADR-21).
//   node scripts/templates-push.mjs backend/templates/*.json          # через API, без рестарта
//   node scripts/templates-push.mjs --sql backend/templates/*.json    # напрямую в БД (нужен рестарт)
//
// Режим по умолчанию идёт через админский API (REP_ADMIN=1): новый или изменённый шаблон
// сохраняется черновиком и сразу публикуется, память сервиса обновляется без перезапуска (ADR-31).
// Прямая запись в БД оставлена на случай, когда сервис не запущен.
import { execFileSync } from 'node:child_process'
import { readFileSync, writeFileSync, mkdtempSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'

const args = process.argv.slice(2)
const useSql = args.includes('--sql')
const files = args.filter((a) => !a.startsWith('--'))
const API = process.env.REP_API || 'http://127.0.0.1:8080'

if (!files.length) {
  console.error('использование: node scripts/templates-push.mjs [--sql] <шаблон.json> [...]')
  process.exit(2)
}

const templates = []
for (const file of files) {
  const raw = readFileSync(file, 'utf8')
  let tpl
  try {
    tpl = JSON.parse(raw)
  } catch (e) {
    console.error(`✗ ${file}: JSON не разобран — ${e.message}`)
    process.exit(1)
  }
  if (!tpl.id) {
    console.error(`✗ ${file}: нет поля id`)
    process.exit(1)
  }
  templates.push({ file, raw, tpl })
}

let authCookie = ''

async function login() {
  const password = process.env.REP_ADMIN_PASSWORD || ''
  if (!password) return
  const res = await fetch(`${API}/api/auth/login`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ login: process.env.REP_ADMIN_LOGIN || 'admin', password })
  })
  if (res.status === 404) return
  if (!res.ok) throw new Error(`вход: ${res.status}`)
  authCookie = (res.headers.get('set-cookie') || '').split(';')[0]
}

async function pushViaApi({ file, tpl }) {
  const id = encodeURIComponent(tpl.id)
  const json = { 'Content-Type': 'application/json', ...(authCookie ? { Cookie: authCookie } : {}) }

  const draft = await fetch(`${API}/api/admin/reports/${id}/draft`, {
    method: 'PUT',
    headers: json,
    body: JSON.stringify({ body: tpl })
  })
  if (!draft.ok) {
    const payload = await draft.json().catch(() => ({}))
    const code = payload?.error?.code
    if (code === 'REPORT_NOT_FOUND') {
      const created = await fetch(`${API}/api/reports`, {
        method: 'POST',
        headers: json,
        body: JSON.stringify({ id: tpl.id, body: tpl })
      })
      if (!created.ok) {
        throw new Error(`создание: ${created.status} ${JSON.stringify(await created.json().catch(() => ({})))}`)
      }
      console.log(`✓ ${tpl.id} ← ${file} (создан и опубликован)`)
      return
    }
    throw new Error(`черновик: ${draft.status} ${code || ''} ${payload?.error?.message || ''}`.trim())
  }

  const published = await fetch(`${API}/api/admin/reports/${id}/publish`, {
    method: 'POST',
    headers: authCookie ? { Cookie: authCookie } : {}
  })
  if (!published.ok) {
    throw new Error(`публикация: ${published.status} ${JSON.stringify(await published.json().catch(() => ({})))}`)
  }
  console.log(`✓ ${tpl.id} ← ${file} (опубликован без рестарта)`)
}

function pushViaSql() {
  const statements = templates.map(({ raw, tpl }) => {
    const tag = '$tpl$'
    if (raw.includes(tag)) {
      console.error(`✗ шаблон ${tpl.id}: содержит ${tag}, выберите другой тег`)
      process.exit(1)
    }
    return `INSERT INTO report_templates (id, title, body, updated_at)
     VALUES ('${String(tpl.id).replace(/'/g, "''")}', '${String(tpl.title || tpl.id).replace(/'/g, "''")}', ${tag}${raw}${tag}::jsonb, now())
     ON CONFLICT (id) DO UPDATE SET title = EXCLUDED.title, body = EXCLUDED.body, updated_at = now();`
  })
  const dir = mkdtempSync(join(tmpdir(), 'rep-tpl-'))
  const sqlFile = join(dir, 'push.sql')
  writeFileSync(sqlFile, statements.join('\n') + '\n')
  execFileSync('psql', ['-d', process.env.PGDATABASE || 'reporting', '-v', 'ON_ERROR_STOP=1', '-q', '-f', sqlFile], {
    stdio: 'inherit',
    env: process.env
  })
  console.log(`загружено шаблонов: ${templates.length} (прямая запись — нужен рестарт сервиса)`)
}

async function apiAvailable() {
  try {
    const res = await fetch(`${API}/api/health`, { signal: AbortSignal.timeout(1500) })
    return res.ok
  } catch {
    return false
  }
}

if (useSql) {
  pushViaSql()
} else if (await apiAvailable()) {
  try {
    await login()
  } catch (e) {
    console.error(`✗ ${e.message}`)
    process.exit(1)
  }
  for (const t of templates) {
    try {
      await pushViaApi(t)
    } catch (e) {
      console.error(`✗ ${t.tpl.id}: ${e.message}`)
      console.error('  подсказка: если сервис запущен без REP_ADMIN=1, используйте --sql')
      process.exit(1)
    }
  }
  console.log(`загружено шаблонов: ${templates.length} (API, рестарт не нужен)`)
} else {
  console.error(`сервис ${API} недоступен — пишу напрямую в БД; после этого нужен рестарт`)
  pushViaSql()
}
