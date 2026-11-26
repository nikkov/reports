import { describe, expect, it } from 'vitest'
import { sortRows } from './sort.js'

const cols = [
  { field: 'region', title: 'Регион' },
  { field: 'amount', title: 'Сумма' },
  { field: 'product', title: 'Продукт' },
  { title: 'Маржа', expression: 'profit / amount' }
]

const rows = [
  { region: 'Юг', amount: 300, product: 'Товар-10', profit: 25 },   // маржа 0,083
  { region: 'Север', amount: 100, product: 'Товар-9', profit: 40 },  // маржа 0,4
  { region: 'Север', amount: 500, product: 'Товар-2', profit: 50 },  // маржа 0,1
  { region: 'Запад', amount: null, product: 'Товар-1', profit: 0 }   // маржи нет
]

describe('sortRows', () => {
  it('без правил возвращает исходный массив', () => {
    expect(sortRows(rows, cols, [])).toBe(rows)
    expect(sortRows(rows, cols, undefined)).toBe(rows)
  })

  it('одна колонка по возрастанию', () => {
    const out = sortRows(rows, cols, [{ field: 'amount', dir: 'asc' }])
    expect(out.map((r) => r.amount)).toEqual([100, 300, 500, null])
  })

  it('одна колонка по убыванию', () => {
    const out = sortRows(rows, cols, [{ field: 'amount', dir: 'desc' }])
    expect(out.map((r) => r.amount)).toEqual([500, 300, 100, null])
  })

  it('пустые значения всегда в конце', () => {
    for (const dir of ['asc', 'desc']) {
      const out = sortRows(rows, cols, [{ field: 'amount', dir }])
      expect(out[out.length - 1].amount).toBeNull()
    }
  })

  it('несколько колонок: регион, затем сумма по убыванию', () => {
    const out = sortRows(rows, cols, [
      { field: 'region', dir: 'asc' },
      { field: 'amount', dir: 'desc' }
    ])
    expect(out.map((r) => `${r.region}:${r.amount}`)).toEqual([
      'Запад:null',
      'Север:500',
      'Север:100',
      'Юг:300'
    ])
  })

  it('строки сравниваются по-русски и с числами внутри', () => {
    const out = sortRows(rows, cols, [{ field: 'product', dir: 'asc' }])
    expect(out.map((r) => r.product)).toEqual(['Товар-1', 'Товар-2', 'Товар-9', 'Товар-10'])
  })

  it('сортировка по вычисляемой колонке', () => {
    const out = sortRows(rows, cols, [{ field: 'Маржа', dir: 'desc' }])
    expect(out.map((r) => r.product)).toEqual(['Товар-9', 'Товар-2', 'Товар-10', 'Товар-1'])
  })

  it('неизвестное поле игнорируется, порядок сохраняется', () => {
    const out = sortRows(rows, cols, [{ field: 'нет-такого', dir: 'asc' }])
    expect(out.map((r) => r.product)).toEqual(rows.map((r) => r.product))
  })

  it('сортировка устойчивая: равные значения не меняют порядок', () => {
    const same = [
      { region: 'A', amount: 10, id: 1 },
      { region: 'A', amount: 10, id: 2 },
      { region: 'A', amount: 10, id: 3 }
    ]
    const out = sortRows(same, [{ field: 'amount', title: 'Сумма' }], [{ field: 'amount' }])
    expect(out.map((r) => r.id)).toEqual([1, 2, 3])
  })

  it('исходный массив не меняется', () => {
    const before = rows.map((r) => r.product)
    sortRows(rows, cols, [{ field: 'amount', dir: 'desc' }])
    expect(rows.map((r) => r.product)).toEqual(before)
  })
})
