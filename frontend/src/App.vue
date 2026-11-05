<template>
  <div class="page">
    <header>
      <h1>Spike S1 — свой рендерер (ADR-20): группировка, объединения, подытоги, витрина</h1>
      <p class="hint">
        Все объединения и итоговые строки выводятся из одного массива строк (ADR-1/2).
        Окно витрины и обрезка rowspan по окну — ADR-20; сторонних табличных библиотек нет.
      </p>
    </header>

    <div class="toolbar">
      <label>
        Режим:
        <select v-model="mode">
          <option v-for="(m, key) in MODES" :key="key" :value="key">{{ m.label }}</option>
        </select>
      </label>
      <label>
        Строк:
        <select v-model.number="size">
          <option :value="2000">2 000</option>
          <option :value="10000">10 000</option>
          <option :value="20000">20 000</option>
          <option :value="50000">50 000</option>
        </select>
      </label>
      <button @click="expandAll">развернуть всё</button>
      <button @click="collapseAll">свернуть всё</button>
      <span class="metric">строк: <b id="m-rows">{{ model.rows.length }}</b></span>
      <span class="metric">mergeCells: <b id="m-merges">{{ model.merges.length }}</b></span>
      <span class="metric">сборка: <b id="m-build">{{ model.buildMs.toFixed(1) }} мс</b></span>
      <span class="metric">до интерактивности: <b id="m-interactive">{{ interactiveMs }} мс</b></span>
      <span class="metric">DOM-строк: <b id="m-dom">{{ domRows }}</b></span>
      <span v-if="paginate" class="metric">
        стр. <b>{{ page + 1 }}</b> из <b>{{ view.pages }}</b> (по {{ PAGE_SIZE }})
        <button :disabled="page === 0" @click="page--">←</button>
        <button :disabled="page + 1 >= view.pages" @click="page++">→</button>
      </span>
    </div>

    <ReportTable
      :rows="view.rows"
      :columns="columns"
      :merges="useMerges ? view.merges : []"
      :group-count="2"
      :height="560"
      :expanded="expanded"
      :cell-text="cellText"
      @toggle="toggle"
    />

    <section class="chart-wrap">
      <h2>Итоги по регионам (из тех же агрегатов)</h2>
      <div ref="chartRef" class="chart"></div>
    </section>
  </div>
</template>

<script setup>
import { computed, nextTick, onMounted, ref, watch } from 'vue'
import * as echarts from 'echarts'
import { buildTableModel, computeMerges, genRows } from './spike/model.js'
import ReportTable from './components/ReportTable.vue'

const columns = [
  { field: 'region', title: 'Регион', width: 200 },
  { field: 'category', title: 'Категория', width: 190 },
  { field: 'product', title: 'Продукт', width: 150 },
  { field: 'amount', title: 'Сумма', width: 150, align: 'right' },
  { field: 'profit', title: 'Прибыль', width: 150, align: 'right' },
  { field: 'margin', title: 'Рентабельность', width: 150, align: 'right' }
]

const SEP = '\u0001'
const tStart = performance.now()

const MODES = {
  stress: {
    fields: ['region'],
    subtotalLevels: [0],
    n: 50000,
    label: 'группировка по региону (плоские детали)'
  },
  levels: {
    fields: ['region', 'category', 'product'],
    subtotalLevels: [0, 1],
    n: 20000,
    label: '3 уровня: регион → категория → продукт'
  }
}

const params = new URLSearchParams(location.search)
const useMerges = params.get('merges') !== '0'
const useVirtual = params.get('virtual') !== '0'
const mode = ref(params.get('mode') || 'stress')
const SIZES = [2000, 10000, 20000, 50000]
const requested = Number(params.get('n') || 2000)
const size = ref(SIZES.includes(requested) ? requested : 2000)

