// Ядро модели таблицы для спайка S1: группировка, агрегаты, подытоги, объединения (ADR-1).
// Важно: и данные, и mergeCells выводятся из одного массива FlatRow.

const NULL_LABEL = '(пусто)'
const SEP = '\u0001'

/** Детерминированный генератор строк (без random(), чтобы скриншоты совпадали). */
export function genRows(n) {
  const regions = ['Север', 'Юг', 'Запад', 'Восток', 'Центр']
  const categories = ['Электроника', 'Бытовая техника', 'Мебель', 'Аксессуары']
  let seed = 42
  const rnd = () => {
    seed = (seed * 1103515245 + 12345) % 2147483648
    return seed / 2147483648
  }
  const rows = new Array(n)
  for (let i = 0; i < n; i++) {
    rows[i] = {
      id: i,
      region: i % 1009 === 0 ? null : regions[Math.floor(rnd() * 5)],
      category: i % 2113 === 0 ? null : categories[Math.floor(rnd() * 4)],
      product: 'Товар-' + (1 + Math.floor(rnd() * 40)),
      amount: i % 997 === 0 ? 0 : 100 + Math.round(rnd() * 90000),
      profit: 0
    }
    rows[i].profit = Math.round(rows[i].amount * 0.25 - rnd() * 5000)
  }
  return rows
}

function label(v) {
  return v === null || v === undefined || v === '' ? NULL_LABEL : String(v)
}

function buildTree(rows, groupFields) {
  const root = { level: -1, path: [], children: new Map(), details: [] }
  for (const r of rows) {
    let node = root
    for (let d = 0; d < groupFields.length; d++) {
      const key = label(r[groupFields[d]])
      let child = node.children.get(key)
      if (!child) {
        child = {
          level: d,
          key,
          path: node.path.concat(key),
          children: new Map(),
          details: []
        }
        node.children.set(key, child)
      }
      node = child
    }
    node.details.push(r)
  }
  return root
}

function aggregate(node) {
  let amount = 0
  let profit = 0
  let count = 0
  for (const r of node.details) {
    amount += r.amount
    profit += r.profit
    count++
  }
  for (const child of node.children.values()) {
    const a = aggregate(child)
    amount += a.amount
    profit += a.profit
    count += a.count
  }
  node.agg = { amount, profit, count }
  return node.agg
}

function idOf(node) {
  return node.path.join(SEP)
}

/**
 * Строит плоскую модель.
 * @param {Array} rows сырые строки
 * @param {string[]} groupFields порядок группировки
 * @param {Set<string>} expanded ids раскрытых групп
 * @param {{subtotalLevels:number[]}} opts уровни, где печатается промежуточный итог
 */
export function buildTableModel(rows, groupFields, expanded, opts = {}) {
  const started = performance.now()
  const subtotalLevels = new Set(opts.subtotalLevels ?? [0])
  const root = buildTree(rows, groupFields)
  aggregate(root)

  const flat = []

  const pushSubtotal = (node) => {
    flat.push({
      kind: 'subtotal',
      level: node.level,
      groupId: idOf(node),
      groupPath: node.path,
      cells: { [groupFields[node.level]]: 'Итого ' + node.key },
      totals: { amount: node.agg.amount, profit: node.agg.profit }
    })
  }

  const walk = (node, parentVisible) => {
    const id = idOf(node)
    const expandedHere = expanded.has(id)
    const isLeaf = node.children.size === 0

    if (isLeaf) {
      if (parentVisible) {
        for (const r of node.details) {
          flat.push({
            kind: 'detail',
            level: groupFields.length,
            groupId: id,
            groupPath: node.path,
            cells: { ...r, __raw: r },
            totals: { amount: r.amount, profit: r.profit }
          })
        }
      }
      if (subtotalLevels.has(node.level) && parentVisible) pushSubtotal(node)
      return
    }

    if (!expandedHere) {
      // Свёрнуто: дети скрыты, вместо них — строка итога группы.
      if (parentVisible) pushSubtotal(node)
      return
    }

    for (const child of node.children.values()) walk(child, parentVisible)
    if (subtotalLevels.has(node.level) && parentVisible) pushSubtotal(node)
  }

  for (const child of root.children.values()) walk(child, true)

  // Общий итог — последняя строка массива, footerMethod не используется (ADR-2).
  flat.push({
    kind: 'grandtotal',
    level: -1,
    groupId: 'grand',
    groupPath: [],
    cells: { [groupFields[0]]: 'ИТОГО' },
    totals: { amount: root.agg.amount, profit: root.agg.profit }
  })

  const merges = computeMerges(flat, groupFields)
  return {
    rows: flat,
    merges,
    buildMs: performance.now() - started,
    total: root.agg,
    detailRows: rows.length
  }
}

/**
 * Объединения выводятся из видимых строк: для каждой групповой колонки берём
 * непрерывные серии с одинаковым префиксом groupPath. Индексы валидны ровно для
 * того массива, который отдан в таблицу, поэтому toggle не может их рассинхронизировать.
 */
export function computeMerges(flat, groupFields) {
  const merges = []
  const n = flat.length
  for (let col = 0; col < groupFields.length; col++) {
    let start = 0
    while (start < n) {
      const row = flat[start]
      // Только детальные строки: строка подытога должна остаться со своей подписью.
      if (row.kind !== 'detail') {
        start++
        continue
      }
      const prefix = row.groupPath.slice(0, col + 1).join(SEP)
      let end = start + 1
      while (end < n) {
        const next = flat[end]
        if (next.kind !== 'detail') break
        if (next.groupPath.slice(0, col + 1).join(SEP) !== prefix) break
        end++
      }
      if (end - start > 1) {
        merges.push({ row: start, col, rowspan: end - start, colspan: 1 })
      }
      start = end
    }
  }
  return merges
}
