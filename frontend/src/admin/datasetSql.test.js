import { describe, expect, it } from 'vitest'
import { listDatasets, listSqlDatasets, setDatasetSql, sqlInfo } from './datasetSql.js'

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
  it('REST и join-источники не показываются', () => {
    const list = listSqlDatasets(template)
    expect(list.map((d) => d.key)).toEqual(['orders'])
    expect(list.every((d) => d.kind === 'sql')).toBe(true)
  })

  it('шаблон без наборов даёт пустой список', () => {
    expect(listSqlDatasets({})).toEqual([])
    expect(listSqlDatasets(null)).toEqual([])
  })
})
