import { describe, expect, it } from 'vitest'
import { validateTemplate } from './validate.js'

const base = () => ({
  version: 1,
  id: 'sales-summary',
  title: 'Продажи',
  params: [
    { name: 'from', type: 'date', label: 'с', required: true, default: '2026-01-01' },
    { name: 'to', type: 'date', label: 'по', required: true, default: '2026-03-31' }
  ],
  datasets: { sales: { sql: 'SELECT 1', params: { from: '$from', to: '$to' } } },
  blocks: [
    { type: 'text', text: 'Период {{from}} — {{to}}' },
    {
      type: 'table',
      dataKey: 'sales',
      grandTotal: true,
      columns: [
        { field: 'region', title: 'Регион', group: true, subtotal: true },
        { field: 'product', title: 'Продукт' },
        { field: 'amount', title: 'Сумма', aggregate: 'sum', format: 'number' },
        { title: 'Рентаб.', expression: 'sum(profit) / sum(amount)', format: 'percent' }
      ]
    },
    { type: 'chart', dataKey: 'sales', chartType: 'line', xField: 'region', yField: 'amount', aggregate: 'sum' }
  ]
})

const paths = (res) => res.errors.map((e) => e.path)

describe('validateTemplate', () => {
  it('принимает корректный шаблон', () => {
    const res = validateTemplate(base())
    expect(res.errors).toEqual([])
  })

  it('ловит неизвестный набор данных', () => {
    const t = base()
    t.blocks[1].dataKey = 'nope'
    expect(paths(validateTemplate(t))).toContain('blocks.1.dataKey')
  })

  it('ловит ссылку на необъявленный параметр', () => {
    const t = base()
    t.datasets.sales.params.from = '$period'
    expect(paths(validateTemplate(t))).toContain('datasets.sales.params.from')
  })

  it('ловит групповую колонку после обычной', () => {
    const t = base()
    t.blocks[1].columns = [
      { field: 'product', title: 'Продукт' },
      { field: 'region', title: 'Регион', group: true }
    ]
    expect(paths(validateTemplate(t))).toContain('blocks.1.columns.1')
  })

  it('ловит подытог у негрупповой колонки', () => {
    const t = base()
    t.blocks[1].columns[1].subtotal = true
    expect(paths(validateTemplate(t))).toContain('blocks.1.columns.1')
  })

  it('ловит ошибку схемы: неизвестный format', () => {
    const t = base()
    t.blocks[1].columns[2].format = 'money'
    const res = validateTemplate(t)
    expect(res.errors.length).toBeGreaterThan(0)
  })

  it('ловит колонку одновременно с field и expression', () => {
    const t = base()
    t.blocks[1].columns[3].field = 'profit'
    const res = validateTemplate(t)
    expect(res.errors.length).toBeGreaterThan(0)
  })

  it('предупреждает (но не валит) диаграмму по отсутствующему в таблице полю', () => {
    const t = base()
    t.blocks[2].yField = 'total'
    const res = validateTemplate(t)
    expect(res.errors).toEqual([])
    expect(res.warnings.map((w) => w.path)).toContain('blocks.2.yField')
  })

  it('ловит необъявленный параметр в тексте блока', () => {
    const t = base()
    t.blocks[0].text = 'Период {{period}}'
    expect(paths(validateTemplate(t))).toContain('blocks.0.text')
  })

  it('принимает заголовок-группу children и видит его листья', () => {
    const t = base()
    t.blocks[1].columns = [
      { field: 'region', title: 'Регион', group: true },
      {
        title: 'Финансы',
        children: [
          { field: 'amount', title: 'Сумма', aggregate: 'sum' },
          { field: 'profit', title: 'Прибыль', aggregate: 'sum' }
        ]
      }
    ]
    expect(validateTemplate(t).errors).toEqual([])
  })

  it('ловит данные у колонки-группы', () => {
    const t = base()
    t.blocks[1].columns.push({ title: 'Группа', field: 'amount', children: [{ field: 'profit', title: 'П' }] })
    expect(paths(validateTemplate(t))).toContain('blocks.1.columns.4')
  })

  it('ловит группировку по колонке-заголовку', () => {
    const t = base()
    t.blocks[1].columns = [{ title: 'Группа', group: true, children: [{ field: 'amount', title: 'С' }] }]
    expect(paths(validateTemplate(t))).toContain('blocks.1.columns.0')
  })

  it('принимает справочник из БД и каскад от него', () => {
    const t = base()
    t.params = [
      { name: 'region', type: 'select', source: { kind: 'lookup', sql: 'SELECT id::text AS value, name AS label FROM regions' }, required: true },
      { name: 'category', type: 'select', source: { kind: 'lookup', sql: 'SELECT DISTINCT category AS value, category AS label FROM sales WHERE region_id = :region::int', dependsOn: 'region' } }
    ]
    t.datasets.sales.params = { from: '$region', to: '$region' }
    t.blocks[0].text = 'Регион {{region}}'
    expect(validateTemplate(t).errors).toEqual([])
  })

  it('ловит справочник без sql', () => {
    const t = base()
    t.params = [{ name: 'region', type: 'select', source: { kind: 'lookup' } }]
    expect(paths(validateTemplate(t))).toContain('params.0.source')
  })

  it('ловит устаревший queryId в справочнике', () => {
    const t = base()
    t.params = [{ name: 'region', type: 'select', source: { kind: 'lookup', queryId: 'lookup_regions' } }]
    const msgs = validateTemplate(t).errors.map((e) => e.message).join(' ')
    expect(msgs).toContain('queryId')
  })

  it('ловит статический набор без options', () => {
    const t = base()
    t.params = [{ name: 'cat', type: 'select', source: { kind: 'static' } }]
    expect(paths(validateTemplate(t))).toContain('params.0.source')
  })

  it('ловит множественный выбор у даты', () => {
    const t = base()
    t.params = [{ name: 'from', type: 'date', multiple: true, source: { kind: 'date' } }]
    expect(paths(validateTemplate(t))).toContain('params.0.multiple')
  })

  it('ловит зависимость от неизвестного или объявленного ниже параметра', () => {
    const t = base()
    t.params = [
      { name: 'category', type: 'select', source: { kind: 'lookup', queryId: 'c', dependsOn: 'region' } },
      { name: 'region', type: 'select', source: { kind: 'lookup', queryId: 'r' } }
    ]
    const errs = paths(validateTemplate(t))
    expect(errs).toContain('params.0.source.dependsOn')

    t.params = [
      { name: 'category', type: 'select', source: { kind: 'lookup', queryId: 'c', dependsOn: 'nope' } }
    ]
    expect(paths(validateTemplate(t))).toContain('params.0.source.dependsOn')
  })

  it('ловит зависимость от множественного выбора', () => {
    const t = base()
    t.params = [
      { name: 'region', type: 'select', multiple: true, source: { kind: 'lookup', queryId: 'r' } },
      { name: 'category', type: 'select', source: { kind: 'lookup', queryId: 'c', dependsOn: 'region' } }
    ]
    expect(paths(validateTemplate(t))).toContain('params.1.source.dependsOn')
  })

  it('ловит синтаксическую ошибку в выражении', () => {
    const t = base()
    t.blocks[1].columns.push({ title: 'Плохо', expression: 'round(sum(profit))' })
    const idx = t.blocks[1].columns.length - 1
    expect(paths(validateTemplate(t))).toContain(`blocks.1.columns.${idx}`)
  })

  it('ловит недопустимый символ в выражении', () => {
    const t = base()
    t.blocks[1].columns.push({ title: 'Условие', expression: 'sum(profit) > 0' })
    const idx = t.blocks[1].columns.length - 1
    const res = validateTemplate(t)
    expect(res.errors.map((e) => e.path)).toContain(`blocks.1.columns.${idx}`)
    expect(res.errors.find((e) => e.path === `blocks.1.columns.${idx}`).message).toContain('>')
  })

  it('предупреждает об опечатке в поле выражения, не валя шаблон', () => {
    const t = base()
    t.blocks[1].columns.push({ title: 'Опечатка', expression: 'sum(profit) / sum(nope)' })
    const idx = t.blocks[1].columns.length - 1
    const res = validateTemplate(t)
    expect(res.errors).toEqual([])
    expect(res.warnings.map((w) => w.path)).toContain(`blocks.1.columns.${idx}`)
  })

  it('ловит лишнее поле в шаблоне', () => {
    const t = base()
    t.blocks[0].unknown = 1
    const res = validateTemplate(t)
    expect(res.errors.some((e) => e.path === 'blocks.0.unknown')).toBe(true)
  })
})

