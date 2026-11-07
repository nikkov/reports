<template>
  <div class="rt-wrap">
  <div class="rt rt-screen" :class="['b-' + layout.borders, { zebra: layout.zebra }]" :style="rootStyle" ref="scroller" @scroll="onScroll">
    <table class="rt-table" :style="{ tableLayout: layout.tableLayout }">
        <colgroup>
          <col v-for="(c, ci) in columns" :key="c.field || ci" :style="colWidth(ci)" />
        </colgroup>
        <thead>
          <tr v-for="(row, ri) in headerRows" :key="ri">
            <th
              v-for="(cell, ci) in row"
              :key="ci"
              :colspan="cell.colspan || 1"
              :rowspan="cell.rowspan || 1"
              :class="{ num: cell.align === 'right', group: (cell.rowspan || 1) > 1 && ri < headerRows.length - 1 }"
            >
              {{ cell.title }}
            </th>
          </tr>
        </thead>
        <tbody>
          <!-- Верхний/нижний спейсер вместо полотна высотой rows*rowHeight (1,3 млн px при 50k). -->
          <tr v-if="topPad > 0" class="spacer" :style="{ height: topPad + 'px' }">
            <td :colspan="columns.length"></td>
          </tr>
          <tr
            v-for="(row, i) in windowRows"
            :key="rowKey(row, i)"
            :class="[row.kind, { stripe: isStripe(start + i, row) }]"
          >
            <template v-for="(cell, ci) in cellsAt(start + i)" :key="cell.key">
              <td
                v-if="cell.render"
                :rowspan="cell.rowspan"
                :colspan="cell.colspan"
                :style="cellStyle(cell.col, start + i, row.kind)"
                :class="{
                  num: columns[cell.col]?.align === 'right',
                  ctr: columns[cell.col]?.align === 'center',
                  total: row.kind !== 'detail',
                  inherit: cell.inherit,
                  cont: cell.continued
                }"
              >
                <span v-if="cell.toggle" class="chev"            @click.stop="handleToggle(cell.toggle)">{{
                  cell.expanded ? '▾' : '▸'
                }}</span>
                <a
                  v-if="cell.drillable"
                  class="drill"
                  href="#"
                  :title="drill.label || 'Открыть детали'"
                  @click.stop.prevent="emit('drill', { row, spec: drill })"
                >{{ pretty(cell.text) }}</a>
                <template v-else>{{ pretty(cell.text) }}</template>
              </td>
            </template>
          </tr>
          <tr v-if="bottomPad > 0" class="spacer" :style="{ height: bottomPad + 'px' }">
            <td :colspan="columns.length"></td>
          </tr>
        </tbody>
      </table>
  </div>

    <!-- Печатный слой (ADR-28): та же модель и те же стили, но таблица нарезана на страницы,
         шапка повторяется, объединения пересчитаны по границам страницы. Виден только в @media print. -->
    <div v-if="printMode" class="rt-print" :class="['b-' + layout.borders, { zebra: layout.zebra }]" :style="printVars">
      <table
        v-for="(chunk, ci) in printChunks"
        :key="ci"
        class="rt-table"
        :style="{ tableLayout: layout.tableLayout }"
      >
        <colgroup>
          <col v-for="(c, x) in columns" :key="c.field || x" :style="colWidth(x)" />
        </colgroup>
        <thead>
          <tr v-for="(row, ri) in headerRows" :key="ri">
            <th
              v-for="(cell, x) in row"
              :key="x"
              :colspan="cell.colspan || 1"
              :rowspan="cell.rowspan || 1"
              :class="{ num: cell.align === 'right' }"
            >{{ cell.title }}</th>
          </tr>
        </thead>
        <tbody>
          <tr
            v-for="i in chunkRows(chunk)"
            :key="i"
            :class="[rows[i].kind, { stripe: isStripe(i, rows[i]) }]"
          >
            <td
              v-for="cell in printCells(i, chunk)"
              :key="cell.key"
              :rowspan="cell.rowspan"
              :colspan="cell.colspan"
              :style="cellStyle(cell.col, i, rows[i].kind)"
              :class="{
                num: columns[cell.col]?.align === 'right',
                ctr: columns[cell.col]?.align === 'center',
                total: rows[i].kind !== 'detail',
                inherit: cell.inherit,
                cont: cell.continued
              }"
            >{{ pretty(cell.text) }}</td>
          </tr>
        </tbody>
      </table>
    </div>
  </div>
