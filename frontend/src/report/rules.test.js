import { describe, expect, it } from 'vitest'
import { compare, matchWhen, resolveColor, ruleApplies, ruleStyleFor } from './rules.js'

const row = { amount: 1000, profit: -250, margin: 0.05, product: 'Товар-12', order_date: '2026-03-05', empty: null }
const getValue = (column) => row[column]

describe('compare: типы и операторы', () => {
  it('числа сравниваются как числа, а не как строки', () => {
    expect(compare(1000, '>', 900)).toBe(true)
    expect(compare('1000', '>', 900)).toBe(true)
    expect(compare(9, '>', 10)).toBe(false)
  })

  it('строки сравниваются лексикографически', () => {
    expect(compare('Товар-12', 'contains', 'товар')).toBe(true)
    expect(compare('Товар-12', '=', 'Товар-12')).toBe(true)
    expect(compare('Товар-12', '!=', 'Товар-13')).toBe(true)
  })

  it('ISO-даты сравниваются как даты', () => {
    expect(compare('2026-03-05', '>=', '2026-03-01')).toBe(true)
    expect(compare('2026-03-05', '<', '2026-03-01')).toBe(false)
  })

  it('between проверяет попадание в диапазон', () => {
    expect(compare(0.05, 'between', [0, 0.1])).toBe(true)
    expect(compare(0.15, 'between', [0, 0.1])).toBe(false)
  })

  it('null не проходит числовые сравнения, но ловится empty/notEmpty', () => {
    expect(compare(null, '<', 0)).toBe(false)
    expect(compare(null, 'empty', undefined)).toBe(true)
    expect(compare('', 'notEmpty', undefined)).toBe(false)
  })
})

describe('matchWhen: одиночные условия и all/any', () => {
  it('одиночное условие', () => {
    expect(matchWhen({ column: 'profit', op: '<', value: 0 }, getValue)).toBe(true)
  })

  it('all требует выполнения всех', () => {
    const when = { all: [{ column: 'profit', op: '<', value: 0 }, { column: 'margin', op: '<', value: 0.1 }] }
    expect(matchWhen(when, getValue)).toBe(true)
    expect(matchWhen({ all: [{ column: 'profit', op: '<', value: 0 }, { column: 'margin', op: '>', value: 0.5 }] }, getValue)).toBe(false)
  })

  it('any требует хотя бы одного', () => {
    const when = { any: [{ column: 'amount', op: '>', value: 5000 }, { column: 'profit', op: '<', value: 0 }] }
    expect(matchWhen(when, getValue)).toBe(true)
  })

  it('неизвестный оператор не срабатывает', () => {
    expect(matchWhen({ column: 'profit', op: '~', value: 0 }, getValue)).toBe(false)
  })
})

describe('ruleApplies: зоны', () => {
  const rule = { when: { column: 'profit', op: '<', value: 0 }, apply: { colors: { text: '#b42318' } } }

  it('по умолчанию работает на строках данных и подытогах', () => {
    expect(ruleApplies(rule, null, 'detail', getValue)).toBe(true)
    expect(ruleApplies(rule, null, 'subtotal', getValue)).toBe(true)
    expect(ruleApplies(rule, null, 'grandtotal', getValue)).toBe(false)
  })

  it('zones ограничивает набор', () => {
    expect(ruleApplies({ ...rule, zones: ['grandtotal'] }, null, 'detail', getValue)).toBe(false)
    expect(ruleApplies({ ...rule, zones: ['grandtotal'] }, null, 'grandtotal', getValue)).toBe(true)
  })
})

describe('ruleStyleFor: слияние и приоритет', () => {
  const rules = [
    { when: { column: 'profit', op: '<', value: 0 }, apply: { colors: { text: 'danger' } } },
    { when: { column: 'profit', op: '<', value: 0 }, apply: { font: { weight: 600 } }, priority: 1 }
  ]
  const palette = { danger: '#b42318' }

  it('сливает несколько правил и разворачивает палитру', () => {
    expect(ruleStyleFor(rules, 'detail', 'profit', getValue, palette)).toEqual({
      color: '#b42318',
      fontWeight: '600'
    })
  })

  it('scope cell действует только на целевую колонку', () => {
    expect(ruleStyleFor(rules, 'detail', 'amount', getValue, palette)).toBeNull()
  })

  it('scope row действует на любую колонку строки', () => {
    const rowRules = [{ ...rules[0], scope: 'row' }]
    expect(ruleStyleFor(rowRules, 'detail', 'amount', getValue, palette)).toEqual({ color: '#b42318' })
  })

  it('target перенаправляет стиль на другую колонку', () => {
    const t = [{ when: { column: 'profit', op: '<', value: 0 }, target: 'amount', apply: { colors: { background: 'warning' } } }]
    expect(ruleStyleFor(t, 'detail', 'amount', getValue, { warning: '#fff4d6' })).toEqual({ background: '#fff4d6' })
    expect(ruleStyleFor(t, 'detail', 'profit', getValue, {})).toBeNull()
  })

  it('при равном приоритете побеждает объявленное позже', () => {
    const two = [
      { when: { column: 'profit', op: '<', value: 0 }, apply: { colors: { text: '#111111' } } },
      { when: { column: 'profit', op: '<', value: 0 }, apply: { colors: { text: '#222222' } } }
    ]
    expect(ruleStyleFor(two, 'detail', 'profit', getValue, {})).toEqual({ color: '#222222' })
  })

  it('без правил возвращает null', () => {
    expect(ruleStyleFor([], 'detail', 'profit', getValue, {})).toBeNull()
  })
})

describe('resolveColor', () => {
  it('палитра подменяет имя, hex остаётся как есть', () => {
    expect(resolveColor('danger', { danger: '#b42318' })).toBe('#b42318')
    expect(resolveColor('#123456', {})).toBe('#123456')
  })
})
