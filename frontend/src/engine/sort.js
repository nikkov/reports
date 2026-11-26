// Сортировка строк таблицы по правилам шаблона (ADR-183).
// Правила задаются в блоке: "sort": [{ "field": "amount", "dir": "desc" }, ...].
// Сортировка устойчивая: одинаковые значения сохраняют исходный порядок строк.
import { evaluateRowExpression } from './group.js'

function collectLeaves(columns, out = []) {
  for (const col of columns || []) {
    if (Array.isArray(col.children) && col.children.length) {
      collectLeaves(col.children, out)
    } else {
      out.push(col)
    }
  }
  return out
}

const collator = new Intl.Collator('ru', { numeric: true, sensitivity: 'base' })

// Пустые значения всегда в конце (и при asc, и при desc): «нет данных» не должно
// вытеснять реальные значения наверх. Числа сравниваются как числа, строки — по-русски,
// поэтому «Товар-9» не встаёт после «Товар-10».
function compareValues(a, b) {
  const aEmpty = a === null || a === undefined || a === ''
  const bEmpty = b === null || b === undefined || b === ''
  if (aEmpty && bEmpty) return 0
  if (aEmpty) return 1
  if (bEmpty) return -1
  const na = typeof a === 'number' ? a : Number(a)
  const nb = typeof b === 'number' ? b : Number(b)
  const bothNumeric = Number.isFinite(na) && Number.isFinite(nb) && String(a).trim() !== '' &&
    String(b).trim() !== ''
  if (bothNumeric) return na < nb ? -1 : na > nb ? 1 : 0
  return collator.compare(String(a), String(b))
}

/**
 * @param {Array<object>} rows строки набора
 * @param {Array<object>} columns листья колонок (или дерево — развернём)
 * @param {Array<{field: string, dir?: 'asc'|'desc'}>} rules правила сортировки
 * @returns {Array<object>} новый массив строк (исходный не меняется)
 */
export function sortRows(rows, columns, rules) {
  if (!Array.isArray(rules) || rules.length === 0 || !Array.isArray(rows) || rows.length < 2) {
    return rows
  }
  const leaves = collectLeaves(columns)
  const byField = new Map()
  const byTitle = new Map()
  for (const col of leaves) {
    if (col.field) byField.set(col.field, col)
    else if (col.expression && col.title) byTitle.set(col.title, col)
  }

  const prepared = []
  for (const rule of rules) {
    const col = byField.get(rule.field) || byTitle.get(rule.field)
    if (!col) continue
    const key = col.field
      ? (row) => row[col.field]
      : (row) => {
          try {
            return evaluateRowExpression(col.expression, row)
          } catch {
            return null
          }
        }
    prepared.push({ key, dir: rule.dir === 'desc' ? -1 : 1 })
  }
  if (!prepared.length) return rows

  const decorated = rows.map((row, index) => ({
    row,
    index,
    keys: prepared.map((p) => p.key(row))
  }))
  decorated.sort((a, b) => {
    for (let k = 0; k < prepared.length; k++) {
      const cmp = compareValues(a.keys[k], b.keys[k])
      // пустые значения держим в конце при любом направлении
      if (cmp === 0) continue
      const isEmpty = cmp === 1 ? isBlank(a.keys[k]) : isBlank(b.keys[k])
      if (isEmpty) return cmp
      return cmp * prepared[k].dir
    }
    return a.index - b.index
  })
  return decorated.map((x) => x.row)
}

function isBlank(v) {
  return v === null || v === undefined || v === ''
}

// Какие поля заданы в правилах — нужно валидатору и для подсветки заголовков
export function sortFields(rules) {
  if (!Array.isArray(rules)) return []
  return rules.filter((r) => r && typeof r.field === 'string').map((r) => r.field)
}