</template>

<script setup>
// Подписи групп и ячеек с датой: ISO → 05.03.2026 (модель хранит исходные значения).
function pretty(value) {
  // подписи бывают с префиксом («Итого 2025-10-01»), поэтому заменяем ISO-дату в любом месте строки
  return String(value ?? '').replace(/\b(\d{4})-(\d{2})-(\d{2})\b/g, (_, y, m, d) => `${d}.${m}.${y}`)
}

// Свой рендерер таблицы отчёта (ADR-20): нативная <table>, объединения из модели.
// Стили (ADR-23) приходят разрешёнными из шаблона и уходят в CSS-переменные:
// inline-стили на ячейках — только для переопределений конкретной колонки.
import { computed, ref } from 'vue'
import { DEFAULTS, rowHeightFor, zoneVars } from '../template/styles.js'

const props = defineProps({
  rows: { type: Array, required: true },
  columns: { type: Array, required: true }, // [{field,title,align,width}]
  merges: { type: Array, default: () => [] }, // [{row,col,rowspan,colspan}]
  groupCount: { type: Number, default: 0 }, // колонки-группы (объединяются)
  height: { type: Number, default: 520 },
  overscan: { type: Number, default: 8 },
  // печать: включаем печатный слой (ADR-28), экран остаётся виртуализированным
  printMode: { type: Boolean, default: false },
  // drill-through (ADR-29): описание перехода из подытога в другой отчёт
  drill: { type: Object, default: null },
  // печать: рендерим весь отчёт, без окна виртуализации (ADR-28)
  virtual: { type: Boolean, default: true },
  expanded: { type: Set, default: () => new Set() },
  cellText: { type: Function, required: true }, // (row, colField) => string
  // (rowIndex, columnKey|null, zone) => CSS-объект правила или null (ADR-25)
  ruleStyle: { type: Function, default: null },
  onToggle: { type: Function, default: null }, // (groupPath) => void — надёжнее emit из вложенного шаблона
  // разрешённые стили: { layout, header, body, subtotal, grandtotal, columns, font, colors }
  styles: {
    type: Object,
    default: () => ({
      layout: DEFAULTS.layout,
      header: { ...DEFAULTS.header, font: DEFAULTS.font, colors: DEFAULTS.colors, padding: DEFAULTS.layout.cellPadding },
      body: { ...DEFAULTS.body, font: DEFAULTS.font, colors: DEFAULTS.colors, padding: DEFAULTS.layout.cellPadding },
      subtotal: { ...DEFAULTS.subtotal, font: DEFAULTS.font, colors: DEFAULTS.colors, padding: DEFAULTS.layout.cellPadding },
      grandtotal: { ...DEFAULTS.grandtotal, font: DEFAULTS.font, colors: DEFAULTS.colors, padding: DEFAULTS.layout.cellPadding },
      columns: {}
    })
  }
})

const emit = defineEmits(['toggle', 'drill'])

const scroller = ref(null)
const scrollTop = ref(0)

const layout = computed(() => props.styles.layout)

// Короткий отчёт не должен оставлять пустое поле под последней строкой:
// контейнер сжимается по содержимому, длинный — остаётся окном фиксированной высоты.
const containerHeight = computed(() =>
  Math.min(props.height, Math.max(layout.value.rowHeight, totalHeight.value + headerHeight.value))
)

const rootStyle = computed(() => {
  const { layout, header, body, subtotal, grandtotal } = props.styles
  return {
    height: containerHeight.value + 'px',
    '--rt-border': body.colors.border,
    '--rt-stripe': layout.stripeColor,
    '--rt-row-height': layout.rowHeight + 'px',
    '--rt-header-height': layout.headerHeight + 'px',
    '--rt-subtotal-height': layout.subtotalHeight + 'px',
    '--rt-grandtotal-height': layout.grandtotalHeight + 'px',
    '--rt-cell-padding': layout.cellPadding,
    ...zoneVars(header, 'h'),
    ...zoneVars(body, 'b'),
    ...zoneVars(subtotal, 's'),
    ...zoneVars(grandtotal, 'g')
  }
})

