// Печатный документ (ADR-28): отдельное окно с заголовком и таблицей, разбитой на страницы.
//
// Почему не печать страницы приложения: в печатном макете Chromium выносит таблицу на вторую
// страницу целиком (мешают контейнер виртуализации, sticky-шапка и объединённые ячейки),
// а таблица в несколько тысяч строк вообще не пагинируется. Поэтому разметка готовится здесь:
// строки режутся на страницы по PRINT_ROWS_PER_PAGE, в каждой повторяется шапка,
// объединённые ячейки разворачиваются в повтор значения — так строка не «рвётся» между страницами.

// Блоки по 200 строк: сами блоки браузер режет по строкам (мелкие таблицы пагинируются
// надёжно, одна таблица на тысячи строк — нет), а шапка повторяется в каждом блоке.
const PRINT_ROWS_PER_PAGE = 200

const PRINT_CSS = `
@page { size: A4 landscape; margin: 10mm 8mm; }
* { box-sizing: border-box; }
body { margin: 0; font-family: 'PT Sans', 'Segoe UI', Arial, sans-serif; color: #1c1f23; background: #fff; }
h1 { font-size: 16px; margin: 0 0 2px; }
.meta { font-size: 11px; color: #5b6470; margin: 0 0 8px; }
/* контейнер нужен ради CSS-переменных шаблона, но без contain: strict — иначе Chromium не пагинирует */
.rt { contain: none !important; height: auto !important; max-height: none !important; overflow: visible !important;
      position: static !important; border: 0 !important; border-radius: 0 !important; }
.rt-table { width: 100%; margin: 0 0 4px; }
.rt-table thead { display: table-header-group; position: static !important; }
.rt-table th { position: static !important; }
.print-table th, .print-table td { border: 1px solid #9aa3ad; padding: 2px 4px; }
.chev { display: none !important; }
`

const escapeHtml = (s) =>
  String(s ?? '').replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c])

/** Стили приложения: без них пропадут шрифты, цвета и ширины колонок. */
function collectStyles(doc) {
  return Array.from(doc.querySelectorAll('style'))
    .map((el) => el.textContent || '')
    .join('\n')
}

/**
 * Развернуть объединённые ячейки в сетку: каждая строка получает значение своей группы.
 * Иначе rowspan в сотни строк не даёт разбить таблицу между страницами.
 */
function buildGrid(table) {
  const rows = Array.from(table.querySelectorAll('tbody tr'))
  const grid = []
  rows.forEach((tr, r) => {
    grid[r] = grid[r] || []
    let c = 0
    for (const td of Array.from(tr.cells)) {
      while (grid[r][c]) c++
      const rs = td.rowSpan || 1
      const cs = td.colSpan || 1
      // в ячейке группы на экране живёт «▾» — на бумаге он не нужен
      const clean = td.cloneNode(true)
      clean.querySelectorAll('.chev').forEach((el) => el.remove())
      const value = clean.textContent.replace(/\s+/g, ' ').trim()
      const cls = td.className || ''
      for (let dr = 0; dr < rs; dr++) {
        grid[r + dr] = grid[r + dr] || []
        for (let dc = 0; dc < cs; dc++) grid[r + dr][c + dc] = { value, cls }
      }
      c += cs
    }
  })
  return { grid, rowClasses: rows.map((tr) => tr.className || '') }
}

/** Разметка печати: массив таблиц по PRINT_ROWS_PER_PAGE строк с повтором шапки. */
export function buildPrintTables(table) {
  const thead = table.querySelector('thead')
  const colgroup = table.querySelector('colgroup')
  const { grid, rowClasses } = buildGrid(table)
  const width = grid.reduce((max, row) => Math.max(max, row.length), 0)

  const tables = []
  for (let start = 0; start < grid.length; start += PRINT_ROWS_PER_PAGE) {
    const body = grid
      .slice(start, start + PRINT_ROWS_PER_PAGE)
      .map((row, i) => {
        const cls = rowClasses[start + i]
        const cells = Array.from({ length: width }, (_, ci) => {
          const cell = row[ci]
          return `<td class="${escapeHtml(cell?.cls || '')}">${escapeHtml(cell?.value || '')}</td>`
        }).join('')
        return `<tr class="${escapeHtml(cls)}">${cells}</tr>`
      })
      .join('')
    tables.push(
      `<table class="rt-table print-table">${colgroup ? colgroup.outerHTML : ''}${thead ? thead.outerHTML : ''}<tbody>${body}</tbody></table>`
    )
  }
  return tables
}

/** Открыть печатное окно: заголовок, метаданные и готовые таблицы. */
export function openPrintWindow({ title, meta = '', tables, styles = '' }) {
  const html = `<!doctype html>
<html lang="ru"><head><meta charset="utf-8"><title>${escapeHtml(title)}</title>
<style>${styles}</style><style>${PRINT_CSS}</style>
</head><body>
<h1>${escapeHtml(title)}</h1>
${meta ? `<p class="meta">${escapeHtml(meta)}</p>` : ''}
<div class="rt">${tables.join('\n')}</div>
</body></html>`

  const win = window.open('', '_blank', 'width=1200,height=800')
  if (!win) return null
  win.document.open()
  win.document.write(html)
  win.document.close()
  return win
}

/** Печать после отрисовки: шрифты и раскладка должны быть готовы. */
export async function printWindow(win, delayMs = 350) {
  await new Promise((resolve) => setTimeout(resolve, delayMs))
  win.focus()
  win.print()
}

/** Собрать данные для печати из текущей страницы отчёта. */
export function printPayload(doc, { title, meta }) {
  const table = doc.querySelector('.rt')
  if (!table) return null
  return { title, meta, tables: buildPrintTables(table), styles: collectStyles(doc) }
}
