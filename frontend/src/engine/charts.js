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

// Полоса (band, ADR-58, ADR-69): у точки значение и одна или несколько пар границ.
// Возвращает категории и значение плюс по каждому коридору ряды {lows, highs, band}:
// band — разница «верх − низ», ею и закрашивается диапазон.
export function buildBandModel(rows, { xField, yField, lowField, highField, bands } = {}) {
  const pairs = Array.isArray(bands) && bands.length
    ? bands.map((b) => ({ lowField: b?.lowField, highField: b?.highField }))
    : [{ lowField, highField }]

  const categories = []
  const values = []
  const corpus = pairs.map(() => ({ lows: [], highs: [] }))
  const seen = new Set()

  for (const row of rows || []) {
    const x = row?.[xField]
    if (isNullKey(x)) continue
    const key = String(x)
    if (seen.has(key)) continue // категория один раз: иначе линия пойдёт назад
    seen.add(key)

    categories.push(key)
    values.push(num(row?.[yField]))
    pairs.forEach((pair, i) => {
      const low = num(row?.[pair.lowField])
      const high = num(row?.[pair.highField])
      // границы могут отсутствовать: тогда точка есть, а полосы под ней нет.
      // Перепутанные границы вырождают полосу в ноль, а не ломают график.
      corpus[i].lows.push(low)
      corpus[i].highs.push(high !== null && low !== null && high < low ? low : high)
    })
  }

  const built = corpus.map(({ lows, highs }) => ({
    lows,
    highs,
    band: highs.map((h, i) => (h === null || lows[i] === null ? null : Number((h - lows[i]).toFixed(6))))
  }))

  // Первый коридор дублируется в плоских полях — прежние шаблоны и тесты читают их
  const first = built[0] || { lows: [], highs: [], band: [] }
  return { categories, values, corridors: built, lows: first.lows, highs: first.highs, band: first.band }
}

// Потоки для sankey (ADR-56, ADR-68): пара «источник → приёмник» либо цепочка уровней
// levels: [поле1, поле2, поле3] — тогда связи строятся между соседними уровнями.
// Узлы разных уровней нумеруются префиксом (`0\u0001Восток`), иначе одинаковые названия
// на разных уровнях склеились бы в один узел и путь считался бы неверно; подпись в
// диаграмме берётся из display.
export function buildSankeyModel(rows, { sourceField, targetField, valueField, levels, aggregate = 'sum' } = {}) {
  const chain = Array.isArray(levels) && levels.length >= 2 ? levels : null
  const order = []
  const seen = new Set()
  const display = new Map() // id узла → подпись для глаза
  const flows = new Map() // "источник\u0001приёмник" → {sum,count,min,max}
  let dropped = 0

  const nodeId = (level, value) => (chain ? `${level}\u0001${value}` : String(value))
  // level и raw нужны drill-переходу (ADR-72): из узла видно, какому уровню он
  // принадлежит и какое значение стоит за служебным id
  const addNode = (level, value) => {
    const id = nodeId(level, value)
    if (!seen.has(id)) {
      seen.add(id)
      order.push({ name: id, level, raw: String(value), levels: chain ? { [chain[level]]: String(value) } : {} })
      if (chain) display.set(id, String(value))
    }
    return id
  }

  for (const row of rows || []) {
    const weight = num(row?.[valueField])
    if (weight === null) {
      dropped++
      continue
    }
    if (chain) {
      const path = chain.map((field) => row?.[field])
      if (path.some((v) => isNullKey(v))) {
        dropped++
        continue
      }
      for (let i = 0; i < chain.length - 1; i++) {
        const from = addNode(i, path[i])
        const to = addNode(i + 1, path[i + 1])
        const key = `${from}\u0001${to}`
        let cell = flows.get(key)
        if (!cell) {
          // у связи уровней известны только её концы: регион на связи
          // «категория → товар» не определить, и подставлять его нельзя
          cell = {
            source: from,
            target: to,
            sourceLevel: i,
            targetLevel: i + 1,
            levels: { [chain[i]]: String(path[i]), [chain[i + 1]]: String(path[i + 1]) },
            sum: 0,
            count: 0,
            min: Infinity,
            max: -Infinity
          }
          flows.set(key, cell)
        }
        cell.sum += weight
        cell.count++
        if (weight < cell.min) cell.min = weight
        if (weight > cell.max) cell.max = weight
      }
      continue
    }

    const from = row?.[sourceField]
    const to = row?.[targetField]
    if (isNullKey(from) || isNullKey(to)) {
      dropped++
      continue
    }
    const source = addNode(0, from)
    const target = addNode(0, to)
    const key = `${source}\u0001${target}`
    let cell = flows.get(key)
    if (!cell) {
      cell = { source, target, sum: 0, count: 0, min: Infinity, max: -Infinity }
      flows.set(key, cell)
    }
    cell.sum += weight
    cell.count++
    if (weight < cell.min) cell.min = weight
    if (weight > cell.max) cell.max = weight
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
    links.push({
      source: cell.source,
      target: cell.target,
      value,
      ...(cell.levels ? { levels: cell.levels, sourceLevel: cell.sourceLevel, targetLevel: cell.targetLevel } : {})
    })
  }

  // подпись узла: у цепочки это исходное значение, у пары — сам узел
  const label = (name) => (display.has(name) ? display.get(name) : name)
  return { nodes: order, links, dropped, display, label, levels: chain }
}
