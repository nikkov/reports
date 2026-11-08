#!/usr/bin/env node
// Реестр ADR собирается из PLAN.md (единственное место, где решения пишутся
// рядом с фазами и гейтами). Скрипт делает docs/adr.md с оглавлением и якорями,
// чтобы читать решения в одном файле и не искать их по двум документам.
//
//   node scripts/adr.mjs            # перегенерировать docs/adr.md
//   node scripts/adr.mjs --check    # проверить, что реестр не устарел (код 1)
import { readFileSync, writeFileSync, existsSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const here = dirname(fileURLToPath(import.meta.url))
const root = resolve(here, '..')
const sourcePath = resolve(root, 'docs/plan/PLAN.md')
const outPath = resolve(root, 'docs/adr.md')
const check = process.argv.includes('--check')

const source = readFileSync(sourcePath, 'utf8')
const sectionStart = source.indexOf('## 1. Принятые архитектурные решения')
if (sectionStart < 0) {
  console.error('в PLAN.md нет раздела «Принятые архитектурные решения (ADR)»')
  process.exit(2)
}
const nextSection = source.indexOf('\n## ', sectionStart + 1)
const section = source.slice(sectionStart, nextSection < 0 ? source.length : nextSection)

// Заголовок бывает двух видов: «**ADR-N. Название.**» отдельной строкой и
// «**ADR-N. Название.** текст сразу в той же строке» — берём до первого ** и всё,
// что после него, считаем началом тела.
const blocks = []
const re = /\*\*ADR-(\d+)\.\s*([\s\S]*?)\*\*/g
let match
const starts = []
while ((match = re.exec(section)) !== null) {
  starts.push({ number: Number(match[1]), title: match[2].trim(), from: match.index, headerEnd: re.lastIndex })
}
// Тело решения обязано состоять из четырёх частей: без них смысл теряется,
// а «единый формат» превращается в лозунг — поэтому проверяем при сборке.
const PARTS = [
  ['problem', 'Проблема'],
  ['decision', 'Решение'],
  ['effects', 'Следствия'],
  ['limits', 'Ограничения']
]

function parseParts(body) {
  const parts = {}
  for (const [key, label] of PARTS) {
    const re = new RegExp(`^- \\*\\*${label}:\\*\\* ?([\\s\\S]*?)(?=^- \\*\\*(?:Проблема|Решение|Следствия|Ограничения):|$)`, 'm')
    const m = body.match(re)
    if (m) parts[key] = m[1].trim()
  }
  return parts
}

starts.forEach((item, i) => {
  const to = i + 1 < starts.length ? starts[i + 1].from : section.length
  const body = section.slice(item.headerEnd, to).trim()
  blocks.push({ ...item, body, ...parseParts(body) })
})

const incomplete = blocks.filter((b) => PARTS.some(([key]) => !b[key]))
if (incomplete.length) {
  console.error('у этих ADR нет четырёх частей (Проблема, Решение, Следствия, Ограничения):')
  for (const b of incomplete) {
    const missing = PARTS.filter(([key]) => !b[key]).map(([, label]) => label)
    console.error(`  ADR-${b.number}: нет «${missing.join('», «')}»`)
  }
  process.exit(1)
}

blocks.sort((a, b) => a.number - b.number)

const decorated = blocks.map((b) => {
  const version = (b.title.match(/\(версия ([0-9]+(?:\.[0-9]+)*)\)/) || [])[1] || ''
  const plain = b.title.replace(/\s*\(версия [^)]+\)\.?$/, '').replace(/\.$/, '')
  return { ...b, version, plain }
})

const esc = (s) => s.replace(/\|/g, '\\|')
const lines = []
lines.push('# Архитектурные решения (ADR)')
lines.push('')
lines.push(`Реестр собран из \`docs/plan/PLAN.md\` — там решения пишутся рядом с фазами и гейтами.`)
lines.push('Файл генерируется: `node scripts/adr.mjs`; актуальность проверяет `./scripts/check.sh`')
lines.push('(`node scripts/adr.mjs --check`). Правьте PLAN.md, а не этот файл.')
lines.push('')
lines.push('Каждое решение записано четырьмя частями: **проблема**, **решение**, **следствия**, **ограничения**.')
lines.push('')
const maxNumber = decorated.length ? decorated[decorated.length - 1].number : 0
const present = new Set(decorated.map((b) => b.number))
const gaps = []
for (let n = 1; n <= maxNumber; n++) if (!present.has(n)) gaps.push(n)
lines.push(
  `Всего решений: **${decorated.length}**, номера ADR-${decorated[0]?.number ?? 0}…ADR-${maxNumber}` +
    (gaps.length
      ? `; блоков с номерами ${gaps.map((n) => `ADR-${n}`).join(', ')} в PLAN.md нет (заменены или не заводились)`
      : '') +
    '.'
)
lines.push('')
lines.push('## Оглавление')
lines.push('')
lines.push('| № | Решение | Версия |')
lines.push('|---|---|---|')
for (const b of decorated) {
  lines.push(`| [ADR-${b.number}](#adr-${b.number}) | ${esc(b.plain)} | ${b.version || '—'} |`)
}
lines.push('')
for (const b of decorated) {
  lines.push(`<a id="adr-${b.number}"></a>`)
  lines.push('')
  lines.push(`## ADR-${b.number}. ${b.plain}${b.version ? ` (версия ${b.version})` : ''}`)
  lines.push('')
  for (const [key, label] of PARTS) {
    lines.push(`**${label}.** ${b[key]}`)
    lines.push('')
  }
}

const generated = lines.join('\n')

if (check) {
  const current = existsSync(outPath) ? readFileSync(outPath, 'utf8') : ''
  if (current !== generated) {
    console.error('docs/adr.md устарел: перегенерируйте `node scripts/adr.mjs`')
    const have = new Set((current.match(/^## ADR-(\d+)\./gm) || []).map((l) => Number(l.match(/\d+/)[0])))
    const want = new Set(decorated.map((b) => b.number))
    const missing = [...want].filter((n) => !have.has(n))
    const extra = [...have].filter((n) => !want.has(n))
    if (missing.length) console.error(`  нет в файле: ADR-${missing.join(', ADR-')}`)
    if (extra.length) console.error(`  лишние в файле: ADR-${extra.join(', ADR-')}`)
    if (!missing.length && !extra.length) console.error('  состав тот же — разошёлся текст')
    process.exit(1)
  }
  console.log(`реестр ADR актуален: ${decorated.length} решений`)
  process.exit(0)
}

writeFileSync(outPath, generated)
console.log(`docs/adr.md: ${decorated.length} решений, ADR-${decorated[0]?.number}…ADR-${decorated[decorated.length - 1]?.number}`)
