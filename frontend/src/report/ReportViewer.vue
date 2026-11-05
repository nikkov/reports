<template>
  <div class="page">
    <header class="head">
      <h1>{{ template?.title || reportId }}</h1>
      <span class="meta" v-if="status">
        строк: <b>{{ rowCount }}</b> · запрос: <b>{{ elapsedMs }} мс</b> · шаблон v{{ template?.version }}
      </span>
    </header>

    <p v-if="error" class="error"><b>{{ error.code }}</b> {{ error.message }}</p>

    <ParamsForm
      v-if="template"
      :model-value="paramValues"
      :params="template.params || []"
      :loading="loading"
      @update:model-value="onParams"
      @submit="load"
    />

    <template v-if="template">
      <section
        v-for="(block, index) in template.blocks"
        :key="index"
        class="block"
        :class="block.width === 'half' ? 'half' : 'full'"
      >
        <p v-if="block.type === 'text'" class="text">{{ renderText(block.text) }}</p>

        <ReportTable
          v-else-if="block.type === 'table'"
          :rows="tableRows(block)"
          :columns="tableColumns(block)"
          :group-count="0"
          :height="420"
          :cell-text="(row, field) => cellText(block, row, field)"
        />

        <div v-else-if="block.type === 'chart'" class="chart-box">
          <h2 v-if="block.title">{{ block.title }}</h2>
          <div :ref="(el) => setChartEl(index, el)" class="chart"></div>
        </div>
      </section>
    </template>
  </div>
</template>

<script setup>
import { computed, nextTick, onBeforeUnmount, onMounted, reactive, ref, watch } from 'vue'
import * as echarts from 'echarts'
import { api } from '../api/client.js'
import ParamsForm from '../components/ParamsForm.vue'
import ReportTable from '../components/ReportTable.vue'
import { evaluate } from '../engine/expr.js'

const params = new URLSearchParams(location.search)
const reportId = params.get('report') || 'sales-summary'

const template = ref(null)
const rows = ref([])
const paramValues = reactive({})
const loading = ref(false)
const error = ref(null)
const elapsedMs = ref(0)
const status = ref('')
const charts = new Map()
let controller = null

const rowCount = computed(() => rows.value.length)
const nf = new Intl.NumberFormat('ru-RU')

function defaultParams(tpl) {
  const out = {}
  for (const p of tpl.params || []) {
    const fromUrl = params.get(p.name)
    if (fromUrl !== null) out[p.name] = fromUrl
    else if (p.default !== undefined) out[p.name] = String(p.default)
    else out[p.name] = p.type === 'date' ? '' : ''
  }
  return out
}

function onParams(next) {
  Object.assign(paramValues, next)
}

function syncUrl() {
  const q = new URLSearchParams(location.search)
  q.set('report', reportId)
  for (const [k, v] of Object.entries(paramValues)) q.set(k, v)
  history.replaceState(null, '', `${location.pathname}?${q}`)
}

async function load() {
  if (controller) controller.abort()
  controller = new AbortController()
  loading.value = true
  error.value = null
  const started = performance.now()
  try {
    const data = await api.data(reportId, { ...paramValues }, controller.signal)
    rows.value = data.rows
    elapsedMs.value = Math.round(performance.now() - started)
    status.value = 'ok'
    syncUrl()
    await nextTick()
    renderCharts()
  } catch (e) {
    if (e.name === 'AbortError') return
    error.value = { code: e.code || 'ERROR', message: e.message }
  } finally {
    loading.value = false
  }
}

onMounted(async () => {
  try {
    template.value = await api.template(reportId)
    Object.assign(paramValues, defaultParams(template.value))
    await load()
  } catch (e) {
    error.value = { code: e.code || 'ERROR', message: e.message }
  }
})

onBeforeUnmount(() => {
  for (const c of charts.values()) c.dispose()
  if (controller) controller.abort()
})

// --- блоки ---

function renderText(text) {
  return text.replace(/\{\{\s*([a-z][a-z0-9_]*)\s*\}\}/g, (_, name) => paramValues[name] ?? '')
}

function tableColumns(block) {
  return block.columns.map((c) => ({
    field: c.field || c.title,
    title: c.title,
    width: c.width || 150,
    align: c.align || (c.aggregate || c.format === 'percent' ? 'right' : 'left')
  }))
}

const AGG_NAMES = new Set(['sum', 'avg', 'min', 'max', 'count'])

// Поля, нужные для строки ИТОГО: агрегаты колонок и всё, что упомянуто в выражениях.
function fieldsForTotals(block) {
  const fields = new Set()
  for (const col of block.columns) {
    if (col.field && col.aggregate) fields.add(col.field)
    if (col.expression) {
      for (const m of col.expression.matchAll(/([a-z_][a-z0-9_]*)/gi)) {
        if (!AGG_NAMES.has(m[1])) fields.add(m[1])
      }
    }
  }
  return fields
}