// Многоуровневый заголовок: группы берут colspan по числу листьев, листья —
// rowspan на остаток строк; колонка без группы занимает все строки заголовка.
const headerRows = computed(() => {
  const cols = props.columns
  const depth = Math.max(1, ...cols.map((c) => (c.headerPath?.length || 0) + 1))
  const rows = Array.from({ length: depth }, () => [])
  const runs = Array.from({ length: depth }, () => new Map())

  for (const col of cols) {
    const path = col.headerPath || []
    for (let l = 0; l < path.length; l++) {
      const prefix = path.slice(0, l + 1).join('\u0001')
      const existing = runs[l].get(prefix)
      if (existing) {
        existing.colspan++
        continue
      }
      const cell = { title: path[l], colspan: 1, rowspan: 1, align: 'left' }
      runs[l].set(prefix, cell)
      rows[l].push(cell)
    }
    rows[path.length].push({
      title: col.title,
      colspan: 1,
      rowspan: depth - path.length,
      align: col.headerAlign || col.align
    })
  }
  return rows
})

// Виртуализация по высотам зон (ADR-23): одна строка может быть выше другой,
// поэтому смещения считаются префиксными суммами, а не умножением на константу.
const offsets = computed(() => {
  const rows = props.rows
  const off = new Float64Array(rows.length + 1)
  for (let i = 0; i < rows.length; i++) {
    off[i + 1] = off[i] + rowHeightFor(layout.value, rows[i].kind)
  }
  return off
})

const totalHeight = computed(() => offsets.value[props.rows.length] || 0)

// Заголовок перекрывает строки только когда прокрутка началась: при scrollTop = 0
// первые строки (например, «Итого» свёрнутой группы) видны полностью.
const headerHeight = computed(() => headerRows.value.length * layout.value.headerHeight)

function lowerBound(arr, value) {
  let lo = 0
  let hi = arr.length - 1
  while (lo < hi) {
    const mid = (lo + hi) >> 1
    if (arr[mid] < value) lo = mid + 1
    else hi = mid
  }
  return lo
}

const anchor = computed(() => scrollTop.value + (scrollTop.value > 0 ? headerHeight.value : 0))

const start = computed(() => {
  if (!props.virtual) return 0
  const n = props.rows.length
  if (!n) return 0
  const index = lowerBound(offsets.value, anchor.value)
  return Math.max(0, Math.min(index, n - 1))
})

const end = computed(() => {
  if (!props.virtual) return props.rows.length
  const n = props.rows.length
  const off = offsets.value
  const limit = off[start.value] + props.height + props.overscan * layout.value.rowHeight
  let i = start.value
  while (i < n && off[i] < limit) i++
  return Math.min(n, i + 1)
})

const topPad = computed(() => (props.virtual ? offsets.value[start.value] || 0 : 0))
const bottomPad = computed(() =>
  props.virtual ? Math.max(0, totalHeight.value - (offsets.value[end.value] || 0)) : 0
)

// Первая реально видимая строка (без overscan): на ней показываем подпись группы,
// чей объединённый блок начался выше окна.
const firstVisible = computed(() => lowerBound(offsets.value, scrollTop.value))

const windowRows = computed(() => props.rows.slice(start.value, end.value))

// Ширина и границы колонки: px-число или «N%»; min/max — только если заданы.
function colWidth(index) {
  const col = props.columns[index]
  const override = props.styles.columns?.[col?.columnKey] || {}
  const width = override.width ?? col?.width
  const style = { minWidth: override.minWidth ? override.minWidth + 'px' : undefined,
                  maxWidth: override.maxWidth ? override.maxWidth + 'px' : undefined }
  if (typeof width === 'number') style.width = width + 'px'
  else if (typeof width === 'string') style.width = width
  else style.width = 'auto'
  return style
}

// Переопределения конкретной колонки (цвет/шрифт/выравнивание) — единственные inline-стили.
function cellStyle(index, rowIndex, zone) {
  const col = props.columns[index]
  const o = props.styles.columns?.[col?.columnKey]
  const rule = props.ruleStyle ? props.ruleStyle(rowIndex, col?.columnKey, zone) : null
  if (!o && !rule) return undefined
  const style = { ...(rule || {}) }
  if (!o) return style
  if (o.align) style.textAlign = o.align
  if (o.colors?.text) style.color = o.colors.text
  if (o.colors?.background) style.background = o.colors.background
  if (o.padding) style.padding = o.padding
  if (o.font?.family) style.fontFamily = o.font.family + ', system-ui, sans-serif'
  if (o.font?.size) style.fontSize = o.font.size + 'px'
  if (o.font?.weight) style.fontWeight = String(o.font.weight)
  if (o.font?.style) style.fontStyle = o.font.style
  return style
}