describe('стили таблицы (ADR-23)', () => {
  const withStyles = (styles) => {
    const t = base()
    t.styles = styles
    return t
  }

  it('принимает пресет и высоты зон', () => {
    const res = validateTemplate(withStyles({ preset: 'compact', layout: { rowHeight: 22, headerHeight: 24 } }))
    expect(res.errors).toEqual([])
  })

  it('ловит неизвестный ключ стиля', () => {
    const res = validateTemplate(withStyles({ header: { backgroundColor: '#fff' } }))
    expect(res.errors.map((e) => e.path)).toContain('styles.header.backgroundColor')
  })

  it('ловит цвет вне формата', () => {
    const res = validateTemplate(withStyles({ colors: { text: 'red' } }))
    expect(res.errors.length).toBeGreaterThan(0)
  })

  it('ловит шрифт вне allowlist', () => {
    const res = validateTemplate(withStyles({ font: { family: 'Comic Sans MS' } }))
    expect(res.errors.length).toBeGreaterThan(0)
  })

  it('ловит стиль колонки для несуществующего поля', () => {
    const res = validateTemplate(withStyles({ columns: { nope: { width: 100 } } }))
    expect(res.errors.map((e) => e.path)).toContain('styles.columns.nope')
  })

  it('ловит minWidth больше maxWidth', () => {
    const res = validateTemplate(withStyles({ columns: { amount: { minWidth: 200, maxWidth: 100 } } }))
    expect(res.errors.map((e) => e.path)).toContain('styles.columns.amount')
  })

  it('предупреждает о невидимом тексте и малой высоте строки', () => {
    const res = validateTemplate(
      withStyles({
        font: { size: 20 },
        layout: { rowHeight: 16 },
        header: { colors: { text: '#ffffff', background: '#ffffff' } }
      })
    )
    const paths = res.warnings.map((w) => w.path)
    expect(paths).toContain('styles.header.colors')
    expect(paths).toContain('styles.layout.rowHeight')
  })

  it('принимает ширину в процентах и px', () => {
    const res = validateTemplate(withStyles({ columns: { region: { width: '20%' }, amount: { width: 140 } } }))
    expect(res.errors).toEqual([])
  })
})

