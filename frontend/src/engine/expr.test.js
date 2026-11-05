import { describe, expect, it } from 'vitest'
import { evaluate } from './expr.js'

const row = (data) => ({ field: (n) => data[n], agg: () => undefined })
const totals = (data) => ({ field: () => undefined, agg: (_fn, name) => data[name] })

describe('evaluate', () => {
  it('арифметика с приоритетами и скобками', () => {
    expect(evaluate('2 + 3 * 4', row({}))).toBe(14)
    expect(evaluate('(2 + 3) * 4', row({}))).toBe(20)
    expect(evaluate('-5 + 2', row({}))).toBe(-3)
  })

  it('ссылки на поля строки', () => {
    expect(evaluate('profit / amount', row({ profit: 25, amount: 100 }))).toBe(0.25)
  })

  it('агрегаты по области', () => {
    expect(evaluate('sum(profit) / sum(amount)', totals({ profit: 25, amount: 100 }))).toBe(0.25)
  })

  it('деление на ноль и null дают null', () => {
    expect(evaluate('profit / amount', row({ profit: 10, amount: 0 }))).toBeNull()
    expect(evaluate('profit / amount', row({ profit: null, amount: 10 }))).toBeNull()
  })

  it('неизвестное поле в строке даёт null, а не падение', () => {
    expect(evaluate('total * 2', row({}))).toBeNull()
  })

  it('битые выражения бросают ошибку', () => {
    expect(() => evaluate('sum(', row({}))).toThrow()
    expect(() => evaluate('2 +', row({}))).toThrow()
    expect(() => evaluate('2 $ 3', row({}))).toThrow()
  })
})
