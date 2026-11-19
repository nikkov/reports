import { describe, expect, it } from 'vitest'
import { validateTemplate } from './validate.js'

// Итерация 2 дашборда (ADR-160): кольцо, полукруг и блок-легенда.
const tpl = (blocks) => ({
  version: 2,
  id: 'dash',
  title: 'D',
  params: [],
  datasets: { d: { sql: 'SELECT 1 AS v' } },
  blocks
})
const text = (res) => res.errors.map((e) => `${e.path}: ${e.message}`).join(' | ')

describe('валидатор: кольцо, полукруг, легенда', () => {
  it('donut/centerField/target только для pie', () => {
    const bad = validateTemplate(tpl([
      { type: 'chart', dataKey: 'd', chartType: 'bar', xField: 'v', yField: 'v', donut: true }
    ]))
    expect(text(bad)).toContain('donut')
    const ok = validateTemplate(tpl([
      { type: 'chart', dataKey: 'd', chartType: 'pie', xField: 'v', yField: 'v', donut: true, target: 10 }
    ]))
    expect(ok.errors).toEqual([])
  })

  it('semi/showLimits только для gauge', () => {
    const bad = validateTemplate(tpl([
      { type: 'chart', dataKey: 'd', chartType: 'line', xField: 'v', yField: 'v', semi: true }
    ]))
    expect(text(bad)).toContain('semi')
    const ok = validateTemplate(tpl([
      { type: 'chart', dataKey: 'd', chartType: 'gauge', yField: 'v', semi: true, showLimits: true }
    ]))
    expect(ok.errors).toEqual([])
  })

  it('у легенды не нужен набор данных, но нужны подписи маркеров', () => {
    const ok = validateTemplate(tpl([
      { type: 'legend', items: [{ label: 'план', color: '#c62828' }, { label: 'факт' }] }
    ]))
    expect(ok.errors).toEqual([])
    const bad = validateTemplate(tpl([{ type: 'legend', items: [{}] }]))
    expect(text(bad)).toContain('label')
  })
})