describe('условное форматирование (ADR-25)', () => {
  const withRules = (rules, styles) => {
    const t = base()
    t.blocks[1].rules = rules
    if (styles) t.styles = styles
    return t
  }

  it('принимает правило с палитрой', () => {
    const res = validateTemplate(
      withRules([{ when: { column: 'amount', op: '>', value: 0 }, apply: { colors: { text: 'danger' } } }], {
        palette: { danger: '#b42318' }
      })
    )
    expect(res.errors).toEqual([])
  })

  it('ловит цвет вне палитры', () => {
    const res = validateTemplate(withRules([]) && withRules([], { palette: {} }))
    expect(res.errors).toEqual([])
    const bad = validateTemplate(withRules([], { colors: { text: 'danger' }, palette: {} }))
    expect(bad.errors.map((e) => e.path)).toContain('styles.colors.text')
  })

  it('ловит правило по несуществующей колонке', () => {
    const res = validateTemplate(withRules([{ when: { column: 'nope', op: '>', value: 0 }, apply: {} }]))
    expect(res.errors.map((e) => e.path)).toContain('blocks.1.rules.0.when.column')
  })

  it('ловит not between без пары значений', () => {
    const res = validateTemplate(withRules([{ when: { column: 'amount', op: 'between', value: 5 }, apply: {} }]))
    expect(res.errors.map((e) => e.path)).toContain('blocks.1.rules.0.when.value')
  })

  it('ловит оператор без значения', () => {
    const res = validateTemplate(withRules([{ when: { column: 'amount', op: '>' }, apply: {} }]))
    expect(res.errors.map((e) => e.path)).toContain('blocks.1.rules.0.when.value')
  })

  it('ловит target на несуществующую колонку', () => {
    const res = validateTemplate(
      withRules([{ when: { column: 'amount', op: '>', value: 0 }, target: 'nope', apply: {} }])
    )
    expect(res.errors.map((e) => e.path)).toContain('blocks.1.rules.0.target')
  })

  it('принимает all/any без вложенности', () => {
    const res = validateTemplate(
      withRules([
        { when: { all: [{ column: 'amount', op: '>', value: 0 }, { column: 'region', op: '=', value: 'Восток' }] }, apply: {} }
      ])
    )
    expect(res.errors).toEqual([])
  })
})

