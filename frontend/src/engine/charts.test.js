import { describe, expect, it } from 'vitest'
import { buildBandModel, buildGaugeModel, buildSankeyModel } from './charts.js'

const rows = [
  { category: 'Ноутбуки', region: 'Восток', amount: 100 },
  { category: 'Ноутбуки', region: 'Запад', amount: 50 },
  { category: 'Мониторы', region: 'Восток', amount: 25 },
  { category: 'Мониторы', region: '', amount: 10 } // пустой приёмник — поток отбрасывается
]

// Многоуровневый sankey (ADR-68): цепочка уровней вместо пары source/target.
describe('sankey: цепочка уровней', () => {
  const path = [
    { region: 'Восток', category: 'Одежда', product: 'Футболка', v: 10 },
    { region: 'Восток', category: 'Одежда', product: 'Куртка', v: 5 },
    { region: 'Восток', category: 'Обувь', product: 'Кроссовки', v: 7 },
    { region: 'Юг', category: 'Одежда', product: 'Футболка', v: 3 },
    { region: 'Юг', category: 'Обувь', product: '', v: 99 }, // пустой уровень — строка отбрасывается
    { region: '', category: 'Одежда', product: 'Куртка', v: 4 } // пустой верхний — тоже
  ]

  it('строит связи между соседними уровнями и суммирует вес', () => {
    const model = buildSankeyModel(path, { levels: ['region', 'category', 'product'], valueField: 'v' })

    // 3 региона? нет: Восток и Юг — на первом уровне, 2 категории, 3 товара
    expect(model.nodes).toHaveLength(7)
    // Восток→Одежда, Восток→Обувь, Юг→Одежда, Одежда→Футболка, Одежда→Куртка, Обувь→Кроссовки
    expect(model.links).toHaveLength(6)
    const value = (from, to) => model.links.find((l) => l.source === from && l.target === to)?.value
    expect(value('1\u0001Обувь', '2\u0001Кроссовки')).toBe(7)
    expect(value('0\u0001Восток', '1\u0001Одежда')).toBe(15)
    expect(value('0\u0001Восток', '1\u0001Обувь')).toBe(7)
    expect(value('1\u0001Одежда', '2\u0001Футболка')).toBe(13) // Восток 10 + Юг 3
    expect(value('1\u0001Одежда', '2\u0001Куртка')).toBe(5)
    expect(model.dropped).toBe(2)
  })

  it('одинаковые названия на разных уровнях не склеиваются', () => {
    const same = [{ a: 'Восток', b: 'Восток', v: 1 }]
    const model = buildSankeyModel(same, { levels: ['a', 'b'], valueField: 'v' })
    expect(model.nodes.map((n) => n.name)).toEqual(['0\u0001Восток', '1\u0001Восток'])
    expect(model.links).toHaveLength(1)
    // подписи — исходные значения без префикса уровня
    expect(model.label('0\u0001Восток')).toBe('Восток')
  })

  it('пара source/target работает как раньше', () => {
    const model = buildSankeyModel(path, { sourceField: 'region', targetField: 'category', valueField: 'v' })
    expect(model.nodes.map((n) => n.name)).toContain('Восток')
    expect(model.label('Восток')).toBe('Восток')
    expect(model.levels).toBe(null)
  })
})

