// Ядро табличной модели отчёта (Ф2a): дерево группировок, агрегаты по областям,
// строки подытогов и общий итог, вычисляемые колонки. Один источник истины (ADR-1):
// всё, включая объединения, выводится из массива строк.
import { evaluate } from './expr.js'

const FUNCS = new Set(['sum', 'avg', 'min', 'max', 'count', 'countDistinct'])

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
        values[i] = acc
          ? evalExpression(col.expression, rowScope, acc)
          : evalExpression(col.expression, rowScope, rowAccumulator(col.expression, rowScope))
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
    // регион и другие предки остаются в своих колонках, иначе строка «висит» без группы
    for (let i = 0; i < level; i++) row.values[i] = node.path[i]
    row.values[labelIndex >= 0 ? labelIndex : 0] = `Итого ${node.key}`
    flat.push(row)
  }

  const walk = (node, visible) => {
    const isLeaf = node.children.size === 0
    const childrenVisible = visible && isOpen(node.path)

    // Детали, подгруженные по требованию (ADR-65): у серверной агрегации в модели одна
    // строка на группу, раскрывать нечего. Загруженные строки показываем как детальные,
    // а итог группы берём из серверной строки — он посчитан по всем строкам, а не по
    // загруженной части (она может быть обрезана лимитом).
    const lazy = isLeaf && opts.details ? opts.details.get(node.path.join(SEP)) : null
    if (lazy && lazy.rows?.length) {
      const open = isOpen(node.path)
      if (visible && open) {
        for (const r of lazy.rows) flat.push(makeRow('detail', groupFields.length, node, null, r, null))
        const serverRow = node.details[0] || {}
        const labelIndex = columns.findIndex((c) => c.group && c.field === groupFields[node.level])
        const row = makeRow('subtotal', node.level, node, null, serverRow, -1)
        for (let i = 0; i < node.level; i++) row.values[i] = node.path[i]
        row.values[labelIndex >= 0 ? labelIndex : 0] = `Итого ${node.key}`
        flat.push(row)
      } else if (visible) {
        pushSubtotal(node)
      }
      return
    }

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
/**
 * Значение выражения в контексте одной строки (плоская таблица без группировки).
 * Агрегат по одной строке равен её значению; null-значение даёт null (как в группе).
 */
export function rowAccumulator(expression, row) {
  const acc = {}
  for (const m of String(expression).matchAll(/[a-zA-Z_][a-zA-Z0-9_]*/g)) {
    const name = m[0]
    if (FUNCS.has(name) || acc[name]) continue
    const raw = row?.[name]
    const v = Number(raw)
    acc[name] =
      raw === null || raw === undefined || raw === '' || !Number.isFinite(v)
        ? { sum: 0, count: 0, min: null, max: null, distinct: 0 }
        : { sum: v, count: 1, min: v, max: v, distinct: 1 }
  }
  return acc
}

export function evaluateRowExpression(expression, row) {
  return evalExpression(expression, row, rowAccumulator(expression, row))
}

export function computeMerges(flat, groupCount) {
  const merges = []
  // строка итога уровня L участвует в объединениях колонок выше L (её предки),
  // но не в своей колонке — иначе подпись «Итого …» спрячется под объединением
  const member = (row, col) =>
    row.kind === 'detail' || (row.kind === 'subtotal' && col < row.groupPath.length - 1)

  for (let col = 0; col < groupCount; col++) {
    let start = 0
    while (start < flat.length) {
      if (!member(flat[start], col)) {
        start++
        continue
      }
      const prefix = flat[start].groupPath.slice(0, col + 1).join(SEP)
      let end = start + 1
      while (end < flat.length) {
        const next = flat[end]
        if (!member(next, col)) break
        if (next.groupPath.slice(0, col + 1).join(SEP) !== prefix) break
        end++
      }
      if (end - start > 1) merges.push({ row: start, col, rowspan: end - start, colspan: 1 })
      start = end
    }
  }
  return merges
}

/**
 * Данные для диаграммы по тем же правилам агрегации, что и таблица (Ф3.2):
 * категория = xField, необязательная серия = seriesField, значение = aggregate(yField).
 * Один проход по строкам, без повторных обходов на каждую серию.
 * @returns {{categories: string[], series: Array<{name: string, data: Array<number|null>}>}}
 */
export function aggregateSeries(rows, { xField, seriesField, yField, aggregate = 'sum' }) {
  const norm = (v) => (v === null || v === undefined || v === '' ? '(пусто)' : String(v))
  const categories = []
  const catIndex = new Map()
  const bySeries = new Map() // имя серии → массив ячеек {sum,count,min,max}

  for (const row of rows) {
    const x = norm(row[xField])
    let ci = catIndex.get(x)
    if (ci === undefined) {
      ci = categories.length
      catIndex.set(x, ci)
      categories.push(x)
      for (const arr of bySeries.values()) arr.push(null)
    }

    const name = seriesField ? norm(row[seriesField]) : ''
    let arr = bySeries.get(name)
    if (!arr) {
      arr = new Array(categories.length).fill(null)
      bySeries.set(name, arr)
    }

    const v = Number(row[yField])
    if (!Number.isFinite(v)) continue
    const cell = arr[ci] ?? { sum: 0, count: 0, min: Infinity, max: -Infinity }
    cell.sum += v
    cell.count++
    if (v < cell.min) cell.min = v
    if (v > cell.max) cell.max = v
    arr[ci] = cell
  }

  const pick = (c) => {
    if (c === null) return null
    switch (aggregate) {
      case 'avg':
        return c.sum / c.count
      case 'count':
        return c.count
      case 'min':
        return c.min
      case 'max':
        return c.max
      default:
        return c.sum
    }
  }

  return {
    categories,
    series: [...bySeries.entries()].map(([name, arr]) => ({
      name: name || yField || 'value',
      data: arr.map(pick)
    }))
  }
}

export default buildTableModel