describe('автообновление (ADR-39)', () => {
  const withRefresh = (value) => ({ ...base(), refreshSeconds: value })

  it('принимает период в допустимом диапазоне', () => {
    expect(validateTemplate(withRefresh(30)).errors).toEqual([])
    expect(validateTemplate(withRefresh(0)).errors).toEqual([])
  })

  it('отклоняет не целое и выход за границы', () => {
    expect(validateTemplate(withRefresh(1.5)).errors.map((e) => e.path)).toContain('refreshSeconds')
    expect(validateTemplate(withRefresh(4000)).errors.map((e) => e.path)).toContain('refreshSeconds')
    expect(validateTemplate(withRefresh(-5)).errors.map((e) => e.path)).toContain('refreshSeconds')
  })

  it('слишком частый период — предупреждение, а не ошибка', () => {
    const res = validateTemplate(withRefresh(2))
    expect(res.errors).toEqual([])
    expect(res.warnings.map((w) => w.path)).toContain('refreshSeconds')
  })

  it('без поля автообновление выключено', () => {
    const res = validateTemplate(base())
    expect(res.errors).toEqual([])
    expect(res.warnings.map((w) => w.path)).not.toContain('refreshSeconds')
  })
})

describe('drill-through с диаграммы (ADR-43)', () => {
  const withChart = (drill) => {
    const t = base()
    t.blocks = [
      ...t.blocks,
      {
        type: 'chart',
        dataKey: 'sales',
        chartType: 'bar',
        xField: 'region',
        yField: 'amount',
        drill
      }
    ]
    return t
  }

  it('принимает x, series и param.<name>', () => {
    const res = validateTemplate(withChart({ report: 'sales-drill', params: { region: 'x', category: 'series', from: 'param.from' } }))
    expect(res.errors).toEqual([])
  })

  it('литерал допустим', () => {
    expect(validateTemplate(withChart({ report: 'sales-drill', params: { region: 'Восток' } })).errors).toEqual([])
  })

  it('group.<field> в диаграмме — ошибка', () => {
    const res = validateTemplate(withChart({ report: 'sales-drill', params: { region: 'group.region' } }))
    expect(res.errors.map((e) => e.path)).toContain('blocks.3.drill.params.region')
  })

  it('без report — ошибка', () => {
    const res = validateTemplate(withChart({ params: {} }))
    expect(res.errors.map((e) => e.path)).toContain('blocks.3.drill.report')
  })
})