function computeTotals(block) {
  const out = {}
  for (const field of fieldsForTotals(block)) {
    let sum = 0
    let count = 0
    let min = Infinity
    let max = -Infinity
    for (const r of rows.value) {
      const v = Number(r[field])
      if (!Number.isFinite(v)) continue
      sum += v
      count++
      if (v < min) min = v
      if (v > max) max = v
    }
    out[field] = { sum, count, min: count ? min : 0, max: count ? max : 0 }
  }
  return out
}

// Ф1: плоская таблица. Строка ИТОГО синтезируется здесь же (2a переносит это в движок).
function tableRows(block) {
  if (!block.grandTotal) return rows.value
  const totals = computeTotals(block)
  const totalRow = { kind: 'grandtotal', cells: {}, totals }

  const first = block.columns[0]
  totalRow.cells[first.field || first.title] = 'ИТОГО'
  for (const col of block.columns) {
    const key = col.field || col.title
    if (key !== (first.field || first.title)) totalRow.cells[key] = ''
  }
  return [...rows.value, totalRow]
}

function formatValue(col, value) {
  if (value === null || value === undefined) return '—'
  if (col.format === 'percent') {
    const n = Number(value)
    return Number.isFinite(n) ? (n * 100).toFixed(col.decimals ?? 1).replace('.', ',') + ' %' : '—'
  }
  if (col.format === 'integer' || col.format === 'number') {
    const n = Number(value)
    return Number.isFinite(n) ? nf.format(n) : String(value)
  }
  return String(value)
}

function aggFromTotals(totals, fn, name) {
  const t = totals[name]
  if (!t) return null
  if (fn === 'sum') return t.sum
  if (fn === 'count') return t.count
  if (fn === 'avg') return t.count ? t.sum / t.count : null
  if (fn === 'min') return t.min
  if (fn === 'max') return t.max
  return null
}

function cellText(block, row, field) {
  const col = block.columns.find((c) => (c.field || c.title) === field)
  if (!col) return ''

  if (col.expression) {
    let value = null
    try {
      if (row.kind === 'grandtotal') {
        value = evaluate(col.expression, { field: () => undefined, agg: (fn, name) => aggFromTotals(row.totals, fn, name) })
      } else {
        value = evaluate(col.expression, { field: (n) => row[n], agg: () => undefined })
      }
    } catch {
      value = null
    }
    if (value === null) return row.kind === 'grandtotal' ? '—' : '—'
    return formatValue(col, value)
  }

  if (row.kind === 'grandtotal') {
    if (col.aggregate && col.field) {
      const t = row.totals[col.field]
      return t ? formatValue(col, t.sum) : '—'
    }
    return row.cells[field] || '—'
  }
  return formatValue(col, row[col.field])
}

// --- диаграммы ---

function setChartEl(index, el) {
  if (el) charts.set(index, el)
}

function renderCharts() {
  for (const block of template.value.blocks) {
    if (block.type !== 'chart') continue
    const index = template.value.blocks.indexOf(block)
    const el = charts.get(index)
    if (!el) continue

    const agg = new Map()
    for (const r of rows.value) {
      const key = String(r[block.xField] ?? '(пусто)')
      const y = Number(r[block.yField] ?? 0) || 0
      agg.set(key, (agg.get(key) || 0) + y)
    }
    const entries = [...agg.entries()].slice(0, 50)

    let chart = charts.get('inst:' + index)
    if (!chart) {
      chart = echarts.init(el)
      charts.set('inst:' + index, chart)
    }
    chart.setOption(
      {
        animation: false,
        grid: { left: 80, right: 24, top: 24, bottom: 40 },
        tooltip: { trigger: 'axis' },
        xAxis: { type: 'category', data: entries.map((e) => e[0]) },
        yAxis: { type: 'value' },
        series: [{ type: block.chartType === 'pie' ? 'bar' : block.chartType, data: entries.map((e) => e[1]) }]
      },
      true
    )
    chart.resize()
  }
}

watch(paramValues, () => {
  for (const [, c] of charts) if (c && c.setOption) c.resize()
})
</script>

<style>
body {
  margin: 0;
  font-family: system-ui, -apple-system, 'Segoe UI', Roboto, sans-serif;
  background: #f5f6f8;
  color: #1c1f23;
}
.page {
  max-width: 1400px;
  margin: 0 auto;
  padding: 18px 24px 40px;
}
.head {
  display: flex;
  align-items: baseline;
  gap: 16px;
  margin-bottom: 10px;
}
.head h1 {
  font-size: 20px;
  margin: 0;
}
.meta {
  color: #5b6472;
  font-size: 12px;
}
.error {
  background: #fdecec;
  border: 1px solid #f3c2c2;
  color: #8c1f1f;
  padding: 8px 12px;
  border-radius: 8px;
  font-size: 13px;
}
.block {
  margin-bottom: 16px;
}
.block.half {
  max-width: 680px;
}
.text {
  font-size: 14px;
  margin: 6px 0 12px;
}
.chart-box {
  background: #fff;
  border: 1px solid #dfe3e8;
  border-radius: 8px;
  padding: 10px 14px 4px;
}
.chart-box h2 {
  font-size: 13px;
  margin: 0 0 6px;
  font-weight: 600;
}
.chart {
  height: 300px;
}
</style>
