// Арифметика дат в default параметров (ADR-92).
//
// Выражение задаёт момент относительно «сейчас»:
//   *            — текущий момент (для date — сегодняшняя дата, для datetime — время до минут)
//   today(*)     — сегодня, 00:00
//   week(*)      — понедельник текущей недели, 00:00
//   month(*)     — первое число текущего месяца, 00:00
//   quarter(*)   — первое число месяца начала квартала, 00:00
//   year(*)      — 1 января текущего года, 00:00
// и цепочка смещений: +N/-N с единицами h (часы), d (дни), m (месяцы), y (годы):
//   *-5h, *-1d, month(*)-1m, *-30d+2h.
//
// Всё считается в локальном поясе браузера (решение PO): «начало месяца» — месяц пользователя.

export const EXPR_BASES = ['*', 'today(*)', 'week(*)', 'month(*)', 'quarter(*)', 'year(*)']

const BASE_RE = /^(\*|today\(\*\)|week\(\*\)|month\(\*\)|quarter\(\*\)|year\(\*\))/
const OFFSET_RE = /([+-])(\d+)([hdmy])/y

// Похоже ли значение на выражение: начинается с * или содержит вызов функции.
// Константы вида 2026-03-01 выражениями не считаются.
export function looksLikeDateExpr(value) {
  if (typeof value !== 'string') return false
  const s = value.trim()
  if (!s) return false
  return s.startsWith('*') || s.includes('(')
}

// Разбор выражения. Возвращает { ok, base, steps, error }.
export function parseDateExpr(expr) {
  const s = String(expr ?? '').trim()
  const m = BASE_RE.exec(s)
  if (!m) {
    return { ok: false, base: null, steps: [], error: `неизвестное начало выражения «${s}»` }
  }
  const base = m[1]
  const steps = []
  let i = base.length
  while (i < s.length) {
    OFFSET_RE.lastIndex = i
    const off = OFFSET_RE.exec(s)
    if (!off) {
      return {
        ok: false,
        base,
        steps,
        error: `не разобрано «${s.slice(i)}»: ожидается смещение вида +1d, -5h, +2m, -1y`
      }
    }
    steps.push({ sign: off[1] === '-' ? -1 : 1, amount: Number(off[2]), unit: off[3] })
    i = OFFSET_RE.lastIndex
  }
  return { ok: true, base, steps, error: null }
}

function startOfWeek(d) {
  // ISO: неделя начинается с понедельника
  const out = new Date(d.getFullYear(), d.getMonth(), d.getDate())
  const dow = (out.getDay() + 6) % 7
  out.setDate(out.getDate() - dow)
  return out
}

function applyBase(base, now) {
  const y = now.getFullYear()
  const mo = now.getMonth()
  const d = now.getDate()
  switch (base) {
    case '*':
      return new Date(now.getTime())
    case 'today(*)':
      return new Date(y, mo, d)
    case 'week(*)':
      return startOfWeek(now)
    case 'month(*)':
      return new Date(y, mo, 1)
    case 'quarter(*)':
      return new Date(y, Math.floor(mo / 3) * 3, 1)
    case 'year(*)':
      return new Date(y, 0, 1)
    default:
      return null
  }
}

function applyStep(date, { sign, amount, unit }) {
  const out = new Date(date.getTime())
  const delta = sign * amount
  if (unit === 'h') out.setHours(out.getHours() + delta)
  else if (unit === 'd') out.setDate(out.getDate() + delta)
  else if (unit === 'm') out.setMonth(out.getMonth() + delta)
  else if (unit === 'y') out.setFullYear(out.getFullYear() + delta)
  return out
}

// Вычисление выражения. null — если выражение не разобралось.
export function evalDateExpr(expr, now = new Date()) {
  const parsed = parseDateExpr(expr)
  if (!parsed.ok) return null
  let date = applyBase(parsed.base, now)
  if (!date) return null
  for (const step of parsed.steps) date = applyStep(date, step)
  return date
}

const pad = (n) => String(n).padStart(2, '0')

// Формат значения по типу параметра: date → YYYY-MM-DD, datetime → YYYY-MM-DDTHH:mm
// (ровно то, что показывает <input type="date"> и <input type="datetime-local">).
export function formatDateValue(date, type) {
  const day = `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())}`
  if (type === 'datetime') return `${day}T${pad(date.getHours())}:${pad(date.getMinutes())}`
  return day
}

// Значение параметра по умолчанию: выражение вычисляется, константа возвращается как есть.
export function resolveDefaultValue(param, now = new Date()) {
  const raw = param?.default
  if (raw === undefined || raw === null) return ''
  const type = param.type
  if ((type === 'date' || type === 'datetime') && looksLikeDateExpr(raw)) {
    const date = evalDateExpr(raw, now)
    if (!date) return ''
    return formatDateValue(date, type)
  }
  return String(raw)
}
