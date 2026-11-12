// Высота блоков отчёта (ADR-85): пиксели или процент от высоты окна.
// Шаблон задаёт одно значение, клиент переводит его в пиксели — виртуализация
// таблицы и ECharts работают только с числом, «%» без окна смысла не имеет.
export const BLOCK_HEIGHT_MIN = { table: 200, chart: 160 }
export const BLOCK_HEIGHT_MAX = 2000
export const BLOCK_HEIGHT_PERCENT_MIN = 10
export const BLOCK_HEIGHT_PERCENT_MAX = 100

// Значение по умолчанию, если шаблон высоту не задал
export const BLOCK_HEIGHT_DEFAULT = { table: 460, chart: 320 }

/**
 * Разбирает height блока: число → пиксели, строка «60%» → процент от окна.
 * Возвращает null, если значение не задано или не распознано (валидатор ругается отдельно).
 * @param {number|string|undefined} raw
 * @returns {{kind: 'px', value: number}|{kind: 'percent', value: number}|null}
 */
export function parseBlockHeight(raw) {
  if (raw === undefined || raw === null || raw === '') return null
  if (typeof raw === 'number') {
    return Number.isFinite(raw) ? { kind: 'px', value: raw } : null
  }
  if (typeof raw === 'string') {
    const m = /^(\d{1,3})%$/.exec(raw.trim())
    if (!m) return null
    return { kind: 'percent', value: Number(m[1]) }
  }
  return null
}

/**
 * Высота блока в пикселях для текущего окна.
 * @param {'table'|'chart'} type
 * @param {number|string|undefined} raw значение height из шаблона
 * @param {number} viewportHeight высота окна, px
 * @returns {number}
 */
export function resolveBlockHeight(type, raw, viewportHeight) {
  const min = BLOCK_HEIGHT_MIN[type] ?? 160
  const fallback = BLOCK_HEIGHT_DEFAULT[type] ?? 320
  const spec = parseBlockHeight(raw)
  if (!spec) return fallback
  if (spec.kind === 'px') return Math.min(BLOCK_HEIGHT_MAX, Math.max(min, Math.round(spec.value)))
  const pct = Math.min(BLOCK_HEIGHT_PERCENT_MAX, Math.max(BLOCK_HEIGHT_PERCENT_MIN, spec.value))
  const view = Number.isFinite(viewportHeight) && viewportHeight > 0 ? viewportHeight : 0
  return Math.max(min, Math.round((view * pct) / 100))
}

// Ширина блока в 12-колоночной сетке (ADR-96): span 1…12, старый width — half = 6, full = 12.
export function resolveBlockSpan(block) {
  const raw = Number(block?.span)
  if (Number.isInteger(raw) && raw >= 1 && raw <= 12) return raw
  return block?.width === 'half' ? 6 : 12
}
