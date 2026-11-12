// Экспорт отчёта в Excel (ADR-28): из модели витрины, а не из DOM.
// В DOM живёт только окно виртуализации (~40 строк), поэтому экспорт строится
// по тем же данным и той же логике группировки, что и экран.
import ExcelJS from 'exceljs'
import { buildTableModel } from '../../engine/group.js'
import { formatCell, normalizeFormat } from '../format.js'
import { ruleStyleFor } from '../rules.js'

// Листья колонок: заголовки-группы (children) разворачиваются в path для объединений шапки.
export function leafColumns(block) {
  const out = []
  const walk = (col, path) => {
    if (Array.isArray(col.children)) {
      for (const child of col.children) walk(child, path.concat(col.title))
      return
    }
    out.push({ col, headerPath: path })
  }
  for (const col of block.columns || []) walk(col, [])
  return out
}

// Числовой формат Excel из спецификации шаблона (format.js) — числа остаются числами.
export function excelNumberFormat(format, decimals) {
  const spec = normalizeFormat(format, decimals)
  const digits = spec.decimals === undefined ? 0 : spec.decimals
  switch (spec.style) {
    case 'percent':
      return `${'0'.padEnd(1, '0')}${digits ? '.' + '0'.repeat(digits) : ''}%`
    case 'currency':
      return `# ##0${digits ? ',' + '0'.repeat(digits) : ''}${spec.suffix ? `"${spec.suffix}"` : ''}`
    case 'integer':
      return '# ##0'
    case 'date':
      // pattern из шаблона → коды Excel (ADR-88): YYYY→yyyy, DD→dd, HH→hh, mm остаётся mm
      return spec.pattern
        ? spec.pattern
            .replace(/YYYY/g, 'yyyy')
            .replace(/YY/g, 'yy')
            .replace(/MM/g, 'mm')
            .replace(/DD/g, 'dd')
            .replace(/HH/g, 'hh')
            .replace(/ss/g, 'ss')
            .replace(/(^|[^a-zA-Z])([MD])(?![a-zA-Z])/g, (m0, p, t) => p + t.toLowerCase())
        : 'dd.mm.yyyy'
    default:
      return digits ? `# ##0${'.' + '0'.repeat(digits)}` : '# ##0'
  }
}

// формат даты задан объектом с pattern — та же ветка, что и у колонки type=date
const isDateStyle = (format) => format === 'date' || (format && typeof format === 'object' && (format.style === 'date' || !!format.pattern))

const isNumericStyle = (format, decimals) => {
  const spec = normalizeFormat(format, decimals)
  return ['number', 'integer', 'percent', 'currency'].includes(spec.style)
}

function toDate(value) {
  const m = String(value).match(/^(\d{4})-(\d{2})-(\d{2})(?:[T ](\d{2}):(\d{2}))?/)
  if (!m) return null
  return new Date(Number(m[1]), Number(m[2]) - 1, Number(m[3]), Number(m[4] || 0), Number(m[5] || 0))
}

function zoneStyle(resolved, zone) {
  const z = resolved[zone]
  return {
    font: {
      name: z.font.family === 'system-ui' ? 'Calibri' : z.font.family,
      size: z.font.size,
      bold: Number(z.font.weight) >= 600,
      italic: z.font.style === 'italic',
      color: { argb: argb(z.colors.text) }
    },
    fill: { type: 'pattern', pattern: 'solid', fgColor: { argb: argb(z.colors.background) } },
    alignment: {
      horizontal: z.align === 'right' ? 'right' : z.align === 'center' ? 'center' : 'left',
      vertical: z.verticalAlign === 'top' ? 'top' : z.verticalAlign === 'bottom' ? 'bottom' : 'middle',
      wrapText: false
    },
    border: borderFor(resolved.layout.borders, z.colors.border)
  }
}

