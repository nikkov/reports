// Стили таблицы из шаблона (ADR-23, версия 1.1).
// Порядок применения: умолчания → preset → styles → styles.<зона> → блок → колонка.
// Всё, что здесь считается, потом уходит в CSS-переменные рендерера: inline-стилей на ячейках нет.

export const FONT_FAMILIES = [
  'system-ui',
  'Inter',
  'Roboto',
  'Arial',
  'Georgia',
  'Times New Roman',
  'Courier New',
  'monospace'
]

export const ZONES = ['header', 'body', 'subtotal', 'grandtotal']

// Вид версии 1.0 — без секции styles шаблон должен рендериться ровно так же.
export const DEFAULTS = {
  font: { family: 'system-ui', size: 13, weight: 400, style: 'normal', lineHeight: 1.4 },
  colors: { text: '#1c1f23', background: '#ffffff', border: '#dfe3e8' },
  layout: {
    rowHeight: 26,
    headerHeight: 26,
    subtotalHeight: 26,
    grandtotalHeight: 26,
    cellPadding: '2px 8px',
    borders: 'grid',
    zebra: false,
    stripeColor: '#f7f9fc',
    tableLayout: 'auto'
  },
  header: {
    font: { weight: 600 },
    colors: { text: '#1c1f23', background: '#f2f4f7' },
    align: 'left',
    verticalAlign: 'middle'
  },
  body: { align: 'left', verticalAlign: 'top', padding: '3px 8px' },
  subtotal: {
    font: { weight: 600 },
    colors: { text: '#1c1f23', background: '#f7f9fc' },
    align: 'left',
    verticalAlign: 'top'
  },
  grandtotal: {
    font: { weight: 600 },
    colors: { text: '#1c1f23', background: '#f7f9fc' },
    align: 'left',
    verticalAlign: 'top'
  }
}

export const PRESETS = {
  default: {},
  compact: {
    font: { size: 12 },
    layout: { rowHeight: 22, headerHeight: 24, subtotalHeight: 22, grandtotalHeight: 24, cellPadding: '1px 6px' }
  },
  print: {
    font: { size: 12, family: 'Times New Roman' },
    colors: { border: '#333333' },
    layout: {
      rowHeight: 24,
      headerHeight: 30,
      subtotalHeight: 26,
      grandtotalHeight: 28,
      cellPadding: '3px 6px',
      borders: 'grid',
      zebra: false,
      tableLayout: 'fixed'
    },
    header: { colors: { background: '#ffffff', text: '#000000' }, font: { weight: 700 } },
    grandtotal: { colors: { background: '#f0f0f0', text: '#000000' } }
  },
  dark: {
    colors: { text: '#e6e9ef', background: '#161a21', border: '#2c3440' },
    layout: { stripeColor: '#1c222b' },
    header: { colors: { background: '#1f2630', text: '#f2f5fa' } },
    subtotal: { colors: { background: '#1b212a', text: '#eef2f8' } },
    grandtotal: { colors: { background: '#242c38', text: '#ffffff' } }
  }
}

const isPlainObject = (v) => v !== null && typeof v === 'object' && !Array.isArray(v)

function merge(base, patch) {
  if (!isPlainObject(patch)) return base
  const out = { ...base }
  for (const [key, value] of Object.entries(patch)) {
    if (value === undefined) continue
    out[key] = isPlainObject(value) && isPlainObject(base?.[key]) ? merge(base[key], value) : value
  }
  return out
}

function zoneStyle(zone, partial) {
  // зона = шрифт/цвета/выравнивание; layout-ключи сюда не примешиваются
  return {
    font: merge(DEFAULTS.font, partial?.font),
    colors: merge(DEFAULTS.colors, partial?.colors),
    align: partial?.align || DEFAULTS[zone]?.align || 'left',
    verticalAlign: partial?.verticalAlign || DEFAULTS[zone]?.verticalAlign || 'middle',
    padding: partial?.padding || DEFAULTS.layout.cellPadding
  }
}

/**
 * Свести стили шаблона в плоскую структуру для рендерера.
 * @param {object} template шаблон отчёта
 * @param {object} [block] блок таблицы (может иметь свой styles)
 * @returns {{layout: object, header: object, body: object, subtotal: object, grandtotal: object, columns: object, preset: string}}
 */
export function resolveStyles(template, block) {
  const own = template?.styles || {}
  const presetName = own.preset && PRESETS[own.preset] ? own.preset : 'default'
  const layers = [PRESETS[presetName], own, block?.styles || {}]

  let font = DEFAULTS.font
  let colors = DEFAULTS.colors
  let layout = DEFAULTS.layout
  // зоны стартуют с умолчаний версии 1.0, затем их правят пресет/шаблон/блок
  const zones = {}
  for (const zone of ZONES) zones[zone] = { ...DEFAULTS[zone] }
  const columns = { ...(own.columns || {}) }

  for (const layer of layers) {
    font = merge(font, layer.font)
    colors = merge(colors, layer.colors)
    layout = merge(layout, layer.layout)
    for (const zone of ZONES) {
      if (layer[zone]) zones[zone] = merge(zones[zone], layer[zone])
    }
    if (layer.columns) {
      for (const [field, style] of Object.entries(layer.columns)) {
        columns[field] = merge(columns[field] || {}, style)
      }
    }
  }

  // font/colors на верхнем уровне нужны диаграммам (оси, легенда) и текстовым блокам
  const out = { preset: presetName, font, colors, layout }
  for (const zone of ZONES) {
    // зона наследует общие шрифт/цвета документа, если не задала свои
    out[zone] = zoneStyle(zone, {
      ...zones[zone],
      font: merge(font, zones[zone]?.font),
      colors: merge(colors, zones[zone]?.colors)
    })
  }
  out.columns = columns
  return out
}

// CSS-переменные зоны: --rt-<префикс>-<свойство>
export function zoneVars(zone, prefix) {
  return {
    [`--rt-${prefix}-font-family`]: zone.font.family,
    [`--rt-${prefix}-font-size`]: `${zone.font.size}px`,
    [`--rt-${prefix}-font-weight`]: String(zone.font.weight),
    [`--rt-${prefix}-font-style`]: zone.font.style,
    [`--rt-${prefix}-line-height`]: String(zone.font.lineHeight),
    [`--rt-${prefix}-color`]: zone.colors.text,
    [`--rt-${prefix}-bg`]: zone.colors.background,
    [`--rt-${prefix}-align`]: zone.align,
    [`--rt-${prefix}-valign`]: zone.verticalAlign,
    [`--rt-${prefix}-padding`]: zone.padding
  }
}

/** Высота строки по её виду (detail / subtotal / grandtotal). */
export function rowHeightFor(layout, kind) {
  if (kind === 'subtotal') return layout.subtotalHeight
  if (kind === 'grandtotal') return layout.grandtotalHeight
  return layout.rowHeight
}