describe('gauge: одно число из выборки', () => {
  it('sum по умолчанию', () => {
    expect(buildGaugeModel(rows, { yField: 'amount' }).value).toBe(185)
  })

  it('avg, min, max, count', () => {
    expect(buildGaugeModel(rows, { yField: 'amount', aggregate: 'avg' }).value).toBeCloseTo(46.25)
    expect(buildGaugeModel(rows, { yField: 'amount', aggregate: 'min' }).value).toBe(10)
    expect(buildGaugeModel(rows, { yField: 'amount', aggregate: 'max' }).value).toBe(100)
    expect(buildGaugeModel(rows, { yField: 'amount', aggregate: 'count' }).value).toBe(4)
  })

  it('countDistinct считает разные значения', () => {
    const dup = [{ v: 1 }, { v: 1 }, { v: 2 }]
    expect(buildGaugeModel(dup, { yField: 'v', aggregate: 'countDistinct' }).value).toBe(2)
  })

  it('нечисловые и пустые значения не ломают расчёт', () => {
    const messy = [{ v: '10' }, { v: null }, { v: 'нет' }, { v: 5 }]
    const model = buildGaugeModel(messy, { yField: 'v' })
    expect(model.value).toBe(15)
    expect(model.rows).toBe(2)
  })

  it('шкала: min по умолчанию 0, max подбирается выше значения', () => {
    const model = buildGaugeModel(rows, { yField: 'amount' })
    expect(model.min).toBe(0)
    expect(model.max).toBeGreaterThan(model.value)
  })

  it('явные min/max сохраняются, unit и name прокидываются', () => {
    const model = buildGaugeModel(rows, { yField: 'amount', min: 0, max: 200, unit: '%', name: 'Выполнение' })
    expect([model.min, model.max, model.unit, model.name]).toEqual([0, 200, '%', 'Выполнение'])
  })

  it('пустая выборка даёт нулевую шкалу, а не деление на ноль', () => {
    const model = buildGaugeModel([], { yField: 'amount', min: 5 })
    expect(model.value).toBe(0)
    expect(model.max).toBeGreaterThan(model.min)
  })
})

describe('sankey: потоки между узлами', () => {
  it('суммирует вес по паре источник → приёмник', () => {
    const model = buildSankeyModel(rows, {
      sourceField: 'category',
      targetField: 'region',
      valueField: 'amount'
    })
    expect(model.links).toEqual([
      { source: 'Ноутбуки', target: 'Восток', value: 100 },
      { source: 'Ноутбуки', target: 'Запад', value: 50 },
      { source: 'Мониторы', target: 'Восток', value: 25 }
    ])
    expect(model.nodes.map((n) => n.name)).toEqual(['Ноутбуки', 'Восток', 'Запад', 'Мониторы'])
    expect(model.dropped).toBe(1)
  })

  it('avg даёт средний вес пары', () => {
    const model = buildSankeyModel(rows, {
      sourceField: 'category',
      targetField: 'region',
      valueField: 'amount',
      aggregate: 'avg'
    })
    const monitors = model.links.find((l) => l.source === 'Мониторы')
    expect(monitors.value).toBe(25) // одна строка в паре
  })

  it('нерисуемые потоки (0 и отрицательные) отбрасываются', () => {
    const withZero = [
      { a: 'A', b: 'B', v: 0 },
      { a: 'A', b: 'C', v: -5 },
      { a: 'A', b: 'D', v: 7 }
    ]
    const model = buildSankeyModel(withZero, { sourceField: 'a', targetField: 'b', valueField: 'v' })
    expect(model.links).toEqual([{ source: 'A', target: 'D', value: 7 }])
  })

  it('пустые имена узлов пропускаются целиком', () => {
    const model = buildSankeyModel(
      [{ a: '', b: 'B', v: 1 }, { a: 'A', b: null, v: 2 }, { a: 'A', b: 'B', v: 3 }],
      { sourceField: 'a', targetField: 'b', valueField: 'v' }
    )
    expect(model.links).toEqual([{ source: 'A', target: 'B', value: 3 }])
    expect(model.nodes.map((n) => n.name)).toEqual(['A', 'B'])
  })

  it('вырожденный поток «узел в себя» остаётся в модели', () => {
    const model = buildSankeyModel([{ a: 'A', b: 'A', v: 4 }], {
      sourceField: 'a',
      targetField: 'b',
      valueField: 'v'
    })
    expect(model.links).toEqual([{ source: 'A', target: 'A', value: 4 }])
    expect(model.nodes).toEqual([{ name: 'A' }])
  })
})

