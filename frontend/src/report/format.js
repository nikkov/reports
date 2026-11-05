// Форматы отображения значений (ADR-23, расширение требований PO 2026-11-05).
// Один резолвер на весь отчёт: колонка задаёт формат, стиль колонки может его перекрыть.
// Формат — либо строка-псевдоним ('number' | 'integer' | 'percent' | 'currency' | 'date'),
// либо объект с параметрами разделителей, знаков и аффиксов.

export const FORMAT_STYLES = ['number', 'integer', 'percent', 'currency', 'date']

const NBSP = '\u00a0' // разделитель тысяч по умолчанию — как у Intl.NumberFormat('ru-RU')

/** Привести формат к полному объекту. decimals сохраняется как есть (undefined = по Intl). */
export function normalizeFormat(format, decimals) {
  if (format && typeof format === 'object') {
    return {
      style: format.style || 'number',
      decimals: format.decimals ?? decimals,
      group: format.group ?? NBSP,
      decimal: format.decimal ?? ',',
      prefix: format.prefix ?? '',
      suffix: format.suffix ?? '',
      negative: format.negative || 'minus'
    }
  }
  const style = format || 'number'
  return {
    style,
    decimals,
    group: NBSP,
    decimal: ',',
    prefix: '',
    suffix: '',
    negative: 'minus'
  }
}

function groupDigits(intPart, separator) {
  return intPart.replace(/\B(?=(\d{3})+(?!\d))/g, separator)
}

function formatNumber(value, spec) {
  const n = Number(value)
  if (!Number.isFinite(n)) return String(value)
  const negative = n < 0
  const digits = spec.style === 'integer' ? 0 : spec.decimals

  let body
  if (digits === undefined) {
    // без явного числа знаков — как раньше: ru-RU, не больше трёх дробных
    body = new Intl.NumberFormat('ru-RU', { maximumFractionDigits: 3 }).format(Math.abs(n))
    if (spec.group !== NBSP) body = body.replace(/\u00a0/g, spec.group)
    if (spec.decimal !== ',') body = body.replace(',', spec.decimal)
  } else {
    const fixed = Math.abs(n).toFixed(digits)
    const [intPart, fracPart] = fixed.split('.')
    body = groupDigits(intPart, spec.group) + (fracPart ? spec.decimal + fracPart : '')
  }

  const text = `${spec.prefix}${body}${spec.suffix}`
  if (!negative) return text
  return spec.negative === 'parentheses' ? `(${text})` : `-${text}`
}

/**
 * Отформатировать значение ячейки.
 * @param {string|object} format строка-псевдоним или объект формата
 * @param {number|string|undefined} decimals переопределение знаков (legacy columns[].decimals)
 * @param {*} value значение
 * @returns {string|null} null — если форматировать нечего (пустое значение)
 */
export function formatCell(format, decimals, value) {
  if (value === null || value === undefined || value === '') return null
  const spec = normalizeFormat(format, decimals)

  if (spec.style === 'date') {
    const m = String(value).match(/^(\d{4})-(\d{2})-(\d{2})(?:[T ](\d{2}):(\d{2}))?/)
    if (!m) return String(value)
    const date = `${m[3]}.${m[2]}.${m[1]}`
    return m[4] ? `${date} ${m[4]}:${m[5]}` : date
  }

  if (spec.style === 'percent') {
    const n = Number(value)
    if (!Number.isFinite(n)) return String(value)
    const digits = spec.decimals === undefined ? 1 : spec.decimals
    return `${(n * 100).toFixed(digits).replace('.', spec.decimal)} %`
  }

  if (spec.style === 'currency') {
    const n = Number(value)
    if (!Number.isFinite(n)) return String(value)
    return formatNumber(n, { ...spec, decimals: spec.decimals ?? 2, suffix: spec.suffix || ` ${NBSP}₽` })
  }

  return formatNumber(value, spec)
}

/** Какой формат применится к колонке: стиль колонки сильнее самой колонки. */
export function columnFormat(col, styleColumn) {
  const format = styleColumn?.format ?? col?.format
  if (format) return { format, decimals: (typeof format === 'object' ? format.decimals : undefined) ?? col?.decimals }
  if (col?.type === 'date') return { format: 'date', decimals: undefined }
  if (col?.aggregate) return { format: 'number', decimals: col?.decimals }
  return null
}
