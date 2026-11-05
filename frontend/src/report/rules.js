// Условное форматирование таблиц (ADR-25, версия 1.2).
// Правила декларативные: сравнение по сырому значению строки, оформление — поверх зоны и колонки.
// Сравнений в грамматике выражений нет, поэтому условия описываются объектом, а не строкой.

export const OPS = ['=', '!=', '>', '>=', '<', '<=', 'between', 'empty', 'notEmpty', 'contains']
export const RULE_ZONES = ['detail', 'subtotal', 'grandtotal']
export const DEFAULT_ZONES = ['detail', 'subtotal']

const isEmpty = (v) => v === null || v === undefined || v === ''

function compareSign(cmp, op) {
  switch (op) {
    case '=':
      return cmp === 0
    case '!=':
      return cmp !== 0
    case '>':
      return cmp > 0
    case '>=':
      return cmp >= 0
    case '<':
      return cmp < 0
    case '<=':
      return cmp <= 0
    default:
      return false
  }
}

const isIsoDate = (v) => typeof v === 'string' && /^\d{4}-\d{2}-\d{2}/.test(v)

/**
 * Сравнить значение с ожидаемым.
 * Числа и числовые строки сравниваются как числа, ISO-даты — как строки (лексикографически верно),
 * остальное — как строки. null/'' никогда не проходят числовые сравнения.
 */
export function compare(value, op, expected) {
  if (op === 'empty') return isEmpty(value)
  if (op === 'notEmpty') return !isEmpty(value)
  if (isEmpty(value)) return false

  if (op === 'contains') return String(value).toLowerCase().includes(String(expected).toLowerCase())

  if (op === 'between') {
    const [lo, hi] = Array.isArray(expected) ? expected : []
    const n = Number(value)
    if (!Number.isFinite(n) || !Number.isFinite(Number(lo)) || !Number.isFinite(Number(hi))) return false
    return n >= Number(lo) && n <= Number(hi)
  }

  if (isIsoDate(value) && isIsoDate(String(expected))) {
    const a = value.slice(0, 10)
    const b = String(expected).slice(0, 10)
    return compareSign(a < b ? -1 : a > b ? 1 : 0, op)
  }

  const n = Number(value)
  const e = Number(expected)
  if (Number.isFinite(n) && Number.isFinite(e) && value !== '' && expected !== '') {
    return compareSign(n < e ? -1 : n > e ? 1 : 0, op)
  }

  const a = String(value)
  const b = String(expected)
  return compareSign(a < b ? -1 : a > b ? 1 : 0, op)
}

/** Условие: одиночное {column, op, value} либо {all: [...]} / {any: [...]} без вложенности. */
export function matchWhen(when, getValue) {
  if (!when || typeof when !== 'object') return false
  if (Array.isArray(when.all)) return when.all.every((w) => matchWhen(w, getValue))
  if (Array.isArray(when.any)) return when.any.some((w) => matchWhen(w, getValue))
  if (!when.column || !OPS.includes(when.op)) return false
  return compare(getValue(when.column), when.op, when.value)
}

/** Действует ли правило на строку этой зоны. */
export function ruleApplies(rule, row, zone, getValue) {
  const zones = Array.isArray(rule?.zones) && rule.zones.length ? rule.zones : DEFAULT_ZONES
  if (!zones.includes(zone)) return false
  return matchWhen(rule.when, getValue)
}

const deepMerge = (base, patch) => {
  const out = { ...base }
  for (const [k, v] of Object.entries(patch || {})) {
    if (v === null || typeof v !== 'object' || Array.isArray(v)) out[k] = v
    else out[k] = deepMerge(base?.[k] || {}, v)
  }
  return out
}

/** Семантические цвета: 'danger' → #b42318 из styles.palette. */
export function resolveColor(value, palette) {
  if (typeof value !== 'string') return value
  return palette?.[value] || value
}

function toCss(apply, palette) {
  if (!apply) return {}
  const css = {}
  if (apply.colors?.text) css.color = resolveColor(apply.colors.text, palette)
  if (apply.colors?.background) css.background = resolveColor(apply.colors.background, palette)
  if (apply.font?.family) css.fontFamily = apply.font.family
  if (apply.font?.size) css.fontSize = apply.font.size + 'px'
  if (apply.font?.weight) css.fontWeight = String(apply.font.weight)
  if (apply.font?.style) css.fontStyle = apply.font.style
  if (apply.align) css.textAlign = apply.align
  if (apply.padding) css.padding = apply.padding
  return css
}

const PRIORITY = (r) => (Number.isFinite(r?.priority) ? r.priority : 0)

/**
 * Стиль правила для ячейки: scope 'cell' — только целевая колонка, scope 'row' — любая ячейка строки.
 * @param {Array} rules
 * @param {string} zone 'detail' | 'subtotal' | 'grandtotal'
 * @param {string|null} columnKey целевая колонка; null — стиль всей строки
 */
export function ruleStyleFor(rules, zone, columnKey, getValue, palette) {
  if (!Array.isArray(rules) || !rules.length) return null
  const matched = rules
    .filter((rule) => {
      if (!ruleApplies(rule, null, zone, getValue)) return false
      if (rule.scope === 'row') return true
      const target = rule.target || rule.when?.column
      return target === columnKey
    })
    .sort((a, b) => PRIORITY(a) - PRIORITY(b))

  if (!matched.length) return null
  let apply = {}
  for (const rule of matched) apply = deepMerge(apply, rule.apply)
  const css = toCss(apply, palette)
  return Object.keys(css).length ? css : null
}
