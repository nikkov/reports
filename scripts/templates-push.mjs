#!/usr/bin/env node
// Загрузка шаблонов из файлов в БД (ADR-21). Файлы остаются источником в репозитории,
// CLI-валидатор проверяет их до загрузки.
//   node scripts/templates-push.mjs backend/templates/*.json
import { execFileSync } from 'node:child_process'
import { readFileSync, writeFileSync, mkdtempSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'

const files = process.argv.slice(2)
if (!files.length) {
  console.error('использование: node scripts/templates-push.mjs <шаблон.json> [...]')
  process.exit(2)
}

const statements = []
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
  // dollar-quoting: тело шаблона не интерполируется в SQL-строку
  const tag = '$tpl$'
  if (raw.includes(tag)) {
    console.error(`✗ ${file}: содержит ${tag}, выберите другой тег`)
    process.exit(1)
  }
  statements.push(
    `INSERT INTO report_templates (id, title, body, updated_at)
     VALUES ('${String(tpl.id).replace(/'/g, "''")}', '${String(tpl.title || tpl.id).replace(/'/g, "''")}', ${tag}${raw}${tag}::jsonb, now())
     ON CONFLICT (id) DO UPDATE SET title = EXCLUDED.title, body = EXCLUDED.body, updated_at = now();`
  )
  console.log(`✓ ${tpl.id} ← ${file}`)
}

const dir = mkdtempSync(join(tmpdir(), 'rep-tpl-'))
const sqlFile = join(dir, 'push.sql')
writeFileSync(sqlFile, statements.join('\n') + '\n')

execFileSync('psql', ['-d', process.env.PGDATABASE || 'reporting', '-v', 'ON_ERROR_STOP=1', '-q', '-f', sqlFile], {
  stdio: 'inherit',
  env: process.env
})
console.log(`загружено шаблонов: ${files.length}`)
