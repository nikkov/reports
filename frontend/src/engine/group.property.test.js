import { describe, expect, it } from 'vitest'
import fc from 'fast-check'
import { buildTableModel, computeMerges } from './group.js'

// Инварианты движка на случайных данных (2b.5). Генератор детерминирован seed-ом fast-check,
// поэтому падение воспроизводимо.
const columns = [
  { field: 'region', title: 'Регион', group: true, subtotal: true },
  { field: 'category', title: 'Категория', group: true },
  { field: 'product', title: 'Продукт' },
  { field: 'amount', title: 'Сумма', aggregate: 'sum' },
  { field: 'profit', title: 'Прибыль', aggregate: 'sum' },
  { title: 'Рентаб.', expression: 'sum(profit) / sum(amount)' }
]

const rowArb = fc.record({
  region: fc.option(fc.constantFrom('A', 'B', 'C'), { nil: null }),
  category: fc.option(fc.constantFrom('x', 'y', 'z'), { nil: null }),
  product: fc.constantFrom('p1', 'p2', 'p3'),
  amount: fc.integer({ min: 0, max: 1000 }),
  profit: fc.integer({ min: -1000, max: 1000 })
})

const rowsArb = fc.array(rowArb, { maxLength: 60 })

describe('инварианты модели', () => {
  it('сумма подытогов верхнего уровня равна общему итогу', () => {
    fc.assert(
      fc.property(rowsArb, (rows) => {
        const m = buildTableModel(rows, columns, { grandTotal: true })
        const grand = m.rows.at(-1)
        // пустой набор: SUM по нулю строк = null (как в SQL), поэтому нормализуем к 0
        const sum = m.rows
          .filter((r) => r.kind === 'subtotal' && r.groupPath.length === 1)
          .reduce((a, r) => a + Number(r.values[3] ?? 0), 0)
        expect(sum).toBe(Number(grand.values[3] ?? 0))
      }),
      { numRuns: 200 }
    )
  })

  it('каждая детальная строка встречается ровно один раз при полностью раскрытом дереве', () => {
    fc.assert(
      fc.property(rowsArb, (rows) => {
        const m = buildTableModel(rows, columns, { grandTotal: true })
        const details = m.rows.filter((r) => r.kind === 'detail')
        expect(details).toHaveLength(rows.length)
        const sum = details.reduce((a, r) => a + Number(r.values[3] || 0), 0)
        expect(sum).toBe(Number(m.rows.at(-1).values[3] ?? 0))
      }),
      { numRuns: 100 }
    )
  })

  it('сворачивание уменьшает число строк и не меняет общий итог', () => {
    fc.assert(
      fc.property(rowsArb, fc.array(fc.constantFrom('A', 'B', 'C', '(пусто)'), { maxLength: 3 }), (rows, keys) => {
        const collapsed = new Set(keys)
        const all = buildTableModel(rows, columns, { grandTotal: true })
        const folded = buildTableModel(rows, columns, { collapsed, grandTotal: true })

        expect(folded.rows.length).toBeLessThanOrEqual(all.rows.length)
        expect(Number(folded.rows.at(-1).values[3] ?? 0)).toBe(Number(all.rows.at(-1).values[3] ?? 0))
      }),
      { numRuns: 100 }
    )
  })

  it('объединения не выходят за границы массива и не накрывают подпись итога своей колонки', () => {
    fc.assert(
      fc.property(rowsArb, fc.array(fc.constantFrom('A', 'B', 'C'), { maxLength: 2 }), (rows, keys) => {
        const m = buildTableModel(rows, columns, { collapsed: new Set(keys), grandTotal: true })
        for (const merge of m.merges) {
          expect(merge.row).toBeGreaterThanOrEqual(0)
          expect(merge.row + merge.rowspan).toBeLessThanOrEqual(m.rows.length)
          expect(merge.rowspan).toBeGreaterThan(0)
          for (let i = merge.row; i < merge.row + merge.rowspan; i++) {
            const row = m.rows[i]
            const ownLabelCol = row.kind === 'subtotal' ? row.groupPath.length - 1 : -1
            expect(merge.col).not.toBe(ownLabelCol)
          }
        }
      }),
      { numRuns: 100 }
    )
  })

  it('пустой набор данных даёт только строку ИТОГО', () => {
    const m = buildTableModel([], columns, { grandTotal: true })
    expect(m.rows).toHaveLength(1)
    expect(m.rows[0].kind).toBe('grandtotal')
    expect(m.rows[0].values[3]).toBeNull() // SUM по пустому набору — null, в UI это «—»
    expect(m.merges).toEqual([])
  })
})
