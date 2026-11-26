import { describe, expect, it } from 'vitest'
import { validateTemplate } from './validate.js'

// Ширина страницы отчёта (ADR-181): пиксели 640…3840 или "full".
const tpl = (pageWidth) => ({
  version: 2,
  id: 'page-width',
  title: 'Ширина',
  ...(pageWidth === undefined ? {} : { pageWidth }),
  params: [],
  datasets: { d: { sql: 'SELECT 1 AS x' } },
  blocks: [{ type: 'text', text: 'привет' }]
})
const paths = (res) => res.errors.map((e) => e.path).join(' | ')

describe('pageWidth', () => {
  it('по умолчанию не задан — ошибок нет', () => {
    expect(validateTemplate(tpl()).errors).toEqual([])
  })

  it('число в диапазоне проходит', () => {
    expect(validateTemplate(tpl(1800)).errors).toEqual([])
    expect(validateTemplate(tpl(640)).errors).toEqual([])
    expect(validateTemplate(tpl(3840)).errors).toEqual([])
  })

  it('"full" проходит', () => {
    expect(validateTemplate(tpl('full')).errors).toEqual([])
  })

  it('слишком узко и слишком широко — ошибка pageWidth', () => {
    expect(paths(validateTemplate(tpl(639)))).toContain('pageWidth')
    expect(paths(validateTemplate(tpl(3841)))).toContain('pageWidth')
  })

  it('неизвестная строка и дробное число не принимаются', () => {
    expect(paths(validateTemplate(tpl('wide')))).toContain('pageWidth')
    expect(paths(validateTemplate(tpl(1400.5)))).toContain('pageWidth')
  })
})