describe('sankey: остальные агрегаты веса', () => {
  const many = [
    { a: 'A', b: 'B', v: 4 },
    { a: 'A', b: 'B', v: 10 }
  ]

  it('min и max берут границы пары', () => {
    expect(buildSankeyModel(many, { sourceField: 'a', targetField: 'b', valueField: 'v', aggregate: 'min' }).links[0].value).toBe(4)
    expect(buildSankeyModel(many, { sourceField: 'a', targetField: 'b', valueField: 'v', aggregate: 'max' }).links[0].value).toBe(10)
  })

  it('count считает строки пары', () => {
    expect(buildSankeyModel(many, { sourceField: 'a', targetField: 'b', valueField: 'v', aggregate: 'count' }).links[0].value).toBe(2)
  })

  it('неизвестный агрегат работает как sum', () => {
    expect(buildSankeyModel(many, { sourceField: 'a', targetField: 'b', valueField: 'v', aggregate: 'median' }).links[0].value).toBe(14)
  })
})

describe('gauge: крайние случаи шкалы', () => {
  it('значение ниже min не даёт шкалу нулевой длины', () => {
    const model = buildGaugeModel([{ v: 1 }], { yField: 'v', min: 10 })
    expect(model.max).toBe(11)
  })

  it('max равный значению сохраняется как задан', () => {
    const model = buildGaugeModel([{ v: 50 }], { yField: 'v', max: 50 })
    expect(model.max).toBe(50)
  })

  it('неизвестный агрегат считается суммой', () => {
    expect(buildGaugeModel([{ v: 2 }, { v: 3 }], { yField: 'v', aggregate: 'median' }).value).toBe(5)
  })

  it('пустые строки и отсутствие поля не ломают модель', () => {
    expect(buildGaugeModel([{}, { v: null }], { yField: 'v' }).value).toBe(0)
    const empty = buildSankeyModel([], { sourceField: 'a', targetField: 'b', valueField: 'v' })
    expect(empty.nodes).toEqual([])
    expect(empty.links).toEqual([])
    expect(empty.dropped).toBe(0)
  })
})

describe('пустая выборка для каждого агрегата gauge', () => {
  for (const aggregate of ['avg', 'min', 'max', 'count', 'countDistinct']) {
    it(`${aggregate} без строк даёт 0, а не NaN`, () => {
      expect(buildGaugeModel([], { yField: 'v', aggregate }).value).toBe(0)
    })
  }
})

