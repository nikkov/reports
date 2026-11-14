import { describe, expect, it } from 'vitest'
import { substituteText, textParts } from './text.js'

describe('substituteText: параметры и строка запроса (ADR-101)', () => {
  it('{{param}} заменяется подписью, если она есть, иначе значением', () => {
    expect(substituteText('Период {{from}} — {{to}}', { labels: { from: 'Период с', to: 'по' } })).toBe(
      'Период Период с — по'
    )
    expect(substituteText('{{from}}', {})).toBe('{{from}}') // неизвестный не трогаем
  })

  it('{{data.column}} берётся из строки набора', () => {
    const data = { deals: 24723, total: '1356707500' }
    expect(substituteText('Заказов: {{data.deals}}, сумма {{data.total}}', { data })).toBe(
      'Заказов: 24723, сумма 1356707500'
    )
  })

  it('нет строки или колонки — подстановка пустая', () => {
    expect(substituteText('X{{data.deals}}Y', { data: null })).toBe('XY')
    expect(substituteText('X{{data.nope}}Y', { data: { deals: 1 } })).toBe('XY')
    expect(substituteText('X{{data.deals}}Y', { data: { deals: null } })).toBe('XY')
  })

  it('пробелы внутри скобок допустимы', () => {
    expect(substituteText('{{ data.deals }}', { data: { deals: 7 } })).toBe('7')
  })
})

describe('textParts: гиперссылки (ADR-101)', () => {
  it('текст без ссылок — одна часть', () => {
    expect(textParts('просто текст')).toEqual([{ type: 'text', text: 'просто текст' }])
  })

  it('ссылка http и mailto становится ссылкой', () => {
    const parts = textParts('см. [доки](https://example.com/a?b=1) и [почту](mailto:po@example.test)')
    expect(parts.filter((p) => p.type === 'link')).toEqual([
      { type: 'link', text: 'доки', href: 'https://example.com/a?b=1' },
      { type: 'link', text: 'почту', href: 'mailto:po@example.test' }
    ])
    expect(parts[0]).toEqual({ type: 'text', text: 'см. ' })
  })

  it('относительный адрес и якорь тоже ссылки — это внутренние переходы', () => {
    const parts = textParts('[реестр](report.html?report=sales-drill) [вверх](#top)')
    expect(parts.filter((p) => p.type === 'link').map((p) => p.href)).toEqual([
      'report.html?report=sales-drill',
      '#top'
    ])
  })

  it('активные схемы ссылкой не становятся', () => {
    const parts = textParts('[плохо](javascript:alert(1)) [тоже](data:text/html,x) [file](file:///etc/passwd)')
    expect(parts.every((p) => p.type === 'text')).toBe(true)
    expect(parts.map((p) => p.text).join('')).toContain('javascript:alert(1)')
  })

  it('подстановки выполняются до разбора ссылок', () => {
    const parts = textParts('данные: {{data.deals}} — [ссылка](https://example.com)', { data: { deals: 5 } })
    expect(parts[0]).toEqual({ type: 'text', text: 'данные: 5 — ' })
    expect(parts[1]).toEqual({ type: 'link', text: 'ссылка', href: 'https://example.com' })
  })
})
