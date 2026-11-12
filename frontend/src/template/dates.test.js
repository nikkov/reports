import { describe, expect, it } from 'vitest'
import {
  evalDateExpr,
  formatDateValue,
  looksLikeDateExpr,
  parseDateExpr,
  resolveDefaultValue
} from './dates.js'

// воскресенье, 17 мая 2026, 14:37 по локальному времени
const NOW = new Date(2026, 4, 17, 14, 37, 0)
const d = (expr, now = NOW) => formatDateValue(evalDateExpr(expr, now), 'date')
const dt = (expr, now = NOW) => formatDateValue(evalDateExpr(expr, now), 'datetime')

describe('арифметика дат (ADR-92)', () => {
  it('* — текущий момент: дата и время до минут', () => {
    expect(d('*')).toBe('2026-05-17')
    expect(dt('*')).toBe('2026-05-17T14:37')
  })

  it('смещения в часах, днях, месяцах и годах', () => {
    expect(dt('*-5h')).toBe('2026-05-17T09:37')
    expect(dt('*+3h')).toBe('2026-05-17T17:37')
    expect(d('*-1d')).toBe('2026-05-16')
    expect(d('*-30d')).toBe('2026-04-17')
    expect(d('*+1m')).toBe('2026-06-17')
    expect(d('*-1y')).toBe('2025-05-17')
    expect(dt('*-5h+1d')).toBe('2026-05-18T09:37')
  })

  it('начало периода: today, week, month, quarter, year', () => {
    expect(dt('today(*)')).toBe('2026-05-17T00:00')
    expect(d('week(*)')).toBe('2026-05-11') // понедельник
    expect(dt('month(*)')).toBe('2026-05-01T00:00')
    expect(d('quarter(*)')).toBe('2026-04-01') // II квартал
    expect(dt('year(*)')).toBe('2026-01-01T00:00')
  })

  it('смещение применяется и к началу периода', () => {
    expect(d('month(*)-1m')).toBe('2026-04-01')
    expect(d('year(*)+1y')).toBe('2027-01-01')
    expect(d('month(*)-1d')).toBe('2026-04-30')
  })

  it('часовое смещение может увести на предыдущий день', () => {
    const night = new Date(2026, 4, 17, 2, 30, 0)
    expect(d('*-5h', night)).toBe('2026-05-16')
    expect(dt('*-5h', night)).toBe('2026-05-16T21:30')
  })

  it('константы и мусор: что выражение, а что нет', () => {
    expect(looksLikeDateExpr('2026-03-01')).toBe(false)
    expect(looksLikeDateExpr('*')).toBe(true)
    expect(looksLikeDateExpr('month(*)')).toBe(true)
    expect(looksLikeDateExpr('2026-03-01T00:00')).toBe(false)
    expect(evalDateExpr('2026-03-01')).toBe(null)
  })

  it('невалидные выражения объясняют ошибку', () => {
    expect(parseDateExpr('month2(*)').ok).toBe(false)
    expect(parseDateExpr('month2(*)').error).toContain('неизвестное начало')
    expect(parseDateExpr('*+5x').ok).toBe(false)
    expect(parseDateExpr('*+5x').error).toContain('+1d')
    expect(parseDateExpr('month(*)+').ok).toBe(false)
    expect(parseDateExpr('*-5h').ok).toBe(true)
  })

  it('resolveDefaultValue: выражения только для date и datetime', () => {
    expect(resolveDefaultValue({ type: 'date', default: 'month(*)' }, NOW)).toBe('2026-05-01')
    expect(resolveDefaultValue({ type: 'datetime', default: '*-5h' }, NOW)).toBe('2026-05-17T09:37')
    expect(resolveDefaultValue({ type: 'date', default: '2026-03-01' }, NOW)).toBe('2026-03-01')
    // у текстового параметра * остаётся строкой
    expect(resolveDefaultValue({ type: 'text', default: '*' }, NOW)).toBe('*')
    expect(resolveDefaultValue({ type: 'date' }, NOW)).toBe('')
  })
})
