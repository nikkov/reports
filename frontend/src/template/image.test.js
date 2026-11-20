import { describe, expect, it } from 'vitest'
import { validateTemplate } from './validate.js'

// Блок рисунка (ADR-169): ссылка на хранилище сервера, alt обязателен, высота 24…600.
const tpl = (block) => ({
  version: 2,
  id: 'image-demo',
  title: 'Рисунок',
  params: [],
  datasets: { d: { sql: 'SELECT 1 AS x' } },
  blocks: [block]
})
const SRC = '/api/assets/logo0000sample16'
const paths = (res) => res.errors.map((e) => e.path).join(' | ')

describe('блок рисунка', () => {
  it('корректный блок проходит без ошибок', () => {
    const res = validateTemplate(
      tpl({ type: 'image', src: SRC, alt: 'Логотип', height: 64, span: 3, align: 'center' })
    )
    expect(res.errors).toEqual([])
  })

  it('рисунку не нужен набор данных', () => {
    const res = validateTemplate(tpl({ type: 'image', src: SRC, alt: 'Логотип' }))
    expect(res.errors).toEqual([])
  })

  it('внешний URL в src не принимается', () => {
    const res = validateTemplate(
      tpl({ type: 'image', src: 'https://example.com/logo.png', alt: 'Логотип' })
    )
    expect(paths(res)).toContain('blocks.0.src')
  })

  it('alt обязателен', () => {
    const res = validateTemplate(tpl({ type: 'image', src: SRC, alt: '' }))
    expect(paths(res)).toContain('blocks.0.alt')
  })

  it('высота вне 24…600 — ошибка', () => {
    const low = validateTemplate(tpl({ type: 'image', src: SRC, alt: 'Л', height: 10 }))
    const high = validateTemplate(tpl({ type: 'image', src: SRC, alt: 'Л', height: 900 }))
    expect(paths(low)).toContain('blocks.0.height')
    expect(paths(high)).toContain('blocks.0.height')
  })

  it('fit и align проверяются по списку', () => {
    const res = validateTemplate(
      tpl({ type: 'image', src: SRC, alt: 'Л', fit: 'stretch', align: 'middle' })
    )
    expect(paths(res)).toContain('blocks.0.fit')
    expect(paths(res)).toContain('blocks.0.align')
  })

  it('ссылка допускается только http(s)', () => {
    const ok = validateTemplate(
      tpl({ type: 'image', src: SRC, alt: 'Л', link: 'https://example.com' })
    )
    const bad = validateTemplate(tpl({ type: 'image', src: SRC, alt: 'Л', link: 'javascript:alert(1)' }))
    expect(ok.errors).toEqual([])
    expect(paths(bad)).toContain('blocks.0.link')
  })

  it('высота блока не путается с высотой рисунка', () => {
    const res = validateTemplate(tpl({ type: 'image', src: SRC, alt: 'Л', height: 120 }))
    expect(res.errors.some((e) => /height допустим только/i.test(e.message))).toBe(false)
  })
})
