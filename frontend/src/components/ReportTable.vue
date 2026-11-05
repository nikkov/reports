<template>
  <div class="rt" :style="{ height: height + 'px' }" ref="scroller" @scroll="onScroll">
    <table class="rt-table">
        <colgroup>
          <col v-for="c in columns" :key="c.field" :style="{ width: c.width ? c.width + 'px' : 'auto' }" />
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
          <tr v-for="(row, i) in windowRows" :key="rowKey(row, i)" :class="row.kind">
            <template v-for="(cell, ci) in cellsAt(start + i)" :key="cell.key">
              <td
                v-if="cell.render"
                :rowspan="cell.rowspan"
                :colspan="cell.colspan"
                :class="{ num: columns[cell.col].align === 'right', total: row.kind !== 'detail', cont: cell.continued }"
              >
                <span v-if="cell.toggle" class="chev"            @click.stop="handleToggle(cell.toggle)">{{
                  cell.expanded ? '▾' : '▸'
                }}</span>
                {{ cell.text }}
              </td>
            </template>
          </tr>
          <tr v-if="bottomPad > 0" class="spacer" :style="{ height: bottomPad + 'px' }">
            <td :colspan="columns.length"></td>
          </tr>
        </tbody>
      </table>
  </div>
</template>

<script setup>
// Свой рендерер таблицы отчёта (ADR-20): нативная <table>, объединения из модели,
// витрина по фиксированной высоте строки. Никаких сторонних табличных библиотек.
import { computed, ref } from 'vue'

const props = defineProps({
  rows: { type: Array, required: true },
  columns: { type: Array, required: true }, // [{field,title,align,width}]
  merges: { type: Array, default: () => [] }, // [{row,col,rowspan,colspan}]
  groupCount: { type: Number, default: 0 }, // колонки-группы (объединяются)
  height: { type: Number, default: 520 },
  rowHeight: { type: Number, default: 26 },
  overscan: { type: Number, default: 8 },
  expanded: { type: Set, default: () => new Set() },
  cellText: { type: Function, required: true }, // (row, colField) => string
  onToggle: { type: Function, default: null } // (groupPath) => void — надёжнее emit из вложенного шаблона
})

const emit = defineEmits(['toggle'])

const scroller = ref(null)
const scrollTop = ref(0)

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
    rows[path.length].push({ title: col.title, colspan: 1, rowspan: depth - path.length, align: col.align })
  }
  return rows
})

const topPad = computed(() => start.value * props.rowHeight)
const bottomPad = computed(() => Math.max(0, (props.rows.length - end.value) * props.rowHeight))
const visibleCount = computed(() => Math.ceil(props.height / props.rowHeight))

// Окно начинается ровно с первой видимой строки: тогда серия групп всегда начинается
// в начале окна и подпись группы не приходится дублировать (иначе съезжает разметка).
// +26 — высота sticky-заголовка: первая строка окна должна быть видна под ним,
// иначе подпись группы прячется за заголовком.
const HEADER_H = 26
const start = computed(() => {
  const raw = Math.floor((scrollTop.value + HEADER_H) / props.rowHeight)
  return Math.max(0, Math.min(raw, Math.max(0, props.rows.length - 1)))
})

// Первая реально видимая строка (без overscan): на ней показываем подпись группы,
// чей объединённый блок начался выше окна.
const firstVisible = computed(() => Math.max(0, Math.floor(scrollTop.value / props.rowHeight)))

const end = computed(() =>
  Math.min(props.rows.length, start.value + visibleCount.value + props.overscan * 2)
)

const windowRows = computed(() => props.rows.slice(start.value, end.value))

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
const runs = computed(() => {
  const rows = windowRows.value
  const count = props.groupCount || 0
  const byCol = Array.from({ length: count }, () => [])
  for (let col = 0; col < count; col++) {
    let i = 0
    while (i < rows.length) {
      if (rows[i].kind !== 'detail') {
        i++
        continue
      }
      const prefix = (rows[i].groupPath || []).slice(0, col + 1).join('\u0001')
      let j = i + 1
      while (
        j < rows.length &&
        rows[j].kind === 'detail' &&
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

function cellsAt(index) {
  const row = props.rows[index]
  if (!row) return []
  const rel = index - start.value
  const cells = []

  for (let col = 0; col < props.columns.length; col++) {
    const isGroupCol = col < (props.groupCount || 0) && row.kind === 'detail'
    if (!isGroupCol) {
      cells.push({
        key: col,
        col,
        render: true,
        rowspan: 1,
        colspan: 1,
        continued: false,
        text: props.cellText(row, props.columns[col].field),
        toggle: null,
        expanded: false
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
      text: props.cellText(row, props.columns[col].field),
      toggle: row.kind === 'detail' ? row.groupPath.slice(0, col + 1) : null,
      expanded: row.groupPath ? props.expanded.has(row.groupPath.join('\u0001')) : false
    })
  }
  return cells
}
</script>

<style scoped>
.rt {
  overflow: auto;
  background: #fff;
  border: 1px solid #dfe3e8;
  border-radius: 8px;
  position: relative;
  contain: strict;
}
.rt-table {
  border-collapse: separate;
  border-spacing: 0;
  width: 100%;
  table-layout: fixed;
  font-size: 13px;
}
.rt-table th {
  position: sticky;
  top: 0;
  z-index: 2;
  background: #f2f4f7;
  border-bottom: 1px solid #d8dee6;
  border-right: 1px solid #e6eaef;
  padding: 6px 8px;
  text-align: left;
  font-weight: 600;
  white-space: nowrap;
}
.rt-table td {
  height: 26px;
  box-sizing: border-box;
  padding: 3px 8px;
  border-bottom: 1px solid #eef1f5;
  border-right: 1px solid #f1f4f8;
  vertical-align: top;
  overflow: hidden;
  white-space: nowrap;
  text-overflow: ellipsis;
}
.rt-table td.num,
.rt-table th.num {
  text-align: right;
  font-variant-numeric: tabular-nums;
}
.rt-table tr.subtotal td,
.rt-table tr.grandtotal td {
  font-weight: 600;
  background: #f7f9fc;
}
.rt-table tr.grandtotal td {
  border-top: 2px solid #c9d3e0;
}
.rt-table tr.spacer td {
  padding: 0;
  border: 0;
}
.rt-table td.cont {
  color: #8a94a3;
  font-style: italic;
}
.chev {
  cursor: pointer;
  color: #4f8ef7;
  user-select: none;
  margin-right: 4px;
}
</style>
