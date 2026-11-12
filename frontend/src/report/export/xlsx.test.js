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