describe('gauge: цветовые зоны по четырём границам', () => {
  const rows = [{ v: 20.54 }]
  const thresholds = [10, 15, 25, 30]
  const zoneColors = ['#c62828', '#f9a825', '#2e7d32', '#f9a825', '#c62828']

  it('четыре границы дают пять зон и пять цветов', () => {
    const model = buildGaugeModel(rows, { yField: 'v', min: 0, max: 40, thresholds, zoneColors })
    expect(model.zones).toEqual([
      { from: 0, to: 10, color: '#c62828' },
      { from: 10, to: 15, color: '#f9a825' },
      { from: 15, to: 25, color: '#2e7d32' },
      { from: 25, to: 30, color: '#f9a825' },
      { from: 30, to: 40, color: '#c62828' }
    ])
  })

  it('значение в зелёной зоне красит указатель в зелёный', () => {
    const model = buildGaugeModel(rows, { yField: 'v', min: 0, max: 40, thresholds, zoneColors })
    expect(model.value).toBeCloseTo(20.54, 2)
    expect(model.color).toBe('#2e7d32')
  })

  it('значение в жёлтой зоне и выше последней границы — свои цвета', () => {
    const yellow = buildGaugeModel([{ v: 12 }], { yField: 'v', min: 0, max: 40, thresholds, zoneColors })
    expect(yellow.color).toBe('#f9a825')
    const red = buildGaugeModel([{ v: 35 }], { yField: 'v', min: 0, max: 40, thresholds, zoneColors })
    expect(red.color).toBe('#c62828')
  })

  it('ниже первой границы — первый цвет', () => {
    expect(buildGaugeModel([{ v: 3 }], { yField: 'v', min: 0, max: 40, thresholds, zoneColors }).color).toBe('#c62828')
  })

  it('цвет может быть токеном палитры', () => {
    const palette = { bad: '#b00020', warn: '#f9a825', good: '#2e7d32' }
    const model = buildGaugeModel([{ v: 20 }], {
      yField: 'v', min: 0, max: 40, palette,
      thresholds, zoneColors: ['bad', 'warn', 'good', 'warn', 'bad']
    })
    expect(model.zones.map((z) => z.color)).toEqual(['#b00020', '#f9a825', '#2e7d32', '#f9a825', '#b00020'])
    expect(model.color).toBe('#2e7d32')
  })

  it('несовпадение числа цветов и границ отключает зоны, а не ломает рендер', () => {
    const model = buildGaugeModel([{ v: 20 }], { yField: 'v', min: 0, max: 40, thresholds, zoneColors: ['#fff'] })
    expect(model.zones).toEqual([])
    expect(model.color).toBeNull()
    expect(model.value).toBe(20)
  })

  it('границы вне шкалы обрезаются по min/max', () => {
    const model = buildGaugeModel([{ v: 5 }], {
      yField: 'v', min: 0, max: 20, thresholds: [2, 6, 18, 30], zoneColors
    })
    expect(model.zones[0]).toEqual({ from: 0, to: 2, color: '#c62828' })
    // зона p3…p4 обрезана верхом шкалы: она жёлтая, и красная зона «выше p4» в шкалу не попадает
    expect(model.zones[model.zones.length - 1]).toEqual({ from: 18, to: 20, color: '#f9a825' })
  })
})

describe('band: значение и коридор между границами', () => {
  const rows = [
    { day: '2026-03-01', v: 202, lo: 127, hi: 277 },
    { day: '2026-03-02', v: 365, lo: 290, hi: 440 },
    { day: '2026-03-03', v: 164.7, lo: 53, hi: 423 }
  ]
  const fields = { xField: 'day', yField: 'v', lowField: 'lo', highField: 'hi' }

  it('раскладывает три числа по рядам', () => {
    const model = buildBandModel(rows, fields)
    expect(model.categories).toEqual(['2026-03-01', '2026-03-02', '2026-03-03'])
    expect(model.values).toEqual([202, 365, 164.7])
    expect(model.lows).toEqual([127, 290, 53])
    expect(model.highs).toEqual([277, 440, 423])
    expect(model.band).toEqual([150, 150, 370])
  })

  it('перепутанные границы не дают отрицательной полосы', () => {
    const model = buildBandModel([{ day: 'd', v: 5, lo: 90, hi: 10 }], fields)
    expect(model.highs[0]).toBe(90)
    expect(model.band[0]).toBe(0)
  })

  it('строка без границ остаётся точкой без полосы', () => {
    const model = buildBandModel([{ day: 'd', v: 5, lo: null, hi: null }], fields)
    expect(model.values).toEqual([5])
    expect(model.band).toEqual([null])
  })

  it('дубли категории не дублируются (линия не идёт назад)', () => {
    const model = buildBandModel([{ day: 'd', v: 1, lo: 0, hi: 2 }, { day: 'd', v: 9, lo: 0, hi: 2 }], fields)
    expect(model.values).toEqual([1])
  })

  it('строка без категории пропускается', () => {
    const model = buildBandModel([{ day: '', v: 1, lo: 0, hi: 2 }, { day: 'd', v: 2, lo: 0, hi: 3 }], fields)
    expect(model.categories).toEqual(['d'])
  })
})
