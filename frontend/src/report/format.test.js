import { describe, expect, it } from 'vitest'
import { columnFormat, formatCell, normalizeFormat } from './format.js'

const NBSP = '\u00a0'

describe('formatCell: числа', () => {
  it('по умолчанию — разделитель тысяч и запятая', () => {
    expect(formatCell('number', undefined, 1356707500)).toBe('1' + NBSP + '356' + NBSP + '707' + NBSP + '500')
    expect(formatCell('number', undefined, 0.25)).toBe('0,25')
  })

  it('integer отбрасывает дробную часть', () => {
    expect(formatCell('integer', undefined, 252.4)).toBe('252')
    expect(formatCell('integer', 2, 252.4)).toBe('252')
  })

  it('decimals задаёт число знаков', () => {
    expect(formatCell('number', 2, 1234.5)).toBe('1' + NBSP + '234,50')
    expect(formatCell('number', 0, 1234.5)).toBe('1' + NBSP + '235')
  })

  it('отрицательные: минус или скобки', () => {
    expect(formatCell('number', 0, -1500)).toBe('-1' + NBSP + '500')
    expect(formatCell({ style: 'number', decimals: 0, negative: 'parentheses' }, undefined, -1500)).toBe(
      '(1' + NBSP + '500)'
    )
  })

  it('свои разделители, префикс и суффикс', () => {
    expect(
      formatCell({ style: 'number', decimals: 2, group: ' ', decimal: '.', prefix: '$' }, undefined, 1234.5)
    ).toBe('$1 234.50')
    expect(formatCell({ style: 'number', decimals: 0, suffix: ' шт.' }, undefined, 42)).toBe('42 шт.')
  })

  it('проценты умножают на 100, по умолчанию 1 знак', () => {
    expect(formatCell('percent', undefined, 0.2054)).toBe('20,5 %')
    expect(formatCell('percent', 2, 0.2054)).toBe('20,54 %')
    expect(formatCell({ style: 'percent', decimals: 0 }, undefined, 0.99)).toBe('99 %')
  })

  it('валюта: две знака и знак рубля по умолчанию', () => {
    expect(formatCell('currency', undefined, 1234.5)).toBe('1' + NBSP + '234,50' + ' ' + NBSP + '₽')
  })

  it('даты: ISO → 05.03.2026, с временем — добавляет часы', () => {
    expect(formatCell('date', undefined, '2026-03-05')).toBe('05.03.2026')
    expect(formatCell('date', undefined, '2026-03-05T14:30:00')).toBe('05.03.2026 14:30')
    expect(formatCell('date', undefined, 'не дата')).toBe('не дата')
  })

  it('шаблон даты из шаблона отчёта (ADR-88)', () => {
    expect(formatCell({ style: 'date', pattern: 'YYYY-MM-DD' }, undefined, '2026-03-05')).toBe('2026-03-05')
    expect(formatCell({ style: 'date', pattern: 'DD.MM.YY' }, undefined, '2026-03-05')).toBe('05.03.26')
    expect(formatCell({ style: 'date', pattern: 'D.M' }, undefined, '2026-03-05')).toBe('5.3')
    expect(formatCell({ pattern: 'DD/MM/YYYY' }, undefined, '2026-03-05')).toBe('05/03/2026')
    expect(formatCell({ style: 'date', pattern: 'DD.MM.YYYY HH:mm' }, undefined, '2026-03-05T14:30:00')).toBe(
      '05.03.2026 14:30'
    )
    expect(formatCell({ style: 'date', pattern: 'YYYY-MM-DD HH:mm:ss' }, undefined, '2026-03-05T14:30:07')).toBe(
      '2026-03-05 14:30:07'
    )
    // минут нет (только дата) — получаем «00:00», а не пустоту
    expect(formatCell({ style: 'date', pattern: 'DD.MM.YYYY HH:mm' }, undefined, '2026-03-05')).toBe('05.03.2026 00:00')
  })

  it('пустые значения не форматируются', () => {
    expect(formatCell('number', undefined, null)).toBeNull()
    expect(formatCell('number', undefined, '')).toBeNull()
  })
})

describe('columnFormat: источник формата', () => {
  it('формат стиля колонки сильнее формата колонки', () => {
    const col = { format: 'number', decimals: 0 }
    expect(columnFormat(col, { format: { style: 'currency', decimals: 2 } }).format).toEqual({
      style: 'currency',
      decimals: 2
    })
  })

  it('тип даты даёт формат даты, агрегатная колонка — число', () => {
    expect(columnFormat({ type: 'date' }).format).toBe('date')
    expect(columnFormat({ aggregate: 'sum' }).format).toBe('number')
    expect(columnFormat({})).toBeNull()
  })
})

describe('normalizeFormat', () => {
  it('строка превращается в объект с умолчаниями', () => {
    expect(normalizeFormat('percent', 2)).toMatchObject({ style: 'percent', decimals: 2, decimal: ',' })
  })
})
