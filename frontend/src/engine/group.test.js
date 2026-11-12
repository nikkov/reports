import { describe, expect, it } from 'vitest'
import { aggregateSeries, buildTableModel, computeMerges, evaluateRowExpression } from './group.js'

//             0          1           2          3        4        5
const columns = [
  { field: 'region', title: 'Регион', group: true, subtotal: true },
  { field: 'category', title: 'Категория', group: true },
  { field: 'product', title: 'Продукт' },
  { field: 'amount', title: 'Сумма', aggregate: 'sum' },
  { field: 'profit', title: 'Прибыль', aggregate: 'sum' },
  { title: 'Рентаб.', expression: 'sum(profit) / sum(amount)' }
]

const rows = [
  { region: 'Север', category: 'А', product: 'p1', amount: 100, profit: 10 },
  { region: 'Север', category: 'А', product: 'p2', amount: 100, profit: 30 },
  { region: 'Север', category: 'Б', product: 'p3', amount: 50, profit: 5 },
  { region: 'Юг', category: 'А', product: 'p4', amount: 250, profit: 25 },
  { region: null, category: 'А', product: 'p5', amount: 10, profit: 1 }
]

// Серверная агрегация (ADR-37): в модели одна строка на группу. Детали, подгруженные
// по клику (ADR-65), показываются как детальные строки, а итог группы остаётся серверным.
describe('buildTableModel: детали группы, подгруженные по требованию', () => {
  const aggColumns = [
    { field: 'category', title: 'Категория', group: true },
    { field: 'amount', title: 'Сумма', aggregate: 'sum' },
    { field: 'profit', title: 'Прибыль', aggregate: 'sum' }
  ]
  const aggRows = [
    { category: 'Мебель', amount: 1000, profit: 200 },
    { category: 'Свет', amount: 500, profit: 100 }
  ]
  const details = new Map([
    ['Мебель', { rows: [{ category: 'Мебель', amount: 20, profit: 4 }, { category: 'Мебель', amount: 30, profit: 6 }], total: 2 }]
  ])

  it('в свёрнутом виде деталей нет, есть только итог', () => {
    const m = buildTableModel(aggRows, aggColumns, { grandTotal: true, details, collapsed: new Set(['Мебель']) })
    expect(m.rows.map((r) => r.kind)).toEqual(['subtotal', 'detail', 'grandtotal'])
    expect(m.rows[0].values[1]).toBe(1000) // серверный итог, не 50 из загруженной части
  })

  it('раскрытая группа показывает загруженные строки, итог — серверный', () => {
    const m = buildTableModel(aggRows, aggColumns, { grandTotal: true, details })
    const kinds = m.rows.map((r) => r.kind)
    expect(kinds).toEqual(['detail', 'detail', 'subtotal', 'detail', 'grandtotal'])

    const detailAmounts = m.rows.filter((r) => r.kind === 'detail' && r.groupPath[0] === 'Мебель').map((r) => r.values[1])
    expect(detailAmounts).toEqual([20, 30])

    const subtotal = m.rows.find((r) => r.kind === 'subtotal')
    expect(subtotal.values[0]).toBe('Итого Мебель')
    expect(subtotal.values[1]).toBe(1000) // серверная сумма по ВСЕЙ группе, а не 50
    expect(m.rows.at(-1).values[1]).toBe(1500) // общий итог из серверных строк
  })

  it('без загруженных деталей поведение прежнее', () => {
    const m = buildTableModel(aggRows, aggColumns, { grandTotal: true })
    expect(m.rows.filter((r) => r.kind === 'detail')).toHaveLength(2)
  })
})

// Покрытие всех агрегатов: avg/count/countDistinct и неизвестная функция (даёт null).
describe('buildTableModel: агрегаты', () => {
  const cols = [
    { field: 'region', title: 'Регион', group: true },
    { field: 'amount', title: 'Среднее', aggregate: 'avg' },
    { field: 'amount', title: 'Количество', aggregate: 'count' },
    { field: 'amount', title: 'Разных', aggregate: 'countDistinct' },
    { field: 'amount', title: 'Неизвестный', aggregate: 'median' }
  ]

  it('считает avg, count, countDistinct и null для неизвестной функции', () => {
    const m = buildTableModel(rows, cols, { grandTotal: true })
    const grand = m.rows.at(-1)
    expect(grand.values[1]).toBeCloseTo(102) // (100+100+50+250+10)/5
    expect(grand.values[2]).toBe(5)
    expect(grand.values[3]).toBe(4) // 100, 50, 250, 10
    expect(grand.values[4]).toBe(null)
  })
})

