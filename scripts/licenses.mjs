#!/usr/bin/env node
// Ревизия лицензий прямых зависимостей (DoD §5 ТЗ).
// Читает фактические версии из node_modules и кэша Conan, сверяет с allowlist.
// Запуск: node scripts/licenses.mjs [--json]
import { execSync } from 'node:child_process'
import { readFileSync, existsSync } from 'node:fs'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..')
const ALLOWED = new Set(['MIT', 'BSD-2-Clause', 'BSD-3-Clause', 'Apache-2.0', 'ISC', '0BSD'])

// Осознанные отклонения: permissive, но не дословно MIT/BSD/Apache. Согласованы с постановщиком.
const DEVIATIONS = {
  'libpq': 'PostgreSQL License — официальный клиент PostgreSQL, замена невозможна',
  'catch2': 'BSL-1.0 — permissive, требуется сохранить текст лицензии',
  zlib: 'Zlib — permissive, транзитивно требуется Drogon для gzip/deflate'
}

const rows = []

// npm: только прямые зависимости из package.json
const pkg = JSON.parse(readFileSync(join(ROOT, 'frontend/package.json'), 'utf8'))
for (const name of [...Object.keys(pkg.dependencies || {}), ...Object.keys(pkg.devDependencies || {})]) {
  const path = join(ROOT, 'frontend/node_modules', name, 'package.json')
  if (!existsSync(path)) {
    rows.push({ name, version: '—', license: 'НЕ УСТАНОВЛЕН', kind: 'npm' })
    continue
  }
  const meta = JSON.parse(readFileSync(path, 'utf8'))
  rows.push({ name, version: meta.version, license: meta.license || 'UNKNOWN', kind: 'npm' })
}

// Conan: requires из conanfile.txt + транзитивные, которые реально стоят в кэше
const requires = []
for (const line of readFileSync(join(ROOT, 'backend/conanfile.txt'), 'utf8').split('\n')) {
  const m = line.trim().match(/^([a-z0-9_-]+)\/([^@\s]+)/i)
  if (m) requires.push(m[1])
}
const TRANSITIVE = { drogon: ['trantor', 'jsoncpp', 'openssl', 'zlib', 'libpq'], catch2: [] }

let cached = []
try {
  cached = execSync(`ls ${process.env.HOME}/.conan/data`, { encoding: 'utf8' }).split('\n').filter(Boolean)
} catch {
  cached = []
}

// Лицензия Conan-пакета живёт в его рецепте: <pkg>/<ver>/_/_/export/conanfile.py
function conanLicense(name, version) {
  const recipe = join(process.env.HOME, '.conan/data', name, version, '_/_/export/conanfile.py')
  if (!existsSync(recipe)) return 'не найдена'
  const m = readFileSync(recipe, 'utf8').match(/^\s*license\s*=\s*['\"]([^'\"]+)['\"]/m)
  return m ? m[1] : 'не указана'
}

for (const dep of requires) {
  const versions = cached.includes(dep)
    ? execSync(`ls ${process.env.HOME}/.conan/data/${dep}`, { encoding: 'utf8' }).split('\n').filter(Boolean).sort()
    : []
  const depVersion = versions.at(-1) || '—'
  rows.push({ name: dep, version: depVersion, license: conanLicense(dep, depVersion), kind: 'conan (прямая)' })
  for (const t of TRANSITIVE[dep] || []) {
    if (!cached.includes(t)) continue
    const v = execSync(`ls ${process.env.HOME}/.conan/data/${t}`, { encoding: 'utf8' })
      .split('\n')
      .filter(Boolean)
      .sort()
      .at(-1)
    rows.push({ name: t, version: v, license: conanLicense(t, v), kind: 'conan (транзитивная)' })
  }
}

if (process.argv.includes('--json')) {
  console.log(JSON.stringify(rows, null, 2))
} else {
  const w = Math.max(...rows.map((r) => r.name.length), 4)
  for (const r of rows) {
    console.log(`${r.name.padEnd(w)}  ${String(r.version).padEnd(10)} ${r.license.padEnd(24)} ${r.kind}`)
  }
}

const bad = rows.filter((r) => !ALLOWED.has(r.license) && !DEVIATIONS[r.name])
if (bad.length) {
  console.error('\n✗ лицензии вне allowlist: ' + bad.map((b) => `${b.name} (${b.license})`).join(', '))
  process.exit(1)
}
console.log('\nлицензии прямых зависимостей: ok')
