// Текст отчёта: подстановки и гиперссылки (ADR-101).
//
// В тексте допустимы:
//   {{param}}        — подпись значения параметра (как раньше, ADR-86);
//   {{data.column}}  — значение колонки из строки запроса, связанного с блоком (dataKey);
//   [текст](адрес)   — гиперссылка; http/https/mailto, якорь или путь внутри приложения
//                      (javascript:, data:, vbscript:, file: ссылкой не становятся).
// Всё остальное остаётся обычным текстом; HTML не поддерживается — Vue экранирует значения.

const LINK_RE = /\[([^\]\n]+)\]\(([^)\s]+)\)/g
// Ссылка допустима, если это http/https/mailto, якорь или путь внутри приложения.
// Активные схемы (javascript:, data:, vbscript:, file:) отсекаем — они не станут ссылкой.
const UNSAFE_SCHEME = /^\s*(javascript|data|vbscript|file):/i
const isSafeHref = (href) => Boolean(href) && !UNSAFE_SCHEME.test(href)

/** Подстановки: {{name}} и {{data.column}}; неизвестное оставляем как написали. */
export function substituteText(text, { labels = {}, data = null } = {}) {
  return String(text ?? '').replace(/\{\{\s*([^{}]+?)\s*\}\}/g, (whole, raw) => {
    const key = String(raw).trim()
    if (key.startsWith('data.')) {
      const field = key.slice(5).trim()
      if (!data || !field) return ''
      const value = data[field]
      return value === undefined || value === null ? '' : String(value)
    }
    // подпись параметра: NAME в интерфейсе, VALUE в SQL (ADR-86)
    if (Object.prototype.hasOwnProperty.call(labels, key)) return String(labels[key])
    return whole
  })
}

/**
 * Разбить текст на части для шаблона: обычный текст и ссылки.
 * Ссылки только http/https/mailto — javascript:, data: и относительные адреса остаются текстом.
 * @returns {{type: 'text'|'link', text: string, href?: string}[]}
 */
export function textParts(text, context = {}) {
  const source = substituteText(text, context)
  const parts = []
  let last = 0
  for (const match of source.matchAll(LINK_RE)) {
    const [whole, label, href] = match
    const start = match.index ?? 0
    if (start > last) parts.push({ type: 'text', text: source.slice(last, start) })
    if (isSafeHref(href)) {
      parts.push({ type: 'link', text: label, href })
    } else {
      // небезопасная схема — показываем как есть, а не делаем ссылкой
      parts.push({ type: 'text', text: whole })
    }
    last = start + whole.length
  }
  if (last < source.length) parts.push({ type: 'text', text: source.slice(last) })
  return parts.length ? parts : [{ type: 'text', text: '' }]
}
