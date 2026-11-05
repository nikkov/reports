import { describe, expect, it } from 'vitest'
import { aggregateSeries } from './group.js'

const rows = [
  { region: 'Север', month: 'янв', amount: 100, qty: 1 },
  { region: 'Север', month: 'янв', amount: 50, qty: 2 },
  { region: 'Север', month: 'фев', amount: 30, qty: 3 },
  { region: 'Юг', month: 'янв', amount: 200, qty: 4 },
  { region: null, month: 'фев', amount: 10, qty: 5 }
]

describe('aggregateSeries', () => {
  it('суммирует по категориям', () => {
    const r = aggregateSeries(rows, { xField: 'region', yField: 'amount', aggregate: 'sum' })
    expect(r.categories).toEqual(['Север', 'Юг', '(пусто)'])
    expect(r.series).toHaveLength(1)
    expect(r.series[0].data).toEqual([180, 200, 10])
  })

  it('разбивает на серии по seriesField', () => {
    const r = aggregateSeries(rows, {
      xField: 'month',
      seriesField: 'region',
      yField: 'amount',
      aggregate: 'sum'
    })
    expect(r.categories).toEqual(['янв', 'фев'])
    const north = r.series.find((s) => s.name === 'Север')
    expect(north.data).toEqual([150, 30])
    const south = r.series.find((s) => s.name === 'Юг')
    expect(south.data).toEqual([200, null]) // во «фев» у Юга нет строк
  })

  it('поддерживает avg/count/min/max', () => {
    const avg = aggregateSeries(rows, { xField: 'region', yField: 'amount', aggregate: 'avg' })
    expect(avg.series[0].data[0]).toBeCloseTo(60, 6)
    const cnt = aggregateSeries(rows, { xField: 'region', yField: 'amount', aggregate: 'count' })
    expect(cnt.series[0].data).toEqual([3, 1, 1])
    const mx = aggregateSeries(rows, { xField: 'region', yField: 'amount', aggregate: 'max' })
    expect(mx.series[0].data).toEqual([100, 200, 10])
  })

  it('пропускает нечисловые значения', () => {
    const r = aggregateSeries([{ k: 'a', v: 'oops' }, { k: 'a', v: 5 }], { xField: 'k', yField: 'v' })
    expect(r.series[0].data).toEqual([5])
  })
})