function argb(color) {
  const hex = String(color || '#000000').replace('#', '')
  if (hex.length === 3) return 'FF' + hex.split('').map((c) => c + c).join('').toUpperCase()
  if (hex.length === 6) return 'FF' + hex.toUpperCase()
  return 'FF000000'
}

function borderFor(mode, color) {
  const side = { style: 'thin', color: { argb: argb(color) } }
  if (mode === 'none') return {}
  if (mode === 'grid') return { top: side, left: side, bottom: side, right: side }
  return { bottom: side }
}

/**
 * Собрать книгу: лист «Отчёт» (шапка, строки, подытоги, итог) и лист «Параметры».
 * @param {{block: object, rows: object[], styles: object, params?: object, labels?: object, meta?: object}} input
 * @returns {Promise<Blob>}
 */
/**
 * Собрать книгу: по листу на каждый табличный блок + лист «Параметры».
 * Если таблица одна — один лист отчёта (прежнее поведение).
 * @param {{sheets?: {block: object, rows: object[], styles: object}[], block?: object, rows?: object[],
 *          styles?: object, params?: object, labels?: object, meta?: object}} input
 * @returns {Promise<Blob>}
 */
export async function buildWorkbook({ sheets, block, rows, styles, params = {}, labels = {}, meta = {} }) {
  const list = Array.isArray(sheets) && sheets.length ? sheets : [{ block, rows, styles }]
  const wb = new ExcelJS.Workbook()
  wb.creator = 'Сервер отчётов'
  wb.created = new Date()
  wb.properties = { ...(wb.properties || {}) }

  // имя листа: заголовок таблицы, иначе «Таблица N»; повторы и запрещённые символы — Excel не примет
  const used = new Set(['параметры']) // сравнение без учёта регистра, как в Excel
  list.forEach((entry, index) => {
    const title = entry.block?.title || (list.length > 1 ? `Таблица ${index + 1}` : meta.title || 'Отчёт')
    addSheet(wb, entry.block, entry.rows || [], entry.styles, uniqueSheetName(title, used))
  })

  // --- лист «Параметры» ---
  const ps = wb.addWorksheet('Параметры')
  ps.columns = [
    { header: 'Параметр', key: 'name', width: 24 },
    { header: 'Подпись', key: 'label', width: 34 },
    { header: 'Значение', key: 'value', width: 40 }
  ]
  ps.getRow(1).font = { bold: true }
  for (const [name, value] of Object.entries(params)) {
    ps.addRow({ name, label: labels[name] || '', value: String(value ?? '') })
  }
  ps.addRow({})
  ps.addRow({ name: 'Отчёт', label: meta.title || '', value: meta.id || '' })
  ps.addRow({ name: 'Выгружен', label: '', value: new Date().toLocaleString('ru-RU') })

  const buffer = await wb.xlsx.writeBuffer()
  return new Blob([buffer], { type: 'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet' })
}

// Уникальное имя листа: Excel не различает регистр и не принимает длиннее 31 символа.
function uniqueSheetName(title, used) {
  const base = sheetName(title)
  let name = base
  let n = 2
  while (used.has(name.toLowerCase())) {
    const suffix = ` (${n++})`
    name = base.slice(0, 31 - suffix.length) + suffix
  }
  used.add(name.toLowerCase())
  return name
}

