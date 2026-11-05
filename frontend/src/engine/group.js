// Ядро табличной модели отчёта (Ф2a): дерево группировок, агрегаты по областям,
// строки подытогов и общий итог, вычисляемые колонки. Один источник истины (ADR-1):
// всё, включая объединения, выводится из массива строк.
import { evaluate } from './expr.js'

const SEP = '\u0001'

function isNullKey(v) {
  return v === null || v === undefined || v === ''
}

function buildTree(rows, groupFields, nullLabel) {
  const root = { level: -1, path: [], children: new Map(), details: [] }
  for (const row of rows) {
    let node = root
    for (let d = 0; d < groupFields.length; d++) {
      const raw = row[groupFields[d]]
      const key = isNullKey(raw) ? nullLabel : String(raw)
      let child = node.children.get(key)
      if (!child) {
        child = { level: d, key, path: node.path.concat(key), children: new Map(), details: [] }
        node.children.set(key, child)
      }
      node = child
    }
    node.details.push(row)
  }
  return root
}

// Поля, по которым нужны агрегаты: колонки с aggregate и всё, что стоит внутри sum()/avg()/... в выражениях.
function aggregateFields(columns) {
  const fields = new Set()
  for (const col of columns) {
    if (col.field && col.aggregate) fields.add(col.field)
    if (col.expression) {
      for (const m of col.expression.matchAll(/(?:sum|avg|min|max|count|countDistinct)\s*\(\s*([a-zA-Z_][a-zA-Z0-9_]*)/g)) {
        fields.add(m[1])
      }
    }
  }
  return fields
}

function aggregateNode(node, fields) {
  const acc = {}
  for (const f of fields) acc[f] = { sum: 0, count: 0, min: Infinity, max: -Infinity, seen: new Set() }

  const visit = (n) => {
    for (const row of n.details) {
      for (const f of fields) {
        const v = Number(row[f])
        const a = acc[f]
        if (!Number.isFinite(v)) continue
        a.sum += v
        a.count++
        if (v < a.min) a.min = v
        if (v > a.max) a.max = v
        a.seen.add(v)
      }
    }
    for (const child of n.children.values()) visit(child)
  }
  visit(node)

  for (const f of fields) acc[f].distinct = acc[f].seen.size
  return acc
}

function aggValue(acc, fn, field) {
  const a = acc[field]
  if (!a || a.count === 0) return fn === 'count' || fn === 'countDistinct' ? 0 : null
  switch (fn) {
    case 'sum':
      return a.sum
    case 'avg':
      return a.sum / a.count
    case 'min':
      return a.min
    case 'max':
      return a.max
    case 'count':
      return a.count
    case 'countDistinct':
      return a.distinct
    default:
      return null
  }
}

function evalExpression(expression, rowScope, acc) {
  try {
    return evaluate(expression, {
      field: (name) => (rowScope ? rowScope[name] : undefined),
      agg: (fn, name) => (acc ? aggValue(acc, fn, name) : undefined)
    })
  } catch {
    return null
  }
}

/**
 * @param {Array<Object>} rows       сырые строки из API
 * @param {Array<Object>} columns    колонки шаблона (group/subtotal/aggregate/expression/format)
 * @param {{expanded?: Set<string>, grandTotal?: boolean, nullLabel?: string}} opts
 */
export function buildTableModel(rows, columns, opts = {}) {
  const started = performance.now()
  const nullLabel = opts.nullLabel || '(пусто)'
  const collapsed = opts.collapsed
  const grandTotal = opts.grandTotal !== false

  const groupCols = columns.filter((c) => c.group)
  const groupFields = groupCols.map((c) => c.field)
  const subtotalLevels = new Set(
    groupCols.map((c, i) => (c.subtotal ? i : -1)).filter((i) => i >= 0)
  )
  const fields = aggregateFields(columns)
  const aggCols = columns.filter((c) => c.aggregate && c.field)
  const exprCols = columns.filter((c) => c.expression)

  const root = buildTree(rows, groupFields, nullLabel)
  const rootAcc = aggregateNode(root, fields)

  const flat = []

  // Группа раскрыта, если её нет в множестве свёрнутых.
  const isOpen = (path) => !collapsed || !collapsed.has(path.join(SEP))

  const makeRow = (kind, level, node, acc, rowScope, labelIndex) => {
    const values = new Array(columns.length)
    columns.forEach((col, i) => {
      if (col.group) {
        // детальная строка несёт своё значение (объединение покажет его на первой строке блока);
        // в строках итогов — только подпись в колонке своей группы
        values[i] = rowScope ? rowScope[col.field] : labelIndex === i ? `Итого ${node.key}` : ''
      } else if (col.expression) {
        values[i] = evalExpression(col.expression, rowScope, acc)
      } else if (rowScope) {
        values[i] = rowScope[col.field]
      } else if (col.aggregate) {
        values[i] = aggValue(acc, col.aggregate, col.field)
      } else {
        values[i] = null
      }
    })
    return {
      kind,
      level,
      groupId: node ? node.path.join(SEP) : 'grand',
      groupPath: node ? node.path : [],
      values,
      totals: acc
    }
  }

  const pushSubtotal = (node) => {
    const acc = aggregateNode(node, fields)
    const level = node.level
    const labelIndex = columns.findIndex((c) => c.group && c.field === groupFields[level])
    const row = makeRow('subtotal', level, node, acc, null, -1)
    row.values[labelIndex >= 0 ? labelIndex : 0] = `Итого ${node.key}`
    flat.push(row)
  }

  const walk = (node, visible) => {
    const isLeaf = node.children.size === 0
    const childrenVisible = visible && isOpen(node.path)

    if (isLeaf) {
      const open = isOpen(node.path)
      if (visible && open) {
        for (const r of node.details) flat.push(makeRow('detail', groupFields.length, node, null, r, null))
      }
      // свёрнутый лист показывает только свой итог — независимо от subtotal:true
      if (visible && (!open || subtotalLevels.has(node.level))) pushSubtotal(node)
      return
    }

    if (!childrenVisible) {
      // свёрнутая группа: показываем только её итог
      if (visible) pushSubtotal(node)
      return
    }

    for (const child of node.children.values()) walk(child, visible)
    if (subtotalLevels.has(node.level)) pushSubtotal(node)
  }

  for (const child of root.children.values()) walk(child, true)

  if (grandTotal) {
    const row = makeRow('grandtotal', -1, { key: '', path: [] }, rootAcc, null, -1)
    row.values[0] = groupCols.length ? 'ИТОГО' : row.values[0]
    flat.push(row)
  }

  const merges = computeMerges(flat, groupFields.length)

  return {
    rows: flat,
    merges,
    total: rootAcc,
    groupFields,
    buildMs: performance.now() - started,
    sourceRows: rows.length
  }
}

/**
 * Вертикальные объединения групповых колонок: непрерывные серии одинакового префикса
 * среди детальных строк. Строки подытогов и итога исключены — иначе подпись «Итого …»
 * исчезает под объединённой ячейкой.
 */
export function computeMerges(flat, groupCount) {
  const merges = []
  for (let col = 0; col < groupCount; col++) {
    let start = 0
    while (start < flat.length) {
      if (flat[start].kind !== 'detail') {
        start++
        continue
      }
      const prefix = flat[start].groupPath.slice(0, col + 1).join(SEP)
      let end = start + 1
      while (end < flat.length) {
        const next = flat[end]
        if (next.kind !== 'detail') break
        if (next.groupPath.slice(0, col + 1).join(SEP) !== prefix) break
        end++
      }
      if (end - start > 1) merges.push({ row: start, col, rowspan: end - start, colspan: 1 })
      start = end
    }
  }
  return merges
}

export default buildTableModel
