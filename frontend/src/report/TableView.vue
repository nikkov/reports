<template>
  <div v-if="isGrouped" class="tv-bar">
    <button type="button" @click="collapseAll">Свернуть всё</button>
    <button type="button" @click="expandAll">Развернуть всё</button>
    <span class="tv-info">{{ collapsed.size }} свёрнуто</span>
  </div>
  <p v-if="brokenExpr.size" class="tv-broken">
    Ошибка в вычисляемых колонках:
    <span v-for="[title, msg] in [...brokenExpr]" :key="title">«{{ title }}» — {{ msg }};</span>
  </p>
  <ReportTable
    v-if="styles"
    :styles="styles"
    :virtual="virtual"
    :print-mode="printMode"
    :rows="view.rows"
    :columns="columns"
    :merges="view.merges"
    :group-count="view.groupCount"
    :height="height"
    :expanded="expandedIds"
    :cell-text="cellText"
    :rule-style="ruleStyleAt"
    :on-toggle="toggle"
  />
  <ReportTable
    v-else
    :virtual="virtual"
    :print-mode="printMode"
    :rows="view.rows"
    :columns="columns"
    :merges="view.merges"
    :group-count="view.groupCount"
    :height="height"
    :expanded="expandedIds"
    :cell-text="cellText"
    :rule-style="ruleStyleAt"
    :on-toggle="toggle"
  />
</template>

<script setup>
// Табличный блок отчёта: одна точка, где шаблон превращается в строки витрины.
import { computed, ref, watch } from 'vue'
import ReportTable from '../components/ReportTable.vue'
import { buildTableModel, evaluateRowExpression } from '../engine/group.js'
import { analyzeExpression } from '../engine/expr.js'
import { evaluate } from '../engine/expr.js'

const props = defineProps({
  block: { type: Object, required: true },
  rows: { type: Array, required: true },
  height: { type: Number, default: 460 },
  // разрешённые стили шаблона (ADR-23): undefined = вид версии 1.0
  styles: { type: Object, default: null },
  // печать: весь отчёт без виртуализации (ADR-28)
  virtual: { type: Boolean, default: true },
  printMode: { type: Boolean, default: false }
})

import { columnFormat, formatCell } from './format.js'
import { ruleStyleFor } from './rules.js'
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
  leaves.value.map((l, i) => {
    const styleColumn = props.styles?.columns?.[l.col.field]
    const numeric = !!(l.col.aggregate || l.col.expression || l.col.format === 'percent' ||
      (typeof l.col.format === 'object' && l.col.format?.style === 'percent'))
    const numericAlign = props.styles?.layout?.numericAlign || 'right'
    const align =
      styleColumn?.align ||
      l.col.align ||
      (numeric && numericAlign !== 'inherit' ? numericAlign : numeric ? 'right' : 'left')
    return {
      field: String(i), // индекс: по нему cellText достаёт значение
      // ключ колонки: имя поля, а у вычисляемой колонки — заголовок (по нему её адресуют правила)
      columnKey: l.col.field || l.col.title,
      title: l.col.title,
      headerPath: l.headerPath,
      width: l.col.width || 150,
      align,
      headerAlign: styleColumn?.headerAlign || align
    }
  })
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

// Синтаксически неверное выражение раньше молча давало «—» во всех строках: помечаем колонку и ячейку.
const brokenExpr = computed(() => {
  const map = new Map()
  for (const col of leafCols.value) {
    if (!col.expression) continue
    try {
      analyzeExpression(col.expression)
    } catch (e) {
      map.set(col.title, e.message)
    }
  }
  if (map.size) console.warn('[report] ошибки в выражениях:', Object.fromEntries(map))
  return map
})

const view = computed(() => (leafCols.value.some((c) => c.group) ? groupedView() : flatView()))

// Для витрины: все группы раскрыты, кроме явно свёрнутых.
const expandedIds = computed(() => {
  if (!view.value.groupCount) return new Set()
  const open = new Set(view.value.ids)
  for (const id of collapsed.value) open.delete(id)
  return open
})

// индекс колонки по имени поля: правила адресуются именами, а витрина — индексами
const colIndexByKey = computed(() => {
  const map = {}
  leafCols.value.forEach((c, i) => {
    if (c.field) map[c.field] = i
    // вычисляемые колонки адресуются по заголовку: поля у них нет
    if (c.title && map[c.title] === undefined) map[c.title] = i
  })
  return map
})

// Стиль правила для ячейки (columnKey = null → правило scope: "row")
function ruleStyleAt(rowIndex, columnKey, zone) {
  const rules = props.block?.rules
  if (!rules?.length) return null
  // индекс приходит из витрины (view.rows), а не из исходных данных: у строк витрины есть values
  const row = view.value.rows[rowIndex]
  if (!row) return null
  const getValue = (key) => {
    const index = colIndexByKey.value[key]
    return index === undefined ? undefined : row.values?.[index]
  }
  return ruleStyleFor(rules, zone, columnKey, getValue, props.styles?.palette)
}

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
  if (col.expression && brokenExpr.value.has(col.title)) return '⚠'
  if (value === null || value === undefined || value === '') return col.aggregate || col.expression ? '—' : ''
  // формат: колонка задаёт, стиль колонки (styles.columns.<field>.format) перекрывает
  // cellText получает исходную колонку шаблона (у неё есть field), а не обогащённую колонку витрины
  const styleColumn = props.styles?.columns?.[col.field]
  const spec = columnFormat(col, styleColumn)
  if (!spec) return String(value)
  const text = formatCell(spec.format, spec.decimals, value)
  return text === null ? '—' : text
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
.tv-broken {
  margin: 0 0 6px;
  padding: 6px 10px;
  font-size: 12px;
  color: #8c1f1f;
  background: #fdeaea;
  border: 1px solid #f3cfcf;
  border-radius: 6px;
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
