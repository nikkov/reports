<template>
  <div class="rt" :style="{ height: height + 'px' }" ref="scroller" @scroll="onScroll">
    <div class="rt-canvas" :style="{ height: totalHeight + 'px' }">
      <table class="rt-table" :style="{ transform: `translateY(${offsetY}px)` }">
        <colgroup>
          <col v-for="c in columns" :key="c.field" :style="{ width: c.width ? c.width + 'px' : 'auto' }" />
        </colgroup>
        <thead>
          <tr>
            <th v-for="c in columns" :key="c.field" :class="{ num: c.align === 'right' }">
              {{ c.title }}
            </th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="(row, i) in windowRows" :key="rowKey(row, i)" :class="row.kind">
            <template v-for="(cell, ci) in cellsAt(start + i)" :key="cell.key">
              <td
                v-if="cell.render"
                :rowspan="cell.rowspan"
                :colspan="cell.colspan"
                :class="{ num: columns[cell.col].align === 'right', total: row.kind !== 'detail', cont: cell.continued }"
              >
                <span v-if="cell.toggle" class="chev" @click.stop="$emit('toggle', cell.toggle)">{{
                  cell.expanded ? '▾' : '▸'
                }}</span>
                {{ cell.text }}
              </td>
            </template>
          </tr>
        </tbody>
      </table>
    </div>
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
  cellText: { type: Function, required: true } // (row, colField) => string
})

const emit = defineEmits(['toggle'])

const scroller = ref(null)
const scrollTop = ref(0)

const totalHeight = computed(() => props.rows.length * props.rowHeight)
const visibleCount = computed(() => Math.ceil(props.height / props.rowHeight))

const start = computed(() => {
  const raw = Math.floor(scrollTop.value / props.rowHeight) - props.overscan
  return Math.max(0, Math.min(raw, Math.max(0, props.rows.length - 1)))
})

const end = computed(() =>
  Math.min(props.rows.length, start.value + visibleCount.value + props.overscan * 2)
)

const offsetY = computed(() => start.value * props.rowHeight)
const windowRows = computed(() => props.rows.slice(start.value, end.value))

function onScroll(e) {
  scrollTop.value = e.target.scrollTop
}

function rowKey(row, i) {
  return (row.groupId || 'r') + ':' + (start.value + i)
}

// Объединения обрезаются по окну: ячейка начинается в первом видимом ряду объединения,
// rowspan не выходит за окно, подпись переносится на первый видимый ряд (cont = продолжение).
function cellsAt(index) {
  const row = props.rows[index]
  if (!row) return []
  const cells = []
  for (let col = 0; col < props.columns.length; col++) {
    const merge = props.merges.find((m) => m.row === index && m.col === col)
    const covering = merge ? null : props.merges.find(
      (m) => m.col === col && m.row < index && m.row + m.rowspan > index
    )

    if (merge) {
      const clampedEnd = Math.min(merge.row + merge.rowspan, end.value)
      cells.push({
        key: col,
        col,
        render: true,
        rowspan: Math.max(1, clampedEnd - merge.row),
        colspan: merge.colspan || 1,
        continued: false,
        text: props.cellText(props.rows[merge.row], props.columns[col].field),
        toggle: row.level === 0 && (row.kind === 'detail' || row.kind === 'subtotal') ? row.groupPath : null,
        expanded: row.groupPath ? props.expanded.has(row.groupPath.join('\u0001')) : false
      })
      continue
    }

    if (covering) {
      // Первый видимый ряд продолжения: показываем подпись группы сверху окна.
      if (covering.row < start.value && index === start.value) {
        cells.push({
          key: col,
          col,
          render: true,
          rowspan: covering.row + covering.rowspan - start.value,
          colspan: covering.colspan || 1,
          continued: true,
          text: props.cellText(props.rows[covering.row], props.columns[col].field),
          toggle: null,
          expanded: false
        })
      }
      continue
    }

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
.rt-canvas {
  position: relative;
  width: 100%;
}
.rt-table {
  position: absolute;
  top: 0;
  left: 0;
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
