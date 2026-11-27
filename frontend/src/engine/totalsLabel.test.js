import { describe, expect, it } from 'vitest'
import { buildTableModel } from './group.js'

const cols = [
  { field: 'region', title: 'Регион', group: true, subtotal: true },
  { field: 'amount', title: 'Сумма', type: 'int', aggregate: 'sum' }
]
const rows = [
  { region: 'Восток', amount: 10 },
  { region: 'Восток', amount: 20 },
  { region: 'Запад', amount: 5 }
]

describe('названия строк итогов (ADR-188)', () => {
  it('по умолчанию: «Итого <значение>» и «ИТОГО»', () => {
    const model = buildTableModel(rows, cols, { grandTotal: true })
    const subs = model.rows.filter((r) => r.kind === 'subtotal')
    const grand = model.rows.find((r) => r.kind === 'grandtotal')
    expect(subs.map((r) => r.values[0])).toEqual(['Итого Восток', 'Итого Запад'])
    expect(grand.values[0]).toBe('ИТОГО')
  })

  it('свои подписи из шаблона, {value} подставляет группу', () => {
    const model = buildTableModel(rows, cols, {
      grandTotal: true,
      grandTotalLabel: 'Всего за период',
      subtotalLabel: 'Всего по региону {value}'
    })
    const subs = model.rows.filter((r) => r.kind === 'subtotal')
    const grand = model.rows.find((r) => r.kind === 'grandtotal')
    expect(subs.map((r) => r.values[0])).toEqual(['Всего по региону Восток', 'Всего по региону Запад'])
    expect(grand.values[0]).toBe('Всего за период')
  })

  it('без {value} значение группы добавляется в конец', () => {
    const model = buildTableModel(rows, cols, { grandTotal: false, subtotalLabel: 'Подытог' })
    const subs = model.rows.filter((r) => r.kind === 'subtotal')
    expect(subs.map((r) => r.values[0])).toEqual(['Подытог Восток', 'Подытог Запад'])
    expect(model.rows.some((r) => r.kind === 'grandtotal')).toBe(false)
  })

  it('суммы в строках итогов не меняются от подписи', () => {
    const model = buildTableModel(rows, cols, { grandTotal: true, grandTotalLabel: 'Всего' })
    const sub = model.rows.find((r) => r.kind === 'subtotal')
    const grand = model.rows.find((r) => r.kind === 'grandtotal')
    expect(Number(sub.values[1])).toBe(30)
    expect(Number(grand.values[1])).toBe(35)
  })
})

describe('текст в строках итогов (ADR-190)', () => {
  const cols2 = [
    { field: 'region', title: 'Регион', group: true, subtotal: true },
    { field: 'amount', title: 'Сумма', type: 'int', aggregate: 'sum' },
    { title: 'Средний чек', expression: 'sum(amount) / sum(orders)', aggregateText: '—' },
    { title: 'Комментарий', aggregate_text: 'Итого по {value}' }
  ]
  const rows2 = [
    { region: 'Восток', amount: 10, orders: 2 },
    { region: 'Восток', amount: 20, orders: 2 },
    { region: 'Запад', amount: 5, orders: 1 }
  ]

  it('в подытоге и итоге печатается текст вместо агрегата', () => {
    const model = buildTableModel(rows2, cols2, { grandTotal: true })
    const sub = model.rows.find((r) => r.kind === 'subtotal')
    const grand = model.rows.find((r) => r.kind === 'grandtotal')
    expect(sub.values[2]).toBe('—')
    expect(grand.values[2]).toBe('—')
    // суммы в соседней колонке не пострадали
    expect(Number(sub.values[1])).toBe(30)
    expect(Number(grand.values[1])).toBe(35)
  })

  it('{value} в тексте подставляет группу, в общем итоге ячейка пустая', () => {
    const model = buildTableModel(rows2, cols2, { grandTotal: true })
    const sub = model.rows.find((r) => r.kind === 'subtotal')
    const grand = model.rows.find((r) => r.kind === 'grandtotal')
    expect(sub.values[3]).toBe('Итого по Восток')
    expect(grand.values[3]).toBe('')
  })

  it('в детальных строках текст не появляется: работает выражение', () => {
    const model = buildTableModel(rows2, cols2, { grandTotal: true })
    const detail = model.rows.find((r) => r.kind === 'detail' && r.values[0] === 'Восток')
    expect(Number(detail.values[2])).toBe(5)
    expect(detail.values[3]).toBeNull()
  })
})
