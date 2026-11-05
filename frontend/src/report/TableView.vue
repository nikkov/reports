<template>
  <div v-if="isGrouped" class="tv-bar">
    <button type="button" @click="collapseAll">Свернуть всё</button>
    <button type="button" @click="expandAll">Развернуть всё</button>
    <span class="tv-info">{{ collapsed.size }} свёрнуто</span>
  </div>
  <ReportTable
    :rows="view.rows"
    :columns="columns"
    :merges="view.merges"
    :group-count="view.groupCount"
    :height="height"
    :expanded="expandedIds"
    :cell-text="cellText"
    :on-toggle="toggle"
  />
</template>

<script setup>
// Табличный блок отчёта: одна точка, где шаблон превращается в строки витрины.
import { computed, ref, watch } from 'vue'
import ReportTable from '../components/ReportTable.vue'
import { buildTableModel, evaluateRowExpression } from '../engine/group.js'
import { evaluate } from '../engine/expr.js'

const props = defineProps({
  block: { type: Object, required: true },
  rows: { type: Array, required: true },
  height: { type: Number, default: 460 }
})

const nf = new Intl.NumberFormat('ru-RU')
const collapsed = ref(new Set())

watch(() => props.rows, () => { collapsed.value = new Set() })

// Листья колонок: заголовки-группы (children) разворачиваются в path для thead,
// данные и агрегаты считаются только по листьям.
const leaves = computed(() => {
  const out = []
  const walk = (col, path) => {
    if (Array.isArray(col.children)) {
      for (const child of col.children) walk(child, path.concat(col.title))
      return
    }
    out.push({ col, headerPath: path })
  }
  for (const col of props.block.columns) walk(col, [])
  return out
})

const leafCols = computed(() => leaves.value.map((l) => l.col))

const columns = computed(() =>
  leaves.value.map((l, i) => ({
    field: String(i),
    title: l.col.title,
    headerPath: l.headerPath,
    width: l.col.width || 150,
    align: l.col.align || (l.col.aggregate || l.col.expression || l.col.format === 'percent' ? 'right' : 'left')
  }))
)

function flatView() {
  const cols = leafCols.value
  const total = {}
  for (const col of cols) {
    if (col.aggregate && col.field) {
      let sum = 0
      for (const r of props.rows) {
        const v = Number(r[col.field])
        if (Number.isFinite(v)) sum += v
      }
      total[col.field] = { sum, count: props.rows.length }
    }
  }

  // вычисляемые колонки считаются и в плоском режиме: агрегат по одной строке = её значение
  const values = props.rows.map((r) =>
    cols.map((c) => (c.expression ? evaluateRowExpression(c.expression, r) : c.field ? r[c.field] : null))
  )
  const out = values.map((v) => ({ kind: 'detail', level: -1, groupPath: [], values: v }))

  if (props.block.grandTotal !== false) {
    const totalValues = cols.map((c, i) => {
      if (i === 0) return 'ИТОГО'
      if (c.expression) {
        try {
          return evaluate(c.expression, {
            field: () => undefined,
            agg: (fn, name) => { const t = total[name]; return fn === 'sum' ? t?.sum : undefined }
          })
        } catch {
          return null
        }
      }
      return c.aggregate ? total[c.field]?.sum ?? null : null
    })
    out.push({ kind: 'grandtotal', level: -1, groupPath: [], values: totalValues })
  }
  return { rows: out, merges: [], groupCount: 0, ids: [] }
}

function groupedView() {
  const model = buildTableModel(props.rows, leafCols.value, {
    collapsed: collapsed.value.size ? collapsed.value : undefined,
    grandTotal: props.block.grandTotal !== false,
    nullLabel: props.block.nullText || '(пусто)'
  })
  const ids = new Set()
  for (const row of model.rows) {
    for (let i = 1; i <= row.groupPath.length; i++) ids.add(row.groupPath.slice(0, i).join('\u0001'))
  }
  return { rows: model.rows, merges: model.merges, groupCount: model.groupFields.length, ids }
}

const view = computed(() => (leafCols.value.some((c) => c.group) ? groupedView() : flatView()))

// Для витрины: все группы раскрыты, кроме явно свёрнутых.
const expandedIds = computed(() => {
  if (!view.value.groupCount) return new Set()
  const open = new Set(view.value.ids)
  for (const id of collapsed.value) open.delete(id)
  return open
})

const isGrouped = computed(() => leafCols.value.some((c) => c.group))

// Все идентификаторы групп считаются по исходным строкам — не зависит от текущей свёрнутости.
const allGroupIds = computed(() => {
  const fields = leafCols.value.filter((c) => c.group).map((c) => c.field)
  const norm = (v) => (v === null || v === undefined || v === '' ? '(пусто)' : String(v))
  const ids = new Set()
  for (const row of props.rows) {
    for (let d = 1; d <= fields.length; d++) {
      ids.add(fields.slice(0, d).map((f) => norm(row[f])).join('\u0001'))
    }
  }
  return ids
})

function collapseAll() {
  collapsed.value = new Set(allGroupIds.value)
}

function expandAll() {
  collapsed.value = new Set()
}

function toggle(path) {
  const id = path.join('\u0001')
  const next = new Set(collapsed.value)
  if (next.has(id)) next.delete(id)
  else next.add(id)
  collapsed.value = next
}

function formatValue(col, value) {
  if (value === null || value === undefined || value === '') return col.aggregate || col.expression ? '—' : ''
  if (col.format === 'percent') {
    const n = Number(value)
    return Number.isFinite(n) ? (n * 100).toFixed(col.decimals ?? 1).replace('.', ',') + ' %' : '—'
  }
  if (col.type === 'date' || col.format === 'date') {
    // даты из СУБД приходят как ISO-строки: печатаем в привычном виде 05.03.2026
    const m = String(value).match(/^(\d{4})-(\d{2})-(\d{2})/)
    return m ? `${m[3]}.${m[2]}.${m[1]}` : String(value)
  }
  if (col.format === 'number' || col.format === 'integer' || col.aggregate) {
    const n = Number(value)
    if (!Number.isFinite(n)) return String(value)
    // decimals из шаблона: для средних значений важно ограничить дробную часть
    if (col.decimals !== undefined) {
      return new Intl.NumberFormat('ru-RU', {
        minimumFractionDigits: col.decimals,
        maximumFractionDigits: col.decimals
      }).format(n)
    }
    return nf.format(n)
  }
  return String(value)
}

function cellText(row, key) {
  const index = Number(key)
  const col = leafCols.value[index]
  if (!col) return ''
  return formatValue(col, row.values?.[index])
}
</script>

<style scoped>
.tv-bar {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 6px;
  font-size: 12px;
  color: #5b6472;
}
.tv-bar button {
  font: inherit;
  padding: 3px 10px;
  border: 1px solid #cfd6de;
  border-radius: 6px;
  background: #fff;
  cursor: pointer;
}
.tv-info {
  font-variant-numeric: tabular-nums;
}
</style>
