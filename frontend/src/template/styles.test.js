import { describe, expect, it, test } from 'vitest'
import { DEFAULTS, PRESETS, resolveStyles, rowHeightFor, zoneVars } from './styles.js'

const tpl = (styles) => ({ version: 1, id: 't', title: 'T', styles })

describe('resolveStyles: порядок применения', () => {
  it('без styles даёт вид версии 1.0', () => {
    const s = resolveStyles({ version: 1 })
    expect(s.preset).toBe('default')
    expect(s.header.colors.background).toBe(DEFAULTS.header.colors.background)
    expect(s.layout.rowHeight).toBe(26)
    expect(rowHeightFor(s.layout, 'detail')).toBe(26)
    expect(rowHeightFor(s.layout, 'subtotal')).toBe(26)
    expect(rowHeightFor(s.layout, 'grandtotal')).toBe(26)
  })

  it('пресет применяется раньше собственных стилей шаблона', () => {
    const s = resolveStyles(tpl({ preset: 'compact', layout: { rowHeight: 30 } }))
    expect(s.font.size).toBe(12) // из пресета
    expect(s.layout.rowHeight).toBe(30) // собственный стиль сильнее
  })

  it('зона перекрывает общие стили шаблона', () => {
    const s = resolveStyles(
      tpl({ colors: { text: '#111111' }, header: { colors: { text: '#ff0000' } } })
    )
    expect(s.header.colors.text).toBe('#ff0000')
    expect(s.body.colors.text).toBe('#111111')
  })

  it('стили блока сильнее стилей шаблона', () => {
    const s = resolveStyles(tpl({ layout: { rowHeight: 28 } }), { styles: { layout: { rowHeight: 40 } } })
    expect(s.layout.rowHeight).toBe(40)
  })

  it('стиль колонки перекрывает зону для своего поля', () => {
    const s = resolveStyles(
      tpl({ columns: { amount: { align: 'right', colors: { text: '#0b3d2e' } } } })
    )
    expect(s.columns.amount.align).toBe('right')
    expect(s.columns.amount.colors.text).toBe('#0b3d2e')
    expect(s.columns.other).toBeUndefined()
  })

  it('высоты зон независимы', () => {
    const s = resolveStyles(tpl({ layout: { rowHeight: 24, subtotalHeight: 30, grandtotalHeight: 36 } }))
    expect(rowHeightFor(s.layout, 'detail')).toBe(24)
    expect(rowHeightFor(s.layout, 'subtotal')).toBe(30)
    expect(rowHeightFor(s.layout, 'grandtotal')).toBe(36)
  })

  it('неизвестный пресет игнорируется без падения', () => {
    const s = resolveStyles(tpl({ preset: 'nonexistent' }))
    expect(s.preset).toBe('default')
  })

  it('все пресеты разрешаются в полный набор зон', () => {
    for (const name of Object.keys(PRESETS)) {
      const s = resolveStyles(tpl({ preset: name }))
      for (const zone of ['header', 'body', 'subtotal', 'grandtotal']) {
        expect(Object.keys(zoneVars(s[zone], 'x')).length).toBe(10)
      }
      expect(s.font.family).toBeTruthy()
    }
  })
})

test('стили набора: применяются по dataKey, блок перекрывает (ADR-91)', () => {
  const template = {
    styles: { font: { size: 14 } },
    datasets: {
      a: { sql: 'select 1', styles: { font: { size: 12, family: 'monospace' }, layout: { rowHeight: 40 } } },
      b: { sql: 'select 1' }
    }
  }

  // блок набора a получает стили набора
  const fromSet = resolveStyles(template, { dataKey: 'a' })
  expect(fromSet.font.size).toBe(12)
  expect(fromSet.font.family).toBe('monospace')
  expect(fromSet.layout.rowHeight).toBe(40)

  // собственные styles блока сильнее стилей набора, остальное от набора сохраняется
  const ownBlock = resolveStyles(template, { dataKey: 'a', styles: { font: { size: 10 } } })
  expect(ownBlock.font.size).toBe(10)
  expect(ownBlock.font.family).toBe('monospace')
  expect(ownBlock.layout.rowHeight).toBe(40)

  // набор без styles и блок без dataKey видят только глобальные стили
  expect(resolveStyles(template, { dataKey: 'b' }).font.size).toBe(14)
  expect(resolveStyles(template, {}).font.size).toBe(14)
})
