import { describe, expect, it } from 'vitest'
import { validateTemplate } from './validate.js'

// KPI-группа (ADR-164): несколько показателей в одном блоке.
const tpl = (blocks) => ({
  version: 2,
  id: 'kpi-group',
  title: 'G',
  params: [],
  datasets: { d: { sql: 'SELECT 1 AS v, 2 AS p' } },
  blocks
})
const paths = (res) => res.errors.map((e) => e.path).join(' | ')

describe('KPI-группа', () => {
  it('items[] с valueField проходят', () => {
    const res = validateTemplate(tpl([
      { type: 'kpi', dataKey: 'd', span: 12, items: [
        { title: 'A', valueField: 'v', compareField: 'p' },
        { title: 'B', valueField: 'p' }] }
    ]))
    expect(res.errors).toEqual([])
  })

  it('показатель без valueField — ошибка с точным путём', () => {
    const res = validateTemplate(tpl([
      { type: 'kpi', dataKey: 'd', items: [{ title: 'A', valueField: 'v' }, { title: 'B' }] }
    ]))
    expect(paths(res)).toContain('blocks.0.items.1.valueField')
  })

  it('valueField блока игнорируется при items[] — предупреждение', () => {
    const res = validateTemplate(tpl([
      { type: 'kpi', dataKey: 'd', valueField: 'v', items: [{ valueField: 'v' }] }
    ]))
    expect(res.errors).toEqual([])
    expect(res.warnings.map((w) => w.message).join(' ')).toContain('items')
  })

  it('без items и без valueField — ошибка', () => {
    const res = validateTemplate(tpl([{ type: 'kpi', dataKey: 'd' }]))
    expect(paths(res)).toContain('blocks.0.valueField')
  })
})