// Данные генерируются лениво: 50k строк на старте блокируют главный поток.
const cache = new Map()
function rowsFor(m, n) {
  const key = m + ':' + n
  if (!cache.has(key)) {
    const t = performance.now()
    cache.set(key, genRows(n))
    console.log('genRows', key, (performance.now() - t).toFixed(1), 'ms')
  }
  return cache.get(key)
}

const PAGE_SIZE = Math.min(20000, Math.max(50, Number(params.get('ps') || 200)))
const paginate = params.get('page') === '1'
const page = ref(0)

const expanded = ref(makeTopLevelSet(mode.value, size.value))
const interactiveMs = ref(0)
const domRows = ref(0)
const tableRef = ref(null)
const chartRef = ref(null)

let chart = null

function makeTopLevelSet(m, n) {
  const ids = new Set()
  for (const r of rowsFor(m, n)) {
    const v = r[MODES[m].fields[0]]
    ids.add(v === null || v === undefined || v === '' ? '(пусто)' : String(v))
  }
  return ids
}

const model = computed(() =>
  buildTableModel(rowsFor(mode.value, size.value), MODES[mode.value].fields, expanded.value, {
    subtotalLevels: MODES[mode.value].subtotalLevels
  })
)

const view = computed(() => {
  const all = model.value.rows
  if (!paginate) return { rows: all, merges: model.value.merges, pages: 1 }
  const start = page.value * PAGE_SIZE
  const rows = all.slice(start, start + PAGE_SIZE)
  return { rows, merges: computeMerges(rows, MODES[mode.value].fields), pages: Math.max(1, Math.ceil(all.length / PAGE_SIZE)) }
})

const nf = new Intl.NumberFormat('ru-RU')

function pct(amount, profit) {
  if (!amount) return '—'
  return (profit / amount * 100).toFixed(1).replace('.', ',') + ' %'
}

function cellText(row, field) {
  const totals = row.totals ?? { amount: 0, profit: 0 }
  if (field === 'margin') return pct(totals.amount, totals.profit)
  if (field === 'amount') return nf.format(totals.amount ?? 0)
  if (field === 'profit') return nf.format(totals.profit ?? 0)

  if (row.kind === 'grandtotal') return field === 'region' ? 'ИТОГО' : ''
  if (row.kind === 'subtotal') return row.cells[field] ?? ''
  return row.cells[field] ?? (field === 'region' || field === 'category' ? '(пусто)' : '')
}

function firstCell(row) {
  if (row.kind === 'grandtotal') return 'ИТОГО'
  if (row.kind === 'subtotal') return row.cells.region ?? ''
  return row.cells.region ?? '(пусто)'
}

function fCategory({ row }) {
  if (row.kind === 'grandtotal') return ''
  if (row.kind === 'subtotal') return row.cells.category ?? ''
  return row.cells.category ?? '(пусто)'
}

function fProduct({ row }) {
  if (row.kind !== 'detail') return ''
  return row.cells.product ?? ''
}

function fAmount({ row }) {
  if (row.kind === 'detail') return nf.format(row.totals.amount)
  if (row.kind === 'subtotal' || row.kind === 'grandtotal') return nf.format(row.totals.amount)
  return ''
}

function fProfit({ row }) {
  if (row.kind === 'detail' || row.kind === 'subtotal' || row.kind === 'grandtotal')
    return nf.format(row.totals.profit)
  return ''
}

function fMargin({ row }) {
  const { amount, profit } = row.totals ?? {}
  if (!amount) return '—'
  return (profit / amount * 100).toFixed(1).replace('.', ',') + ' %'
}

function idOf(path) {
  return path.join(SEP)
}

function isExpanded(path) {
  return expanded.value.has(idOf(path))
}

function toggle(path) {
  const id = idOf(path)
  const next = new Set(expanded.value)
  if (next.has(id)) next.delete(id)
  else next.add(id)
  expanded.value = next
}

