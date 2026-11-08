// Модели диаграмм-одиночек (ADR-56): gauge показывает одно число, sankey — потоки
// между узлами. Вынесено из компонента, чтобы проверять расчёт тестами без DOM.
const isNullKey = (v) => v === null || v === undefined || v === ''
const num = (v) => {
  if (isNullKey(v)) return null       // Number(null) === 0, для расчёта это пропуск, а не ноль
  const n = Number(v)
  return Number.isFinite(n) ? n : null
}

// Аккуратная верхняя граница для шкалы: 1.25 от значения, округлённое вверх до «круглого» шага.
function niceMax(value, min) {
  if (!Number.isFinite(value) || value <= min) return min + 1
  const raw = (value - min) * 1.25
  const step = Math.pow(10, Math.floor(Math.log10(raw)))
  return min + Math.ceil(raw / step) * step
}

// Значение для gauge: агрегат по всей выборке (без группировки по категориям).
export function buildGaugeModel(rows, { yField, aggregate = 'sum', min, max, unit = '', name = '' } = {}) {
  const values = []
  for (const row of rows || []) {
    const v = num(row?.[yField])
    if (v !== null) values.push(v)
  }

  let value = 0
  switch (aggregate) {
    case 'avg':
      value = values.length ? values.reduce((a, b) => a + b, 0) / values.length : 0
      break
    case 'min':
      value = values.length ? Math.min(...values) : 0
      break
    case 'max':
      value = values.length ? Math.max(...values) : 0
      break
    case 'count':
      value = values.length
      break
    case 'countDistinct':
      value = new Set(values).size
      break
    case 'sum':
    default:
      value = values.reduce((a, b) => a + b, 0)
  }

  const lo = num(min) ?? 0
  const hi = num(max) ?? niceMax(value, lo)
  return { value, min: lo, max: hi > lo ? hi : lo + 1, unit, name, rows: values.length }
}

// Потоки для sankey: пары «источник → приёмник» со агрегированным весом.
export function buildSankeyModel(rows, { sourceField, targetField, valueField, aggregate = 'sum' } = {}) {
  const order = []
  const seen = new Set()
  const flows = new Map() // "источник\u0001приёмник" → {sum,count,min,max}
  let dropped = 0

  for (const row of rows || []) {
    const from = row?.[sourceField]
    const to = row?.[targetField]
    const weight = num(row?.[valueField])
    if (isNullKey(from) || isNullKey(to) || weight === null) {
      dropped++
      continue
    }
    const key = `${from}\u0001${to}`
    let cell = flows.get(key)
    if (!cell) {
      cell = { source: String(from), target: String(to), sum: 0, count: 0, min: Infinity, max: -Infinity }
      flows.set(key, cell)
    }
    cell.sum += weight
    cell.count++
    if (weight < cell.min) cell.min = weight
    if (weight > cell.max) cell.max = weight

    for (const node of [String(from), String(to)]) {
      if (!seen.has(node)) {
        seen.add(node)
        order.push({ name: node })
      }
    }
  }

  const pick = (cell) => {
    switch (aggregate) {
      case 'avg':
        return cell.sum / cell.count
      case 'min':
        return cell.min
      case 'max':
        return cell.max
      case 'count':
        return cell.count
      case 'sum':
      default:
        return cell.sum
    }
  }

  const links = []
  for (const cell of flows.values()) {
    const value = pick(cell)
    if (!Number.isFinite(value) || value <= 0) continue // ECharts не рисует нулевые и отрицательные потоки
    links.push({ source: cell.source, target: cell.target, value })
  }

  return { nodes: order, links, dropped }
}
