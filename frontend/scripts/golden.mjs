#!/usr/bin/env node
// Golden-сверка (2a.7): агрегаты движка против эталонного GROUP BY в PostgreSQL.
// Сравниваются общий итог и подытог по каждому региону, а также число строк.
import { execFileSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'
import { dirname, resolve } from 'node:path'
import { buildTableModel } from '../src/engine/group.js'

const here = dirname(fileURLToPath(import.meta.url))
const API = process.env.REP_API || 'http://127.0.0.1:8080'
const REPORT = process.env.REP_REPORT || 'sales-summary'
const FROM = process.env.REP_FROM || '2026-01-01'
const TO = process.env.REP_TO || '2026-03-31'

const psql = (sql) =>
  execFileSync('psql', ['-d', process.env.PGDATABASE || 'reporting', '-tA', '-F', '|', '-c', sql], {
    encoding: 'utf8'
  })
    .trim()
    .split('\n')
    .filter(Boolean)

async function main() {
  console.log(`отчёт ${REPORT}, период ${FROM}..${TO}`)

  const template = await (await fetch(`${API}/api/reports/${REPORT}`)).json()
  const data = await (
    await fetch(`${API}/api/report/data`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ reportId: REPORT, params: { from: FROM, to: TO } })
    })
  ).json()

  const block = template.blocks.find((b) => b.type === 'table')

  // листья: заголовки-группы (children) разворачиваются, данные живут только в листьях
  const leaves = []
  const walk = (col, path) => {
    if (Array.isArray(col.children)) {
      for (const child of col.children) walk(child, path.concat(col.title))
      return
    }
    leaves.push(col)
  }
  for (const col of block.columns) walk(col, [])

  const model = buildTableModel(data.rows, leaves, { grandTotal: true })

  // эталон: SUM по тем же полям, что объявлены в колонках
  const aggCols = leaves
    .map((c, i) => ({ ...c, index: i }))
    .filter((c) => c.aggregate === 'sum' && c.field)
  const sumExpr = aggCols.map((c) => `sum(${c.field})::bigint`).join(', ')

  const [grandRow] = psql(
    `SELECT ${sumExpr}, count(*) FROM sales WHERE order_date BETWEEN '${FROM}' AND '${TO}'`
  )
  const grandSql = grandRow.split('|').map(Number)
  const grandModelRow = model.rows.at(-1)

  const regionSql = psql(
    `SELECT coalesce(region, '(пусто)'), ${sumExpr} FROM sales
      WHERE order_date BETWEEN '${FROM}' AND '${TO}' GROUP BY 1 ORDER BY 1`
  )

  const checks = []
  const push = (name, expected, actual) => {
    const ok = Number(expected) === Number(actual)
    checks.push({ name, expected, actual, ok })
  }

  aggCols.forEach((c, k) => push(`ИТОГО.${c.field}`, grandSql[k], grandModelRow.values[c.index]))
  push('ИТОГО.строк', grandSql[grandSql.length - 1], data.rowCount)

  const modelSubtotals = new Map(
    model.rows.filter((r) => r.kind === 'subtotal' && r.groupPath.length === 1).map((r) => [r.groupPath[0], r])
  )

  for (const line of regionSql) {
    const [region, ...vals] = line.split('|')
    const row = modelSubtotals.get(region)
    if (!row) {
      checks.push({ name: `подытог «${region}»`, expected: 'есть', actual: 'нет', ok: false })
      continue
    }
    aggCols.forEach((c, k) => push(`${region}.${c.field}`, vals[k], row.values[c.index]))
  }

  const failed = checks.filter((c) => !c.ok)
  for (const c of checks) {
    const mark = c.ok ? '✓' : '✗'
    console.log(` ${mark} ${c.name}: модель ${c.actual}, SQL ${c.expected}`)
  }
  console.log(`\nпроверок: ${checks.length}, расхождений: ${failed.length}`)
  if (failed.length) process.exit(1)
}

main().catch((e) => {
  console.error('golden упал:', e.message)
  process.exit(2)
})
