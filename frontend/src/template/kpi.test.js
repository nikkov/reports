import { describe, expect, it } from 'vitest'
import { validateTemplate } from './validate.js'

// KPI-карточка (ADR-159): обязательные поля, предупреждения, ограничения полей диаграмм.
const base = (blocks) => ({
  version: 2,
  id: 'kpi-test',
  title: 'KPI',
  params: [],
  datasets: { d: { sql: 'SELECT 1 AS v, 2 AS p' } },
  blocks
})

const messages = (res) => res.errors.map((e) => e.message).join(' | ')

describe('валидатор: KPI и новые поля диаграмм', () => {
  it('корректная карточка проходит', () => {
    const res = validateTemplate(base([
      { type: 'kpi', dataKey: 'd', valueField: 'v', compareField: 'p', title: 'V' }
    ]))
    expect(res.errors).toEqual([])
  })

  it('без valueField — ошибка', () => {
    const res = validateTemplate(base([{ type: 'kpi', dataKey: 'd', title: 'V' }]))
    expect(messages(res)).toContain('valueField')
  })

  it('без сравнения — предупреждение, а не ошибка', () => {
    const res = validateTemplate(base([{ type: 'kpi', dataKey: 'd', valueField: 'v' }]))
    expect(res.errors).toEqual([])
    expect(res.warnings.map((w) => w.message).join(' ')).toContain('compareField')
  })

  it('sparkline требует xField и yField', () => {
    const res = validateTemplate(base([
      { type: 'chart', dataKey: 'd', chartType: 'sparkline', xField: 'v' }
    ]))
    expect(messages(res)).toContain('yField')
  })

  it('orientation допустим только у bar и только horizontal/vertical', () => {
    const bad = validateTemplate(base([
      { type: 'chart', dataKey: 'd', chartType: 'line', xField: 'v', yField: 'p', orientation: 'horizontal' }
    ]))
    expect(messages(bad)).toContain('orientation')
    const ok = validateTemplate(base([
      { type: 'chart', dataKey: 'd', chartType: 'bar', xField: 'v', yField: 'p', orientation: 'horizontal', showValues: true }
    ]))
    expect(ok.errors).toEqual([])
  })

  it('reference принимает число и объект, но не строку', () => {
    const number = validateTemplate(base([
      { type: 'chart', dataKey: 'd', chartType: 'bar', xField: 'v', yField: 'p', reference: 10 }
    ]))
    expect(number.errors).toEqual([])
    const object = validateTemplate(base([
      { type: 'chart', dataKey: 'd', chartType: 'bar', xField: 'v', yField: 'p', reference: { value: 10, label: 'план' } }
    ]))
    expect(object.errors).toEqual([])
    const bad = validateTemplate(base([
      { type: 'chart', dataKey: 'd', chartType: 'bar', xField: 'v', yField: 'p', reference: 'план' }
    ]))
    expect(bad.errors.some((e) => e.path.includes('reference'))).toBe(true)
  })
})
