import { beforeEach, describe, expect, it } from 'vitest'
import { hiddenLabels, isLegendHidden, resetLegend, toggleLegend } from './legendFilter.js'

// Фильтр серий по легенде (ADR-161): состояние живёт в модуле и общее для блоков.
describe('legendFilter', () => {
  beforeEach(() => resetLegend())

  it('переключает серию в наборе', () => {
    expect(isLegendHidden('by_region', 'Восток')).toBe(false)
    toggleLegend('by_region', 'Восток')
    expect(isLegendHidden('by_region', 'Восток')).toBe(true)
    toggleLegend('by_region', 'Восток')
    expect(isLegendHidden('by_region', 'Восток')).toBe(false)
  })

  it('разные наборы не смешиваются', () => {
    toggleLegend('a', 'Восток')
    expect(hiddenLabels('a')).toEqual(['Восток'])
    expect(hiddenLabels('b')).toEqual([])
  })

  it('сброс очищает всё', () => {
    toggleLegend('a', 'x')
    toggleLegend('b', 'y')
    resetLegend()
    expect(hiddenLabels('a')).toEqual([])
    expect(hiddenLabels('b')).toEqual([])
  })

  it('пустые ключи не ломают состояние', () => {
    toggleLegend('', '')
    expect(isLegendHidden('', '')).toBe(true)
    resetLegend()
    expect(isLegendHidden('', '')).toBe(false)
  })
})
