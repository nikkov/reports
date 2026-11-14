import { describe, expect, it } from 'vitest'
import ExcelJS from 'exceljs'
import { resolveStyles } from '../../template/styles.js'
import { buildWorkbook } from './xlsx.js'

// Группировка в книге задаётся outline-уровнями строк. collapsible: false означает,
// что группы раскрыты всегда — и в Excel тоже: уровней быть не должно (ADR-94).

const block = {
  type: 'table',
  title: 'Продажи',
  grandTotal: true,
  columns: [
    { field: 'region', title: 'Регион', group: true, subtotal: true },
    { field: 'amount', title: 'Сумма', type: 'int', aggregate: 'sum', format: 'number' }
  ]
}

const rows = [
  { region: 'Север', amount: 10 },
  { region: 'Север', amount: 20 },
  { region: 'Юг', amount: 5 }
]

async function sheetOf(blockOverride) {
  const b = { ...block, ...blockOverride }
  const styles = resolveStyles({ styles: { preset: 'default' } }, b)
  const blob = await buildWorkbook({ block: b, rows, styles })
  const wb = new ExcelJS.Workbook()
  await wb.xlsx.load(Buffer.from(await blob.arrayBuffer()))
  return wb.worksheets[0]
}

const levels = (ws) => {
  const out = []
  for (let r = 1; r <= ws.rowCount; r++) out.push(ws.getRow(r).outlineLevel || 0)
  return out
}

describe('Excel: сворачивание групп зависит от collapsible (ADR-94)', () => {
  it('collapsible по умолчанию: строки получают outline-уровни', async () => {
    const ws = await sheetOf({})
    expect(Math.max(...levels(ws))).toBeGreaterThan(0)
    expect(ws.properties.outlineProperties).toEqual({ summaryBelow: true, summaryRight: false })
  })

  it('collapsible: true — то же самое', async () => {
    const ws = await sheetOf({ collapsible: true })
    expect(Math.max(...levels(ws))).toBeGreaterThan(0)
  })

  it('collapsible: false — структуры нет, все строки нулевого уровня', async () => {
    const ws = await sheetOf({ collapsible: false })
    expect(levels(ws).every((l) => l === 0)).toBe(true)
    expect(ws.properties.outlineProperties).toBeUndefined()
  })

  it('collapsible: false не убирает подытоги и общий итог', async () => {
    const ws = await sheetOf({ collapsible: false })
    const texts = []
    for (let r = 1; r <= ws.rowCount; r++) texts.push(String(ws.getRow(r).getCell(1).value ?? ''))
    expect(texts.some((t) => t.startsWith('Итого'))).toBe(true)
    expect(texts.some((t) => t === 'ИТОГО')).toBe(true)
  })
})

describe('Excel: несколько таблиц — каждая на своём листе (ADR-97)', () => {
  const blockA = { ...block, title: 'Продажи по регионам' }
  const blockB = {
    type: 'table',
    title: 'Сервисы',
    grandTotal: true,
    columns: [
      { field: 'service', title: 'Сервис' },
      { field: 'requests', title: 'Запросов', type: 'int', aggregate: 'sum', format: 'number' }
    ]
  }

  const stylesOf = (b) => resolveStyles({ styles: { preset: 'default' } }, b)

  async function book(sheets, params = {}, labels = {}) {
    const blob = await buildWorkbook({ sheets, params, labels, meta: { id: 'two', title: 'Две таблицы' } })
    const wb = new ExcelJS.Workbook()
    await wb.xlsx.load(Buffer.from(await blob.arrayBuffer()))
    return wb
  }

  it('две таблицы дают два листа данных и общий лист «Параметры»', async () => {
    const wb = await book(
      [
        { block: blockA, rows, styles: stylesOf(blockA) },
        { block: blockB, rows: [{ service: 'api', requests: 5 }], styles: stylesOf(blockB) }
      ],
      { from: '2026-03-01' },
      { from: 'Период с' }
    )
    expect(wb.worksheets.map((ws) => ws.name)).toEqual(['Продажи по регионам', 'Сервисы', 'Параметры'])

    // данные каждого листа — из своего набора, а не общие
    const texts = (ws) => {
      const out = []
      ws.eachRow((row) => row.eachCell((cell) => out.push(String(cell.value ?? ''))))
      return out
    }
    const salesSheet = texts(wb.getWorksheet('Продажи по регионам'))
    expect(salesSheet).toContain('Север')
    expect(salesSheet).toContain('30') // суммарная мера по группе
    const opsSheet = texts(wb.getWorksheet('Сервисы'))
    expect(opsSheet).toContain('api')
    expect(opsSheet).not.toContain('Север')
    // параметры один раз, на своём листе
    expect(wb.getWorksheet('Параметры').getRow(2).getCell(3).value).toBe('2026-03-01')
  })

  it('таблицы без title получают «Таблица N», повторы — суффикс', async () => {
    const noTitle = { ...block, title: undefined }
    const wb = await book([
      { block: noTitle, rows, styles: stylesOf(noTitle) },
      { block: { ...noTitle }, rows, styles: stylesOf(noTitle) },
      { block: { ...block, title: 'Параметры' }, rows, styles: stylesOf(block) }
    ])
    const names = wb.worksheets.map((ws) => ws.name)
    expect(names).toEqual(['Таблица 1', 'Таблица 2', 'Параметры (2)', 'Параметры'])
  })

  it('одна таблица сохраняет прежнее поведение: один лист с названием отчёта', async () => {
    const blob = await buildWorkbook({ block: { ...block, title: undefined }, rows, styles: stylesOf(block), meta: { title: 'Мой отчёт' } })
    const wb = new ExcelJS.Workbook()
    await wb.xlsx.load(Buffer.from(await blob.arrayBuffer()))
    expect(wb.worksheets.map((ws) => ws.name)).toEqual(['Мой отчёт', 'Параметры'])
  })
})

