import { readFileSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { describe, expect, it } from 'vitest'
import { validateTemplate } from './validate.js'

// Parity-тест: те же шаблоны прогоняются через серверный валидатор
// (backend/tests/validate_test.cc читает этот же файл) — пути ошибок обязаны совпадать.
const here = dirname(fileURLToPath(import.meta.url))
const fixtures = JSON.parse(readFileSync(resolve(here, '../../../docs/fixtures/validation-cases.json'), 'utf8'))

describe('валидатор: совпадение с сервером на фикстурах', () => {
  for (const c of fixtures.cases) {
    it(c.name, () => {
      const { errors } = validateTemplate(c.template)
      const actual = [...new Set(errors.map((e) => e.path))].sort()
      expect(actual).toEqual([...c.errors].sort())
    })
  }
})
