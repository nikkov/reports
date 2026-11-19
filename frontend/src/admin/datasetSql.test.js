import { describe, expect, it } from 'vitest'
import {
  datasetSpec,
  listDatasets,
  listEditableDatasets,
  listSqlDatasets,
  setDatasetSql,
  setDatasetSpec,
  sqlInfo
} from './datasetSql.js'

const template = {
  id: 'demo',
  datasets: {
    orders: { sql: 'SELECT 1' },
    prices: { rest: { path: '/api/prices' }, connection: 'crm' },
    merged: { join: { left: { dataset: 'orders' } } }
  },
  blocks: []
}

describe('datasetSql: список наборов', () => {
  it('показывает ключи и вид набора', () => {
    const list = listDatasets(template)
    expect(list.map((d) => d.key)).toEqual(['orders', 'prices', 'merged'])
    expect(list.map((d) => d.kind)).toEqual(['sql', 'rest', 'join'])
  })

  it('пустой шаблон не падает', () => {
    expect(listDatasets(null)).toEqual([])
    expect(listDatasets({})).toEqual([])
  })

  it('у REST-набора SQL пустой', () => {
    const prices = listDatasets(template).find((d) => d.key === 'prices')
    expect(prices.sql).toBe('')
  })
})

describe('datasetSql: запись SQL', () => {
  it('меняет только выбранный набор', () => {
    const next = setDatasetSql(template, 'orders', 'SELECT 2')
    expect(next.datasets.orders.sql).toBe('SELECT 2')
    expect(next.datasets.prices).toEqual(template.datasets.prices)
    // исходный объект не мутирован
    expect(template.datasets.orders.sql).toBe('SELECT 1')
  })

  it('сохраняет многострочный текст как есть', () => {
    const sql = 'SELECT a,\n       b\n  FROM t'
    const next = setDatasetSql(template, 'orders', sql)
    expect(next.datasets.orders.sql).toBe(sql)
    expect(next.datasets.orders.sql.split('\n')).toHaveLength(3)
  })

  it('неизвестный набор возвращает шаблон без изменений', () => {
    expect(setDatasetSql(template, 'nope', 'SELECT 3')).toBe(template)
    expect(setDatasetSql(null, 'orders', 'SELECT 3')).toBe(null)
  })
})

describe('datasetSql: сведения о запросе', () => {
  it('считает строки и признак многострочности', () => {
    expect(sqlInfo('SELECT 1')).toEqual({ lines: 1, multiline: false, chars: 8 })
    expect(sqlInfo('SELECT 1\nFROM t').multiline).toBe(true)
    expect(sqlInfo('').lines).toBe(1)
    expect(sqlInfo(undefined).chars).toBe(0)
  })
})

describe('datasetSql: наборы для SQL-панели', () => {
  it('только SQL-наборы отбираются отдельным списком (совместимость)', () => {
    const list = listSqlDatasets(template)
    expect(list.map((d) => d.key)).toEqual(['orders'])
    expect(list.every((d) => d.kind === 'sql')).toBe(true)
  })

  it('панель показывает и REST, и join (ADR-161)', () => {
    const list = listEditableDatasets(template)
    expect(list.map((d) => d.kind).sort()).toEqual(['join', 'rest', 'sql'])
  })

  it('шаблон без наборов даёт пустой список', () => {
    expect(listSqlDatasets({})).toEqual([])
    expect(listSqlDatasets(null)).toEqual([])
    expect(listEditableDatasets(null)).toEqual([])
  })
})

describe('datasetSql: правка REST и join как JSON', () => {
  it('datasetSpec отдаёт SQL текстом, rest и join — JSON', () => {
    expect(datasetSpec(template, 'orders').kind).toBe('sql')
    const rest = datasetSpec(template, 'prices')
    expect(rest.kind).toBe('rest')
    expect(rest.text).toContain('"path"')
    expect(datasetSpec(template, 'merged').kind).toBe('join')
    expect(datasetSpec(template, 'нет').text).toBe('')
  })

  it('смена rest меняет только эту секцию', () => {
    const { template: next, error } = setDatasetSpec(template, 'prices', 'rest', '{"path":"/api/v2"}')
    expect(error).toBe(null)
    expect(next.datasets.prices.rest.path).toBe('/api/v2')
    expect(next.datasets.orders.sql).toBe(template.datasets.orders.sql)
    // исходный шаблон не мутирован
    expect(template.datasets.prices.rest.path).not.toBe('/api/v2')
  })

  it('битый JSON не меняет шаблон и объясняет ошибку', () => {
    const { template: next, error } = setDatasetSpec(template, 'merged', 'join', '{не json}')
    expect(error).toContain('JSON не разбирается')
    expect(next).toBe(template)
  })

  it('массив вместо объекта отвергается', () => {
    const { error } = setDatasetSpec(template, 'prices', 'rest', '[1,2]')
    expect(error).toContain('JSON-объект')
  })
})