function isStripe(index, row) {
  return layout.value.zebra && row.kind === 'detail' && index % 2 === 1
}

function handleToggle(path) {
  if (!path) return
  if (props.onToggle) props.onToggle(path)
  else emit('toggle', path)
}

function onScroll(e) {
  scrollTop.value = e.target.scrollTop
}

function rowKey(row, i) {
  return (row.groupId || 'r') + ':' + (start.value + i)
}

// Серии одинакового groupPath внутри окна. Подпись рисуется на начале серии и,
// если серия началась выше окна, ещё раз на первой видимой строке — иначе при прокрутке
// внутри блока в тысячи строк название группы исчезает.
// строка итога уровня L входит в объединения колонок-предков (col < L)
const member = (row, col) =>
  row.kind === 'detail' || (row.kind === 'subtotal' && col < (row.groupPath?.length ?? 0) - 1)

const runs = computed(() => {
  const rows = windowRows.value
  const count = props.groupCount || 0
  const byCol = Array.from({ length: count }, () => [])
  for (let col = 0; col < count; col++) {
    let i = 0
    while (i < rows.length) {
      if (!member(rows[i], col)) {
        i++
        continue
      }
      const prefix = (rows[i].groupPath || []).slice(0, col + 1).join('\u0001')
      let j = i + 1
      while (
        j < rows.length &&
        member(rows[j], col) &&
        (rows[j].groupPath || []).slice(0, col + 1).join('\u0001') === prefix
      ) {
        j++
      }
      byCol[col].push({ start: i, end: j })
      i = j
    }
  }
  return byCol
})

const firstVisibleRel = computed(() => Math.max(0, firstVisible.value - start.value))

// Прогоны групп по всей модели (не по окну): нужны печатному слою.
const allRuns = computed(() => {
  const rows = props.rows
  const count = props.groupCount || 0
  const byCol = Array.from({ length: count }, () => [])
  for (let col = 0; col < count; col++) {
    let i = 0
    while (i < rows.length) {
      if (!member(rows[i], col)) {
        i++
        continue
      }
      const prefix = (rows[i].groupPath || []).slice(0, col + 1).join('\u0001')
      let j = i + 1
      while (
        j < rows.length &&
        member(rows[j], col) &&
        (rows[j].groupPath || []).slice(0, col + 1).join('\u0001') === prefix
      ) {
        j++
      }
      byCol[col].push({ start: i, end: j })
      i = j
    }
  }
  return byCol
})

// Высота A4 landscape за вычетом полей и повторяемой шапки — по ней режем страницы.
const PRINT_PAGE_PX = 660
const rowsPerChunk = computed(() => {
  const head = headerRows.value.length * layout.value.headerHeight
  const row = layout.value.rowHeight || 24
  return Math.max(6, Math.floor((PRINT_PAGE_PX - head) / row))
})

const printChunks = computed(() => {
  const total = props.rows.length
  const per = rowsPerChunk.value
  const chunks = []
  for (let s = 0; s < total; s += per) chunks.push({ start: s, end: Math.min(total, s + per) })
  return chunks
})

function chunkRows(chunk) {
  const out = []
  for (let i = chunk.start; i < chunk.end; i++) out.push(i)
  return out
}

// Ячейки страницы: объединение, начатое на прошлой странице, повторяется в её первой строке
// с rowspan, обрезанным по границе страницы.
function printCells(index, chunk) {
  const row = props.rows[index]
  if (!row) return []
  const cells = []
  for (let col = 0; col < props.columns.length; col++) {
    const groupCol = col < (props.groupCount || 0) && member(row, col)
    if (!groupCol) {
      cells.push({
        key: col,
        col,
        rowspan: 1,
        colspan: 1,
        continued: false,
        text: props.cellText(row, props.columns[col].field),
        inherit: row.kind === 'subtotal' && col < row.level
      })
      continue
    }
    const run = allRuns.value[col].find((r) => index >= r.start && index < r.end)
    if (!run) continue
    const from = Math.max(run.start, chunk.start)
    if (index !== from) continue
    const to = Math.min(run.end, chunk.end)
    cells.push({
      key: col,
      col,
      rowspan: to - from,
      colspan: 1,
      continued: from > run.start,
      text: props.cellText(props.rows[run.start], props.columns[col].field),
      inherit: row.kind === 'subtotal' && col < row.level
    })
  }
  return cells
}

