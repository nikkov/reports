import { describe, expect, it } from 'vitest'
import { validateTemplate } from './validate.js'

const base = () => ({
  version: 1,
  id: 'sales-summary',
  title: 'Продажи',
  params: [
    { name: 'from', type: 'date', label: 'с', required: true, default: '2026-01-01' },
    { name: 'to', type: 'date', label: 'по', required: true, default: '2026-03-31' }
  ],
  datasets: { sales: { queryId: 'sales_by_region', params: { from: '$from', to: '$to' } } },
  blocks: [
    { type: 'text', text: 'Период {{from}} — {{to}}' },
    {
      type: 'table',
      dataKey: 'sales',
      grandTotal: true,
      columns: [
        { field: 'region', title: 'Регион', group: true, subtotal: true },
        { field: 'product', title: 'Продукт' },
        { field: 'amount', title: 'Сумма', aggregate: 'sum', format: 'number' },
        { title: 'Рентаб.', expression: 'sum(profit) / sum(amount)', format: 'percent' }
      ]
    },
    { type: 'chart', dataKey: 'sales', chartType: 'line', xField: 'region', yField: 'amount', aggregate: 'sum' }
  ]
})

const paths = (res) => res.errors.map((e) => e.path)

describe('validateTemplate', () => {
  it('принимает корректный шаблон', () => {
    const res = validateTemplate(base())
    expect(res.errors).toEqual([])
  })

  it('ловит неизвестный набор данных', () => {
    const t = base()
    t.blocks[1].dataKey = 'nope'
    expect(paths(validateTemplate(t))).toContain('blocks.1.dataKey')
  })

  it('ловит ссылку на необъявленный параметр', () => {
    const t = base()
    t.datasets.sales.params.from = '$period'
    expect(paths(validateTemplate(t))).toContain('datasets.sales.params.from')
  })

  it('ловит групповую колонку после обычной', () => {
    const t = base()
    t.blocks[1].columns = [
      { field: 'product', title: 'Продукт' },
      { field: 'region', title: 'Регион', group: true }
    ]
    expect(paths(validateTemplate(t))).toContain('blocks.1.columns.1')
  })

  it('ловит подытог у негрупповой колонки', () => {
    const t = base()
    t.blocks[1].columns[1].subtotal = true
    expect(paths(validateTemplate(t))).toContain('blocks.1.columns.1')
  })

  it('ловит ошибку схемы: неизвестный format', () => {
    const t = base()
    t.blocks[1].columns[2].format = 'money'
    const res = validateTemplate(t)
    expect(res.errors.length).toBeGreaterThan(0)
  })

  it('ловит колонку одновременно с field и expression', () => {
    const t = base()
    t.blocks[1].columns[3].field = 'profit'
    const res = validateTemplate(t)
    expect(res.errors.length).toBeGreaterThan(0)
  })

  it('ловит диаграмму по отсутствующему полю', () => {
    const t = base()
    t.blocks[2].yField = 'total'
    expect(paths(validateTemplate(t))).toContain('blocks.2.yField')
  })

  it('ловит необъявленный параметр в тексте блока', () => {
    const t = base()
    t.blocks[0].text = 'Период {{period}}'
    expect(paths(validateTemplate(t))).toContain('blocks.0.text')
  })

  it('принимает заголовок-группу children и видит его листья', () => {
    const t = base()
    t.blocks[1].columns = [
      { field: 'region', title: 'Регион', group: true },
      {
        title: 'Финансы',
        children: [
          { field: 'amount', title: 'Сумма', aggregate: 'sum' },
          { field: 'profit', title: 'Прибыль', aggregate: 'sum' }
        ]
      }
    ]
    expect(validateTemplate(t).errors).toEqual([])
  })

  it('ловит данные у колонки-группы', () => {
    const t = base()
    t.blocks[1].columns.push({ title: 'Группа', field: 'amount', children: [{ field: 'profit', title: 'П' }] })
    expect(paths(validateTemplate(t))).toContain('blocks.1.columns.4')
  })

  it('ловит группировку по колонке-заголовку', () => {
    const t = base()
    t.blocks[1].columns = [{ title: 'Группа', group: true, children: [{ field: 'amount', title: 'С' }] }]
    expect(paths(validateTemplate(t))).toContain('blocks.1.columns.0')
  })

  it('ловит лишнее поле в шаблоне', () => {
    const t = base()
    t.blocks[0].unknown = 1
    const res = validateTemplate(t)
    expect(res.errors.some((e) => e.path === 'blocks.0.unknown')).toBe(true)
  })
})