describe('aggregateSeries', () => {
  it('avg/count/min/max по категориям и сумма по умолчанию', () => {
    const series = [
      { x: 'A', y: 10, s: 'a' }, { x: 'A', y: 20, s: 'a' },
      { x: 'B', y: 5, s: 'a' }
    ]
    const avg = aggregateSeries(series, { xField: 'x', seriesField: 's', yField: 'y', aggregate: 'avg' })
    expect(avg.series[0].data).toEqual([15, 5])
    const count = aggregateSeries(series, { xField: 'x', seriesField: 's', yField: 'y', aggregate: 'count' })
    expect(count.series[0].data).toEqual([2, 1])
    const min = aggregateSeries(series, { xField: 'x', seriesField: 's', yField: 'y', aggregate: 'min' })
    expect(min.series[0].data).toEqual([10, 5])
    const max = aggregateSeries(series, { xField: 'x', seriesField: 's', yField: 'y', aggregate: 'max' })
    expect(max.series[0].data).toEqual([20, 5])
    const sum = aggregateSeries(series, { xField: 'x', seriesField: 's', yField: 'y' })
    expect(sum.series[0].data).toEqual([30, 5])
  })
})

describe('buildTableModel', () => {
  it('строит дерево и подытог на каждый регион', () => {
    const m = buildTableModel(rows, columns, { grandTotal: true })
    expect(m.rows.filter((r) => r.kind === 'subtotal')).toHaveLength(3) // Север, Юг, (пусто)
    expect(m.rows.filter((r) => r.kind === 'detail')).toHaveLength(5)
    expect(m.rows.at(-1).kind).toBe('grandtotal')

    const north = m.rows.find((r) => r.kind === 'subtotal' && r.groupPath[0] === 'Север')
    expect(north.values[3]).toBe(250) // сумма
    expect(north.values[5]).toBeCloseTo(45 / 250, 6) // рентабельность по области
    expect(north.values[0]).toBe('Итого Север')
  })

  it('общий итог равен сумме подытогов и всех деталей', () => {
    const m = buildTableModel(rows, columns, { grandTotal: true })
    const grand = m.rows.at(-1)
    expect(grand.values[3]).toBe(510)
    expect(grand.values[4]).toBe(71)
    expect(grand.values[5]).toBeCloseTo(71 / 510, 6)

    const subSum = m.rows
      .filter((r) => r.kind === 'subtotal' && r.groupPath.length === 1)
      .reduce((a, r) => a + r.values[3], 0)
    expect(subSum).toBe(grand.values[3])
  })

  it('null-ключ попадает в группу «(пусто)»', () => {
    const m = buildTableModel(rows, columns, {})
    expect(m.rows.some((r) => r.groupPath[0] === '(пусто)')).toBe(true)
  })

  it('свёрнутая группа отдаёт только свой итог', () => {
    const m = buildTableModel(rows, columns, { collapsed: new Set(['Север', '(пусто)']) })
    const north = m.rows.filter((r) => r.groupPath[0] === 'Север')
    expect(north).toHaveLength(1)
    expect(north[0].kind).toBe('subtotal')
    expect(m.rows.filter((r) => r.kind === 'detail').length).toBeLessThan(5)
  })

  it('вычисляемая колонка не падает при нулевом знаменателе', () => {
    const cols = [
      { field: 'region', title: 'Регион', group: true },
      { field: 'amount', title: 'Сумма', aggregate: 'sum' },
      { title: 'Доля', expression: 'sum(profit) / sum(amount)' }
    ]
    const m = buildTableModel([{ region: 'X', amount: 0, profit: 5 }], cols, {})
    expect(m.rows[0].values[2]).toBeNull()
  })

  it('агрегаты avg/min/max/count', () => {
    const cols = [
      { field: 'region', title: 'Регион', group: true },
      { field: 'amount', title: 'avg', aggregate: 'avg' },
      { field: 'amount', title: 'min', aggregate: 'min' },
      { field: 'amount', title: 'max', aggregate: 'max' },
      { field: 'amount', title: 'cnt', aggregate: 'count' }
    ]
    const m = buildTableModel(rows, cols, {})
    const grand = m.rows.at(-1)
    expect(grand.values[1]).toBeCloseTo(510 / 5, 6)
    expect(grand.values[2]).toBe(10)
    expect(grand.values[3]).toBe(250)
    expect(grand.values[4]).toBe(5)
  })
})