// переменные шаблона без фиксированной высоты: печатный слой течёт по страницам
const printVars = computed(() => {
  const { height, ...rest } = rootStyle.value
  return rest
})

function cellsAt(index) {
  const row = props.rows[index]
  if (!row) return []
  const rel = index - start.value
  const cells = []

  for (let col = 0; col < props.columns.length; col++) {
    const isGroupCol = col < (props.groupCount || 0) && member(row, col)
    if (!isGroupCol) {
      cells.push({
        key: col,
        col,
        render: true,
        rowspan: 1,
        colspan: 1,
        continued: false,
        text: props.cellText(row, props.columns[col].field),
        inherit: row.kind === 'subtotal' && col < row.level,
        // строка итога: клик по её подписи разворачивает группу обратно
        toggle: row.kind === 'subtotal' && col === row.level ? row.groupPath : null,
        // переход доступен и с подытога, и с ячейки группы в детальной строке
        drillable:
          Boolean(props.drill) &&
          (row.kind === 'detail' ? col < (props.groupCount || 0) : row.kind === 'subtotal' && col === row.level),
        expanded: props.expanded.has((row.groupPath || []).slice(0, col + 1).join('\u0001'))
      })
      continue
    }

    const run = runs.value[col].find((r) => rel >= r.start && rel < r.end)
    if (!run) continue

    if (rel !== run.start) continue

    cells.push({
      key: col,
      col,
      render: true,
      rowspan: run.end - run.start,
      colspan: 1,
      continued: false,
      // ячейка группы-предка в строке итога: фон и начертание как у обычных строк
      inherit: row.kind === 'subtotal' && col < row.level,
      text: props.cellText(row, props.columns[col].field),
      // у детальной строки сворачиваем группу этого уровня, у строки итога — саму группу
      toggle: row.kind === 'detail' || row.kind === 'subtotal' ? row.groupPath.slice(0, col + 1) : null,
      drillable: Boolean(props.drill) && row.kind === 'detail',
        // направление шеврона — всегда по префиксу своей колонки: свёрнутость категории
        // не должна переключать галочку региона
        expanded: props.expanded.has((row.groupPath || []).slice(0, col + 1).join('\u0001'))
    })
  }
  return cells
}
</script>

