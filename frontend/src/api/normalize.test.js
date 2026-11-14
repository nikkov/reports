import { describe, expect, it } from 'vitest'
import { normalizeDataset } from './normalize.js'

// ADR-114: компактный ответ (строки-массивы) приводится к объектам по колонкам.
describe('normalizeDataset', () => {
  it('строки-массивы превращаются в объекты по именам колонок', () => {
    const body = {
      columns: [
        { name: 'region', type: 'text' },
        { name: 'amount', type: 'int' }
      ],
      rows: [
        ['Север', 100],
        ['Юг', null]
      ],
      rowCount: 2,
      layout: 'compact'
    }
    const out = normalizeDataset(body)
    expect(out.rows).toEqual([
      { region: 'Север', amount: 100 },
      { region: 'Юг', amount: null }
    ])
    expect(out.rowCount).toBe(2)
  })

  it('прежний формат (объекты) не меняется', () => {
    const body = { columns: [{ name: 'a', type: 'int' }], rows: [{ a: 1 }] }
    expect(normalizeDataset(body)).toBe(body)
  })

  it('пустой набор остаётся как есть', () => {
    expect(normalizeDataset({ columns: [], rows: [] }).rows).toEqual([])
    expect(normalizeDataset(null)).toBe(null)
  })

  it('недостающие значения становятся null, лишние игнорируются', () => {
    const out = normalizeDataset({
      columns: [{ name: 'a' }, { name: 'b' }],
      rows: [['x']]
    })
    expect(out.rows[0]).toEqual({ a: 'x', b: null })
  })
})