describe('Excel: правила условного форматирования (ADR-100)', () => {
  const rulesBlock = {
    type: 'table',
    title: 'Правила',
    grandTotal: true,
    rules: [
      { when: { column: 'profit', op: '<', value: 0 }, apply: { colors: { text: 'danger' } }, zones: ['detail'] },
      {
        when: { column: 'amount', op: '>=', value: 50000 },
        apply: { colors: { background: 'rowHighlight' } },
        scope: 'row',
        zones: ['detail']
      }
    ],
    columns: [
      { field: 'region', title: 'Регион' },
      { field: 'amount', title: 'Сумма', type: 'int', format: 'number' },
      { field: 'profit', title: 'Прибыль', type: 'int', format: 'number' }
    ]
  }
  const rulesRows = [
    { region: 'Север', amount: 60000, profit: -100 },
    { region: 'Юг', amount: 1000, profit: 50 }
  ]

  async function rulesSheet() {
    const styles = resolveStyles(
      { styles: { preset: 'default', palette: { danger: '#b42318', rowHighlight: '#fff7e6' } } },
      rulesBlock
    )
    const blob = await buildWorkbook({ block: rulesBlock, rows: rulesRows, styles })
    const wb = new ExcelJS.Workbook()
    await wb.xlsx.load(Buffer.from(await blob.arrayBuffer()))
    return wb.worksheets[0]
  }

  it('фон всей строки и цвет шрифта приходят из правил зоны detail', async () => {
    const ws = await rulesSheet()
    let rowIndex = 0
    ws.eachRow((row, n) => {
      if (row.getCell(1).value === 'Север') rowIndex = n
    })
    expect(rowIndex).toBeGreaterThan(1)
    const row = ws.getRow(rowIndex)
    // фон правила rowHighlight — у всех ячеек строки
    expect(row.getCell(1).fill?.fgColor?.argb).toBe('FFFFF7E6')
    expect(row.getCell(3).fill?.fgColor?.argb).toBe('FFFFF7E6')
    // цвет шрифта danger — только у ячейки целевой колонки profit
    expect(row.getCell(3).font?.color?.argb).toBe('FFB42318')
    expect(row.getCell(1).font?.color?.argb).not.toBe('FFB42318')
  })

  it('строка без сработавших правил остаётся в обычной зоне', async () => {
    const ws = await rulesSheet()
    let rowIndex = 0
    ws.eachRow((row, n) => {
      if (row.getCell(1).value === 'Юг') rowIndex = n
    })
    const cell = ws.getRow(rowIndex).getCell(1)
    expect(cell.fill?.fgColor?.argb).not.toBe('FFFFF7E6')
    expect(cell.font?.color?.argb).not.toBe('FFB42318')
  })
})

describe('Excel: плоская таблица без группировки', () => {
  it('структуры и сворачивания строк нет (дефект sales-drill)', async () => {
    const flatBlock = {
      type: 'table',
      title: 'Заказы',
      columns: [
        { field: 'region', title: 'Регион' },
        { field: 'amount', title: 'Сумма', type: 'int' }
      ]
    }
    const flatRows = [
      { region: 'Север', amount: 10 },
      { region: 'Юг', amount: 20 }
    ]
    const styles = resolveStyles({ styles: { preset: 'default' } }, flatBlock)
    const blob = await buildWorkbook({ block: flatBlock, rows: flatRows, styles })
    const wb = new ExcelJS.Workbook()
    await wb.xlsx.load(Buffer.from(await blob.arrayBuffer()))
    const ws = wb.worksheets[0]
    expect(levels(ws).every((l) => l === 0)).toBe(true)
    expect(ws.properties.outlineProperties).toBeUndefined()
  })
})
