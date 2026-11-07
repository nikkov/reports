import { describe, expect, it } from 'vitest'
import { CURRENT_VERSION, migrateTemplate } from './migrate.js'

const base = (extra = {}) => ({ id: 't', title: 'T', blocks: [{ type: 'text', text: 'x' }], ...extra })

describe('версия формата и миграции (ADR-51)', () => {
  it('шаблон без version получает актуальную версию', () => {
    const result = migrateTemplate(base())
    expect(result.ok).toBe(true)
    expect(result.fromVersion).toBe(0)
    expect(result.toVersion).toBe(CURRENT_VERSION)
    expect(result.doc.version).toBe(CURRENT_VERSION)
    expect(result.steps.length).toBeGreaterThanOrEqual(2)
  })

  it('у диаграммы версии 1 появляется chartType, заданный не трогается', () => {
    const result = migrateTemplate(
      base({
        version: 1,
        blocks: [
          { type: 'chart', dataKey: 's', xField: 'x', yField: 'y' },
          { type: 'chart', dataKey: 's', chartType: 'pie', xField: 'x', yField: 'y' }
        ]
      })
    )
    expect(result.ok).toBe(true)
    expect(result.doc.version).toBe(2)
    expect(result.doc.blocks[0].chartType).toBe('bar')
    expect(result.doc.blocks[1].chartType).toBe('pie')
    expect(result.steps[0]).toMatch(/^1→2/)
  })

  it('не меняет исходный объект', () => {
    const source = base({ version: 1 })
    migrateTemplate(source)
    expect(source.version).toBe(1)
  })

  it('шаблон новее сервера отклоняется', () => {
    const result = migrateTemplate(base({ version: 99 }))
    expect(result.ok).toBe(false)
    expect(result.error).toContain('99')
  })

  it('нецелая версия — ошибка', () => {
    const result = migrateTemplate(base({ version: '2' }))
    expect(result.ok).toBe(false)
    expect(result.error).toContain('целым')
  })

  it('актуальная версия остаётся без шагов', () => {
    const result = migrateTemplate(base({ version: CURRENT_VERSION }))
    expect(result.ok).toBe(true)
    expect(result.steps).toEqual([])
  })
})