describe('computeMerges', () => {
  it('строка итога входит в объединение предков, но не своей колонки', () => {
    // детали категории A/x, её строка итога (уровень 1), затем другая группа
    const flat = [
      { kind: 'detail', groupPath: ['A', 'x'] },
      { kind: 'detail', groupPath: ['A', 'x'] },
      { kind: 'subtotal', groupPath: ['A', 'x'] },
      { kind: 'detail', groupPath: ['B', 'y'] }
    ]
    const merges = computeMerges(flat, 2)

    // колонка региона включает строку итога категории — регион не «отрывается»
    expect(merges).toContainEqual({ row: 0, col: 0, rowspan: 3, colspan: 1 })
    // колонка категории — только детали, иначе подпись «Итого …» спрячется
    expect(merges).toContainEqual({ row: 0, col: 1, rowspan: 2, colspan: 1 })
  })

  it('объединение региона не накрывает строку итога самого региона', () => {
    const flat = [
      { kind: 'detail', groupPath: ['A', 'x'] },
      { kind: 'subtotal', groupPath: ['A'] },
      { kind: 'detail', groupPath: ['B', 'y'] }
    ]
    const merges = computeMerges(flat, 2)
    expect(merges).toEqual([{ row: 0, col: 0, rowspan: 1, colspan: 1 }].filter(() => false))
  })
})

describe('evaluateRowExpression (плоская таблица)', () => {
  it('считает агрегат по одной строке как значение этой строки', () => {
    const row = { amount: 1000, profit: 250 }
    expect(evaluateRowExpression('sum(profit) / sum(amount)', row)).toBeCloseTo(0.25)
    expect(evaluateRowExpression('sum(amount) - sum(profit)', row)).toBe(750)
    expect(evaluateRowExpression('count(amount)', row)).toBe(1)
  })

  it('даёт null для пустого поля и при делении на ноль', () => {
    expect(evaluateRowExpression('sum(profit) / sum(amount)', { amount: 0, profit: 10 })).toBeNull()
    expect(evaluateRowExpression('sum(profit) / sum(amount)', { amount: 100, profit: null })).toBeNull()
  })
})

describe('вычисляемые колонки в детальных строках группы', () => {
  it('считает выражение по своей строке, а не оставляет пусто', () => {
    const rows = [
      { region: 'Север', product: 'A', plan_amount: 100, fact_amount: 110 },
      { region: 'Север', product: 'B', plan_amount: 200, fact_amount: 190 }
    ]
    const cols = [
      { field: 'region', title: 'Регион', group: true, subtotal: true },
      { field: 'product', title: 'Продукт' },
      { field: 'plan_amount', title: 'План', aggregate: 'sum' },
      { field: 'fact_amount', title: 'Факт', aggregate: 'sum' },
      { title: 'Отклонение', expression: 'sum(fact_amount) - sum(plan_amount)' },
      { title: 'Исполнение', expression: 'sum(fact_amount) / sum(plan_amount)' }
    ]
    const m = buildTableModel(rows, cols, { grandTotal: true })
    const details = m.rows.filter((r) => r.kind === 'detail')
    expect(details).toHaveLength(2)
    expect(details[0].values[4]).toBe(10)
    expect(details[0].values[5]).toBeCloseTo(1.1)
    const subtotal = m.rows.find((r) => r.kind === 'subtotal')
    expect(subtotal.values[4]).toBe(0)
    expect(subtotal.values[5]).toBeCloseTo(1)
  })
})

describe('buildTableModel: плоская таблица без группировки (ADR-95)', () => {
  const flatColumns = [
    { field: 'product', title: 'Продукт' },
    { field: 'amount', title: 'Сумма', aggregate: 'sum' },
    { field: 'profit', title: 'Прибыль', aggregate: 'sum' },
    { title: 'Маржа', expression: 'sum(profit) / sum(amount)' }
  ]
  const flatRows = [
    { product: 'p1', amount: 100, profit: 10 },
    { product: 'p2', amount: 300, profit: 30 }
  ]

  it('каждая строка набора становится детальной, порядок сохраняется', () => {
    const model = buildTableModel(flatRows, flatColumns, {})
    const details = model.rows.filter((r) => r.kind === 'detail')
    expect(details).toHaveLength(2)
    expect(details.map((r) => r.values[0])).toEqual(['p1', 'p2'])
    // вычисляемая колонка считается по строке, а не по всей таблице
    expect(details[1].values[3]).toBeCloseTo(30 / 300)
  })

  it('ИТОГО считает суммы и стоит в первой колонке', () => {
    const model = buildTableModel(flatRows, flatColumns, {})
    const total = model.rows.at(-1)
    expect(total.kind).toBe('grandtotal')
    expect(total.values[0]).toBe('ИТОГО')
    expect(total.values[1]).toBe(400)
    expect(total.values[2]).toBe(40)
  })

  it('без grandTotal остаются только детали', () => {
    const model = buildTableModel(flatRows, flatColumns, { grandTotal: false })
    expect(model.rows).toHaveLength(2)
    expect(model.rows.every((r) => r.kind === 'detail')).toBe(true)
  })

  it('пустой набор даёт только ИТОГО (или ничего без grandTotal)', () => {
    expect(buildTableModel([], flatColumns, {}).rows).toHaveLength(1)
    expect(buildTableModel([], flatColumns, { grandTotal: false }).rows).toHaveLength(0)
  })
})
