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

// Цветовые зоны (ADR-57): N границ делят шкалу на N+1 отрезок, цветов столько же.
// Цвет может быть hex или именем из styles.palette — разрешение делает вызывающий.
export function buildGaugeZones({ min, max, thresholds, zoneColors, palette = {} }) {
  if (!Array.isArray(thresholds) || thresholds.length < 2) return { zones: [], color: null }
  if (!Array.isArray(zoneColors) || zoneColors.length !== thresholds.length + 1) return { zones: [], color: null }
  const lo = Number.isFinite(min) ? min : 0
  const hi = Number.isFinite(max) && max > lo ? max : lo + 1
  const resolve = (c) => (palette && palette[c]) || c

  const edges = [lo, ...thresholds.map(Number), hi]
  const zones = []
  for (let i = 0; i < edges.length - 1; i++) {
    const from = Math.max(lo, edges[i])
    const to = Math.min(hi, edges[i + 1])
    if (!(to > from)) continue
    zones.push({ from, to, color: resolve(zoneColors[i]) })
  }
  return { zones, paletteHits: zoneColors.map(resolve) }
}

// Цвет текущего значения: та зона, в которую попало число.
export function gaugeColorFor(value, zones) {
  for (const zone of zones || []) {
    if (value >= zone.from && value < zone.to) return zone.color
  }
  if (!zones || !zones.length) return null
  return zones[zones.length - 1].color // значение на самой правой границе или выше
}

// Значение для gauge: агрегат по всей выборке (без группировки по категориям).
export function buildGaugeModel(
  rows,
  { yField, aggregate = 'sum', min, max, unit = '', name = '', thresholds, zoneColors, palette } = {}
) {
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
  const model = { value, min: lo, max: hi > lo ? hi : lo + 1, unit, name, rows: values.length }
  const zoneInfo = buildGaugeZones({ min: model.min, max: model.max, thresholds, zoneColors, palette })
  model.zones = zoneInfo.zones || []
  model.color = gaugeColorFor(value, model.zones)
  return model
}

// Полоса (band, ADR-58): у каждой точки три числа — значение и две границы.
// Возвращает ряды для ECharts: значение отдельно, границы — для закрашенного диапазона.
export function buildBandModel(rows, { xField, yField, lowField, highField } = {}) {
  const categories = []
  const values = []
  const lows = []
  const highs = []
  const seen = new Set()

  for (const row of rows || []) {
    const x = row?.[xField]
    if (isNullKey(x)) continue
    const key = String(x)
    if (seen.has(key)) continue // категория один раз: иначе линия пойдёт назад
    seen.add(key)

    const value = num(row?.[yField])
    const low = num(row?.[lowField])
    const high = num(row?.[highField])
    // границы могут отсутствовать: тогда точка есть, а полосы под ней нет
    categories.push(key)
    values.push(value)
    lows.push(low)
    highs.push(high !== null && low !== null && high < low ? low : high) // перепутанные границы не ломают полосу
  }

  const band = highs.map((h, i) => (h === null || lows[i] === null ? null : Number((h - lows[i]).toFixed(6))))
  return { categories, values, lows, highs, band }
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
