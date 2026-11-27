import { describe, expect, it } from 'vitest'
import { cssColorValue } from './index.js'

// ECharts не понимает var(--rep-*): значения умолчаний стилей нужно резолвить (ADR-184)
describe('cssColorValue', () => {
  it('обычный цвет возвращается как есть', () => {
    expect(cssColorValue('#123456')).toBe('#123456')
    expect(cssColorValue('rgb(1, 2, 3)')).toBe('rgb(1, 2, 3)')
  })

  it('не строка и пустое значение не ломают вызов', () => {
    expect(cssColorValue(undefined)).toBeUndefined()
    expect(cssColorValue(null)).toBeNull()
  })

  it('var() без запасного значения резолвится из темы или в пустую строку', () => {
    // в jsdom переменная не задана — получаем пустую строку, а не «var(--x)»
    expect(cssColorValue('var(--rep-table-text)')).not.toContain('var(')
  })

  it('var() с запасным значением отдаёт его, если переменной нет', () => {
    expect(cssColorValue('var(--нет-такой-переменной, #abcdef)')).toBe('#abcdef')
  })
})
