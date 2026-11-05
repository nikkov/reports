#!/usr/bin/env node
// CLI-валидатор шаблонов: node scripts/validate.mjs <файл.json> [ещё файлы...]
// Код возврата 1, если хотя бы один шаблон невалиден.
import { readFileSync } from 'node:fs'
import { validateTemplate } from '../src/template/validate.js'

const files = process.argv.slice(2)
if (!files.length) {
  console.error('использование: npm run validate -- <шаблон.json> [...]')
  process.exit(2)
}

let failed = 0
for (const file of files) {
  let template
  try {
    template = JSON.parse(readFileSync(file, 'utf8'))
  } catch (e) {
    console.error(`✗ ${file}\n  JSON не разобран: ${e.message}`)
    failed++
    continue
  }

  const { errors, warnings } = validateTemplate(template)
  if (errors.length) {
    failed++
    console.error(`✗ ${file} — ${errors.length} ошибк(и)`)
    for (const err of errors) console.error(`  ${err.path}: ${err.message}`)
  } else {
    console.log(`✓ ${file}`)
  }
  for (const warn of warnings) console.log(`  ! ${warn.path}: ${warn.message}`)
}

process.exit(failed ? 1 : 0)
