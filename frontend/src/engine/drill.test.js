import { describe, expect, it } from 'vitest'
import { drillAllowed, levelRefs, resolveDrill } from './drill.js'

// Переход из цепочки уровней (ADR-72): в параметры идёт только то, что известно
// про кликнутый элемент. Иначе фильтр в целевом отчёте оказался бы шире клика.
describe('drill: параметры перехода', () => {
  const spec = {
    report: 'sales-drill',
    params: {
      from: 'param.from',
      to: 'param.to',
      region: 'level.region',
      category: 'level.category',
      source: 'group'
    }
  }

  it('уровни берутся из карты элемента и сосуществуют с param.*', () => {
    const out = resolveDrill(spec, {
      levels: { region: 'Восток', category: 'Бытовая техника' },
      params: { from: '2026-03-01', to: '2026-03-10' }
    })
    expect(out).toEqual([
      ['from', '2026-03-01'],
      ['to', '2026-03-10'],
      ['region', 'Восток'],
      ['category', 'Бытовая техника'],
      ['source', 'group']
    ])
  })

  it('отсутствующий уровень не подставляется пустой строкой', () => {
    const out = resolveDrill(spec, {
      levels: { category: 'Обувь' },
      params: { from: '2026-03-01', to: '2026-03-10' }
    })
    expect(out.map(([name]) => name)).not.toContain('region')
    expect(out).toContainEqual(['category', 'Обувь'])
  })

  it('переход запрещён, если нужного уровня в элементе нет', () => {
    expect(drillAllowed(spec, { category: 'Обувь' })).toBe(false)
    expect(drillAllowed(spec, { region: 'Восток', category: 'Обувь' })).toBe(true)
    // ноль и пустая строка — законные значения, а не «нет данных»
    expect(drillAllowed(spec, { region: '', category: '0' })).toBe(true)
  })

  it('x, series и node подставляются для прежних схем', () => {
    const points = { report: 'sales-drill', params: { region: 'x', category: 'series', node: 'node' } }
    const out = resolveDrill(points, { x: 'Восток', series: 'Одежда', node: 'Куртка' })
    expect(out).toEqual([
      ['region', 'Восток'],
      ['category', 'Одежда'],
      ['node', 'Куртка']
    ])
  })

  it('нестроковая ссылка пропускается, спека без params безопасна', () => {
    expect(resolveDrill({ report: 'x', params: { a: 5, b: null } }, {})).toEqual([])
    expect(resolveDrill(null, {})).toEqual([])
    expect(drillAllowed(null, {})).toBe(true) // нет требований — переходить можно
    expect(levelRefs(undefined)).toEqual([])
  })

  it('пустые x и series становятся пустой строкой, а не null', () => {
    const out = resolveDrill({ params: { region: 'x', category: 'series' } }, {})
    expect(out).toEqual([
      ['region', ''],
      ['category', '']
    ])
  })

  it('список требуемых уровней — из ссылок level.*', () => {
    expect(levelRefs(spec)).toEqual(['region', 'category'])
    expect(levelRefs({ params: { a: 'param.from', b: 'x' } })).toEqual([])
  })
})