function addSheet(wb, block, rows, styles, name) {
  const leaves = leafColumns(block)
  const columns = leaves.map((l) => l.col)
  const model = buildTableModel(rows, columns, {
    grandTotal: block.grandTotal !== false,
    nullLabel: block.nullText || '(пусто)'
  })

  const ws = wb.addWorksheet(name, {
    views: [{ state: 'frozen', ySplit: Math.max(1, Math.max(...leaves.map((l) => l.headerPath.length)) + 1) }]
  })

  // --- шапка с объединениями многоуровневого заголовка ---
  const depth = Math.max(1, ...leaves.map((l) => l.headerPath.length + 1))
  const headerStyle = zoneStyle(styles, 'header')
  for (let r = 0; r < depth; r++) {
    for (let c = 0; c < columns.length; c++) {
      const cell = ws.getCell(r + 1, c + 1)
      cell.style = { ...headerStyle }
    }
  }
  const groups = new Map()
  leaves.forEach((leaf, c) => {
    leaf.headerPath.forEach((title, level) => {
      const key = level + '\u0001' + leaf.headerPath.slice(0, level + 1).join('\u0001')
      if (!groups.has(key)) groups.set(key, { level, start: c, end: c, title })
      else groups.get(key).end = c
    })
    const cell = ws.getCell(depth, c + 1)
    cell.value = leaf.col.title
    cell.style = { ...headerStyle }
  })
  for (const g of groups.values()) {
    const cell = ws.getCell(g.level + 1, g.start + 1)
    cell.value = g.title
    if (g.end > g.start) ws.mergeCells(g.level + 1, g.start + 1, g.level + 1, g.end + 1)
  }

  // ширины колонок из стилей (px → символы Excel)
  leaves.forEach((leaf, c) => {
    const width = styles.columns?.[leaf.col.field]?.width ?? leaf.col.width
    ws.getColumn(c + 1).width = typeof width === 'number' ? Math.max(8, Math.round(width / 7)) : 18
  })

  // --- строки модели: данные, подытоги, итог ---
  // Зона стилей и зона правил называются по-разному: в шаблоне правила адресуют 'detail',
  // а стили зон — 'body'. Раньше в правила уходило 'body', и условия для 'detail' не
  // срабатывали — в книге не было ни цвета шрифта, ни фона (ADR-100).
  const styleZoneOf = (kind) => (kind === 'subtotal' ? 'subtotal' : kind === 'grandtotal' ? 'grandtotal' : 'body')
  const ruleZoneOf = (kind) => (kind === 'subtotal' ? 'subtotal' : kind === 'grandtotal' ? 'grandtotal' : 'detail')
  const getValueFor = (row, columnKey) => {
    const index = leaves.findIndex((l) => l.col.field === columnKey || l.col.title === columnKey)
    return index < 0 ? undefined : row.values?.[index]
  }

  model.rows.forEach((row, ri) => {
    const excelRow = ws.getRow(depth + ri + 1)
    leaves.forEach((leaf, c) => {
      const col = leaf.col
      const raw = row.values?.[c]
      const cell = excelRow.getCell(c + 1)
      const styleZone = styleZoneOf(row.kind)
      const ruleZone = ruleZoneOf(row.kind)
      const base = zoneStyle(styles, styleZone)
      let numFmt = null // задаём после cell.style: присвоение style затирает числовой формат

      if (col.group && row.kind !== 'detail') {
        cell.value = String(raw ?? '')
      } else if (col.expression || col.aggregate) {
        if (raw === null || raw === undefined || raw === '') {
          cell.value = styles.nullText || '—'
        } else if (isNumericStyle(col.format, col.decimals) && Number.isFinite(Number(raw))) {
          cell.value = Number(raw)
          numFmt = excelNumberFormat(col.format, col.decimals)
        } else {
          cell.value = formatCell(col.format, col.decimals, raw) ?? String(raw)
        }
      } else if (col.type === 'date' || isDateStyle(col.format)) {
        const d = toDate(raw)
        if (d) {
          cell.value = d
          numFmt = isDateStyle(col.format) ? excelNumberFormat(col.format, col.decimals) : 'dd.mm.yyyy'
        } else cell.value = String(raw ?? '')
      } else if (typeof raw === 'number') {
        cell.value = raw
        if (col.format) numFmt = excelNumberFormat(col.format, col.decimals)
      } else {
        cell.value = raw === null || raw === undefined ? '' : String(raw)
      }

      cell.style = { ...base }
      if (numFmt) cell.numFmt = numFmt
      if (col.align) cell.alignment = { ...cell.alignment, horizontal: col.align }
      if (col.type === 'date' || isDateStyle(col.format)) cell.alignment = { ...cell.alignment, horizontal: 'center' }

      // правила условного форматирования — те же, что на экране
      if (block.rules?.length) {
        const rule = ruleStyleFor(
          block.rules,
          ruleZone,
          col.field || col.title,
          (name) => getValueFor(row, name),
          styles.palette
        )
        if (rule) {
          if (rule.color) cell.font = { ...cell.font, color: { argb: argb(rule.color) } }
          if (rule.background) cell.fill = { type: 'pattern', pattern: 'solid', fgColor: { argb: argb(rule.background) } }
          if (rule.fontWeight) cell.font = { ...cell.font, bold: Number(rule.fontWeight) >= 600 }
        }
      }
    })
  })

  // объединения модели (группы) — как на экране
  for (const m of model.merges || []) {
    const r1 = depth + m.row + 1
    const c1 = m.col + 1
    const r2 = r1 + (m.rowspan || 1) - 1
    const c2 = c1 + (m.colspan || 1) - 1
    if (r2 > r1 || c2 > c1) {
      try {
        ws.mergeCells(r1, c1, r2, c2)
      } catch {
        /* пересекающиеся объединения Excel не принимает — пропускаем */
      }
    }
  }

  // collapsible: false — группы всегда раскрыты и в книге: без структуры и кнопок +/- (ADR-94)
  if (block?.collapsible !== false) {
    // outline: детальные строки — внутри групп, подытог группы уровня L — на уровень L.
    // Глубину берём по фактическим подытогам: если подытоги только у верхней группы,
    // детали идут уровнем 1 (а не «висящим» вторым уровнем без родителя).
    ws.properties.outlineProperties = { summaryBelow: true, summaryRight: false }
    const groupDepth = model.rows.reduce(
      (max, row) => Math.max(max, Array.isArray(row.groupPath) ? row.groupPath.length : 0),
      0
    )
    // Вложенность Excel строится по строкам, а не по объединениям: чтобы категория сворачивалась
    // отдельно от региона, первая строка каждой категории остаётся видимой опорой (уровень на 1 меньше),
    // а её детали уходят на уровень глубже. Иначе все детали лежат одним уровнем и Excel даёт
    // сворачивание только по границам подытогов, то есть только по региону.
    let prevGroup = null
    model.rows.forEach((row, ri) => {
      let level = 0
      if (row.kind === 'detail') {
        const key = Array.isArray(row.groupPath) ? row.groupPath.join('\u0001') : ''
        const anchor = key !== prevGroup
        prevGroup = key
        level = groupDepth >= 2 ? groupDepth - (anchor ? 1 : 0) : Math.max(1, groupDepth)
      } else {
        prevGroup = null
        level = Math.max(0, Number(row.level || 0))
      }
      const clamp = Math.min(7, Math.max(0, level)) // Excel поддерживает 7 уровней структуры
      if (clamp > 0) ws.getRow(depth + ri + 1).outlineLevel = clamp
    })
  }

  ws.autoFilter = { from: { row: depth, column: 1 }, to: { row: depth, column: columns.length } }

}

/**
 * Excel запрещает в имени листа: * ? : \ / [ ] — и ограничивает 31 символом.
 * Длинное название обрезаем и помечаем «…», чтобы было видно, что оно сокращено.
 */
export function sheetName(title) {
  const clean = String(title).replace(/[*?:\\/[\]]/g, '·').replace(/\s+/g, ' ').trim()
  const short = clean || 'Отчёт'
  if (short.length <= 31) return short
  return short.slice(0, 30).trimEnd() + '…'
}

/** Имя файла: <id>-<YYYY-MM-DD>.xlsx */
export function exportFileName(id) {
  const d = new Date()
  const pad = (n) => String(n).padStart(2, '0')
  return `${id}-${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}.xlsx`
}