function expandAll() {
  const ids = new Set()
  const walk = (rows) => {
    for (const r of rows) {
      if (r.kind === 'subtotal' || r.kind === 'detail') ids.add(idOf(r.groupPath))
    }
  }
  walk(model.value.rows)
  expanded.value = ids
}

function collapseAll() {
  expanded.value = new Set()
}

function countDomRows() {
  return document.querySelectorAll('.rt-table tbody tr').length
}

function renderChart() {
  if (!chart) return
  const subtotals = model.value.rows.filter((r) => r.kind === 'subtotal' && r.level === 0)
  chart.setOption(
    {
      animation: false,
      grid: { left: 70, right: 20, top: 20, bottom: 30 },
      xAxis: { type: 'category', data: subtotals.map((r) => r.cells.region ?? '(пусто)') },
      yAxis: { type: 'value' },
      tooltip: { trigger: 'axis' },
      series: [
        {
          type: 'bar',
          data: subtotals.map((r) => r.totals.amount),
          itemStyle: { color: '#4f8ef7' }
        }
      ]
    },
    true
  )
}

watch(model, () => nextTick(renderChart))

onMounted(async () => {
  domRows.value = countDomRows()
  try {
    chart = echarts.init(chartRef.value)
    renderChart()
  } catch (e) {
    console.error('echarts init failed', e)
  }
  await nextTick()
  // setTimeout, а не rAF: в фоновой вкладке rAF троттлится и метрика не выставляется
  setTimeout(() => {
    // метрика после монтирования и первого рендера таблицы
    interactiveMs.value = Math.round(performance.now() - tStart)
    domRows.value = countDomRows()
    window.__spike = {
      mergesOn: useMerges,
      virtual: useVirtual,
      paginate,
      pageRows: view.value.rows.length,
      rows: model.value.rows.length,
      merges: view.value.merges.length,
      firstRows: view.value.rows.slice(0, 3).map((r) => ({ kind: r.kind, region: r.cells.region ?? null, amount: r.totals.amount })),
      firstMerges: view.value.merges.slice(0, 4),
      buildMs: model.value.buildMs,
      interactiveMs: interactiveMs.value,
      domRows: domRows.value,
      totals: model.value.total
    }
  }, 0)
})

watch([mode, size], ([m, n]) => {
  expanded.value = makeTopLevelSet(m, n)
  page.value = 0
})
watch(model, () => {
  if (page.value >= view.value.pages) page.value = 0
})

watch(model, () => nextTick(() => {
  domRows.value = countDomRows()
  if (window.__spike) window.__spike.domRows = domRows.value
}))
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
  padding: 20px 24px 40px;
}
h1 {
  font-size: 20px;
  margin: 0 0 6px;
}
.hint {
  margin: 0 0 14px;
  color: #5b6472;
  font-size: 13px;
}
.toolbar {
  display: flex;
  flex-wrap: wrap;
  gap: 14px;
  align-items: center;
  padding: 10px 12px;
  background: #fff;
  border: 1px solid #dfe3e8;
  border-radius: 8px;
  margin-bottom: 10px;
  font-size: 13px;
}
.toolbar select,
.toolbar button {
  font: inherit;
  padding: 4px 8px;
  border: 1px solid #cfd6de;
  border-radius: 6px;
  background: #fff;
  cursor: pointer;
}
.metric b {
  font-variant-numeric: tabular-nums;
}
.grid {
  background: #fff;
}
.chev {
  display: inline-block;
  width: 16px;
  cursor: pointer;
  color: #4f8ef7;
  user-select: none;
}
.total {
  font-weight: 600;
}
.chart-wrap {
  margin-top: 18px;
  background: #fff;
  border: 1px solid #dfe3e8;
  border-radius: 8px;
  padding: 12px 16px 4px;
}
.chart-wrap h2 {
  font-size: 14px;
  margin: 0 0 6px;
  font-weight: 600;
}
.chart {
  height: 240px;
}
</style>