<style scoped>
.rt {
  overflow: auto;
  background: var(--rt-b-bg, #fff);
  border: 1px solid var(--rt-border, #dfe3e8);
  border-radius: 8px;
  position: relative;
  contain: strict;
}
.rt-table {
  border-collapse: separate;
  border-spacing: 0;
  width: 100%;
  table-layout: fixed;
  background: var(--rt-b-bg, #fff);
  color: var(--rt-b-color, #1c1f23);
  font-family: var(--rt-b-font-family, system-ui);
  font-size: var(--rt-b-font-size, 13px);
  font-weight: var(--rt-b-font-weight, 400);
  font-style: var(--rt-b-font-style, normal);
  line-height: var(--rt-b-line-height, 1.4);
}
/* sticky на весь блок: иначе вторая строка заголовка (Сумма/Прибыль)
   перекрывает первую (группу «Финансы») при прокрутке */
.rt-table thead {
  position: sticky;
  top: 0;
  z-index: 3;
}
.rt-table th {
  height: var(--rt-header-height, 26px);
  box-sizing: border-box;
  background: var(--rt-h-bg, #f2f4f7);
  color: var(--rt-h-color, #1c1f23);
  font-family: var(--rt-h-font-family, system-ui);
  font-size: var(--rt-h-font-size, 13px);
  font-weight: var(--rt-h-font-weight, 600);
  font-style: var(--rt-h-font-style, normal);
  text-align: var(--rt-h-align, left);
  vertical-align: var(--rt-h-valign, middle);
  padding: var(--rt-h-padding, 2px 8px);
  white-space: nowrap;
  border-bottom: 1px solid var(--rt-border, #d8dee6);
}
.rt-table td {
  height: var(--rt-row-height, 26px);
  box-sizing: border-box;
  padding: var(--rt-b-padding, 2px 8px);
  color: var(--rt-b-color, #1c1f23);
  background: var(--rt-b-bg, #fff);
  font-family: var(--rt-b-font-family, system-ui);
  font-size: var(--rt-b-font-size, 13px);
  font-weight: var(--rt-b-font-weight, 400);
  font-style: var(--rt-b-font-style, normal);
  text-align: var(--rt-b-align, left);
  vertical-align: var(--rt-b-valign, middle);
  border-bottom: 1px solid color-mix(in srgb, var(--rt-border, #eef1f5) 45%, transparent);
  overflow: hidden;
  white-space: nowrap;
  text-overflow: ellipsis;
}
.rt-table tr.subtotal td {
  height: var(--rt-subtotal-height, 26px);
  background: var(--rt-s-bg, #f7f9fc);
  color: var(--rt-s-color, #1c1f23);
  font-family: var(--rt-s-font-family, system-ui);
  font-size: var(--rt-s-font-size, 13px);
  font-weight: var(--rt-s-font-weight, 600);
  font-style: var(--rt-s-font-style, normal);
  text-align: var(--rt-s-align, left);
  vertical-align: var(--rt-s-valign, middle);
  padding: var(--rt-s-padding, 2px 8px);
}
.rt-table tr.grandtotal td {
  height: var(--rt-grandtotal-height, 26px);
  background: var(--rt-g-bg, #e9edf2);
  color: var(--rt-g-color, #1c1f23);
  font-family: var(--rt-g-font-family, system-ui);
  font-size: var(--rt-g-font-size, 13px);
  font-weight: var(--rt-g-font-weight, 700);
  font-style: var(--rt-g-font-style, normal);
  text-align: var(--rt-g-align, left);
  vertical-align: var(--rt-g-valign, middle);
  padding: var(--rt-g-padding, 2px 8px);
  border-top: 2px solid color-mix(in srgb, var(--rt-border, #c9d3e0) 70%, transparent);
}
/* ячейки групп-предков внутри строки итога остаются обычными */
.rt-table tr.subtotal td.inherit,
.rt-table tr.grandtotal td.inherit {
  background: var(--rt-b-bg, #fff);
  color: var(--rt-b-color, #1c1f23);
  font-weight: var(--rt-b-font-weight, 400);
}
.rt-table td.num,
.rt-table th.num {
  text-align: right;
  font-variant-numeric: tabular-nums;
}
.rt-table td.ctr,
.rt-table th.ctr {
  text-align: center;
}
.rt.zebra .rt-table tr.detail.stripe td {
  background: var(--rt-stripe, #f7f9fc);
}
.rt.b-grid .rt-table th,
.rt.b-grid .rt-table td {
  border-right: 1px solid var(--rt-border, #e6eaef);
}
.rt.b-none .rt-table td {
  border-bottom: 0;
}
.rt-table tr.spacer td {
  padding: 0;
  border: 0;
  background: transparent;
}
.rt-table td.cont {
  color: color-mix(in srgb, var(--rt-b-color, #8a94a3) 55%, transparent);
  font-style: italic;
}
/* Печатный слой: на экране скрыт, в печати заменяет экранную таблицу целиком. */
.rt-print {
  display: none;
}
@page {
  size: A4 landscape;
  margin: 10mm 8mm;
}
@media print {
  .rt-screen {
    display: none !important;
  }
  .rt-print {
    display: block !important;
    overflow: visible !important;
    contain: none !important;
  }
  .rt-print .rt-table {
    width: 100%;
    margin: 0 0 4px;
  }
  .rt-print .rt-table thead {
    position: static !important;
    display: table-header-group;
  }
  .rt-print .rt-table tr {
    break-inside: avoid;
  }
}
.drill {
  color: #1f5fbf;
  text-decoration: underline dotted;
  cursor: pointer;
}
.drill::after {
  content: ' →';
  text-decoration: none;
}
.chev {
  cursor: pointer;
  color: #4f8ef7;
  user-select: none;
  margin-right: 4px;
}
</style>
