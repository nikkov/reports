import { describe, expect, it } from 'vitest'
import { parseBlockHeight, resolveBlockHeight, resolveBlockSpan } from './layout.js'

describe('parseBlockHeight', () => {
  it('число — пиксели', () => {
    expect(parseBlockHeight(480)).toEqual({ kind: 'px', value: 480 })
    expect(parseBlockHeight(0)).toEqual({ kind: 'px', value: 0 })
  })

  it('строка с процентом — доля окна', () => {
    expect(parseBlockHeight('60%')).toEqual({ kind: 'percent', value: 60 })
    expect(parseBlockHeight(' 45 % ')).toBeNull() // пробел внутри — не наш формат
    expect(parseBlockHeight('100%')).toEqual({ kind: 'percent', value: 100 })
  })

  it('мусор и отсутствие значения → null', () => {
    expect(parseBlockHeight(undefined)).toBeNull()
    expect(parseBlockHeight('')).toBeNull()
    expect(parseBlockHeight('auto')).toBeNull()
    expect(parseBlockHeight('60px')).toBeNull()
    expect(parseBlockHeight(NaN)).toBeNull()
    expect(parseBlockHeight({})).toBeNull()
  })
})

describe('resolveBlockHeight', () => {
  it('пиксели проходят как есть, но не ниже минимума типа', () => {
    expect(resolveBlockHeight('table', 640, 1080)).toBe(640)
    expect(resolveBlockHeight('chart', 320, 1080)).toBe(320)
    expect(resolveBlockHeight('chart', 100, 1080)).toBe(160)
    expect(resolveBlockHeight('table', 5000, 1080)).toBe(2000)
  })

  it('процент считается от высоты окна', () => {
    expect(resolveBlockHeight('table', '50%', 1000)).toBe(500)
    expect(resolveBlockHeight('chart', '40%', 800)).toBe(320)
    expect(resolveBlockHeight('table', '10%', 900)).toBe(200) // минимум таблицы важнее 90 px
  })

  it('без height — умолчания по типу', () => {
    expect(resolveBlockHeight('table', undefined, 1000)).toBe(460)
    expect(resolveBlockHeight('chart', undefined, 1000)).toBe(320)
  })

  it('процент без окна не даёт нулевой высоты', () => {
    expect(resolveBlockHeight('chart', '50%', 0)).toBe(160)
    expect(resolveBlockHeight('chart', '50%', NaN)).toBe(160)
  })
})

describe('resolveBlockSpan (ADR-96)', () => {
  it('span 1…12 берётся как есть', () => {
    expect(resolveBlockSpan({ span: 7 })).toBe(7)
    expect(resolveBlockSpan({ span: 1 })).toBe(1)
    expect(resolveBlockSpan({ span: 12 })).toBe(12)
  })

  it('старый width: half — половина, full и отсутствие — вся строка', () => {
    expect(resolveBlockSpan({ width: 'half' })).toBe(6)
    expect(resolveBlockSpan({ width: 'full' })).toBe(12)
    expect(resolveBlockSpan({})).toBe(12)
    expect(resolveBlockSpan(null)).toBe(12)
  })

  it('span главнее width, некорректный span игнорируется', () => {
    expect(resolveBlockSpan({ span: 4, width: 'half' })).toBe(4)
    expect(resolveBlockSpan({ span: 13, width: 'half' })).toBe(6)
    expect(resolveBlockSpan({ span: 0 })).toBe(12)
    expect(resolveBlockSpan({ span: 3.5 })).toBe(12)
    expect(resolveBlockSpan({ span: '5' })).toBe(5)
  })
})
