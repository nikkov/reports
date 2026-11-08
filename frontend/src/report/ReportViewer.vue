<template>
  <div class="page">
    <header class="head">
      <h1>{{ template?.title || reportId }}</h1>
      <div class="head-right">
        <span class="meta" v-if="status">
          строк: <b>{{ rowCount }}</b> · запрос: <b>{{ elapsedMs }} мс</b> · шаблон v{{ template?.version }}
        </span>
        <div class="tools no-print" v-if="refreshSeconds > 0 || liveMode">
          <span class="refresh" :class="{ paused: refreshPaused }">
            <template v-if="refreshPaused">обновление на паузе</template>
            <template v-else-if="liveMode">обновление по событию</template>
            <template v-else>обновление каждые {{ refreshSeconds }} с</template>
            <span v-if="lastLoadedAt" class="ref-time">· {{ lastLoadedLabel }}</span>
          </span>
          <button type="button" class="tool" @click="toggleRefresh">
            {{ refreshPaused ? 'Возобновить' : 'Пауза' }}
          </button>
        </div>
        <div class="tools no-print">
          <button type="button" class="tool" :disabled="!tableBlock || exporting" @click="exportExcel">
            {{ exporting ? 'Готовим файл…' : 'Excel' }}
          </button>
          <button type="button" class="tool" :disabled="!tableBlock || printing" @click="printReport">
            {{ printing ? 'Готовим печать…' : 'Печать / PDF' }}
          </button>
        </div>
      </div>
    </header>

    <p v-if="error" class="error"><b>{{ error.code }}</b> {{ error.message }}</p>

    <ParamsForm
      v-if="template"
      :report-id="reportId"
      :model-value="paramValues"
      :params="template.params || []"
      :loading="loading"
      @update:model-value="onParams"
      @labels="paramLabels = $event"
      @ready="onParamsReady"
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

        <TableView
          v-else-if="block.type === 'table'"
          :block="block"
          :rows="rowsFor(block)"
          :height="460"
          :styles="stylesFor(block)"
          :print-mode="printMode"
          :load-group="(pathKey, spec) => loadGroupDetails(block, pathKey, spec)"
          @drill="onDrill"
        />

        <ChartBlock
          v-else-if="block.type === 'chart'"
          :block="block"
          :rows="rowsFor(block)"
          :height="block.height || 320"
          :styles="stylesFor(block)"
          :drill="block.drill || null"
          @drill="onChartDrill"
        />
      </section>
    </template>
  </div>
</template>

<script setup>
import { computed, nextTick, onBeforeUnmount, onMounted, reactive, ref, watch } from 'vue'
import { API_BASE, api } from '../api/client.js'
import ParamsForm from '../components/ParamsForm.vue'
import TableView from './TableView.vue'
import ChartBlock from './ChartBlock.vue'
import { resolveStyles } from '../template/styles.js'

const params = new URLSearchParams(location.search)
const reportId = params.get('report') || 'sales-summary'

const template = ref(null)
const rows = ref([])
const paramValues = reactive({})
const paramLabels = ref({})
const loading = ref(false)
const error = ref(null)
const elapsedMs = ref(0)
const status = ref('')
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

function stopRefresh() {
  if (refreshTimer.value) {
    clearInterval(refreshTimer.value)
    refreshTimer.value = null
  }
}

function startRefresh() {
  stopRefresh()
  if (!refreshSeconds.value) return
  refreshTimer.value = setInterval(() => {
    // фон и уже идущий запрос не трогаем: иначе очередь запросов на медленных отчётах
    if (document.hidden || refreshPaused.value || loading.value) return
    load()
  }, refreshSeconds.value * 1000)
}

function toggleRefresh() {
  refreshPaused.value = !refreshPaused.value
  if (!refreshPaused.value) load()
}

function onParams(params) {
  Object.assign(paramValues, params)
}

function syncUrl() {
  const q = new URLSearchParams(location.search)
  q.set('report', reportId)
  for (const [k, v] of Object.entries(paramValues)) q.set(k, v)
  history.replaceState(null, '', `${location.pathname}?${q}`)
}

// Обязательный параметр, ещё не заполненный справочником, не должен уходить в запрос:
// иначе сервер отвечает 422, и в консоли браузера появляется ошибка.
function missingRequired() {
  return (template.value?.params || []).filter(
    (p) => p.required && String(paramValues[p.name] ?? '') === ''
  )
}

// Несколько наборов данных (ADR-36): каждый табличный блок читает свой dataKey.
const dataByKey = ref({})

// Раскрытие группы серверной агрегации (ADR-65): детали берутся тем же набором данных
// и с теми же параметрами, что и агрегат. Строк — не больше limit сервера (200).
async function loadGroupDetails(block, pathKey, spec) {
  const key = block?.dataKey || ''
  // порядок деталей задаёт шаблон (ADR-71): без него строки идут по содержимому
  const order = Array.isArray(block?.detailOrder) ? block.detailOrder : undefined
  return api.group(reportId, { ...paramValues }, { ...spec, dataset: key || undefined, order })
}

function rowsFor(block) {
  const key = block?.dataKey || ''
  return dataByKey.value[key] || rows.value
}

async function load() {
  if (template.value && missingRequired().length) return // ждём автовыбор справочника
  if (controller) controller.abort()
  controller = new AbortController()
  loading.value = true
  error.value = null
  const started = performance.now()
  try {
    // набор нужен и таблицам, и диаграммам: диаграмма берёт строки своего dataKey
    const blocks = (template.value?.blocks || []).filter((b) => b.type === 'table' || b.type === 'chart')
    const keys = [...new Set(blocks.map((b) => b.dataKey || ''))]
    if (!keys.length) keys.push('') // шаблон без блоков данных — запросим первый набор
    const loaded = {}
    for (const key of keys) {
      const block = blocks.find((b) => (b.dataKey || '') === key)
      // Группировку на сервере задаёт блок с aggregate-объектом {groupBy, measures} (ADR-37).
      // У диаграмм aggregate — строка ('sum', 'avg'…), это клиентская агрегация: такие блоки
      // получают сырые строки, иначе сервер отвечал 422 «нужен хотя бы один groupBy».
      const serverAgg = block?.aggregate && typeof block.aggregate === 'object' ? block.aggregate : null
      loaded[key] = serverAgg
        ? (await api.aggregate(reportId, { ...paramValues }, serverAgg, controller.signal, key)).rows
        : (await api.data(reportId, { ...paramValues }, controller.signal, key)).rows
    }
    dataByKey.value = loaded
    const data = { rows: loaded[keys[0]] || [] }
    const renderStart = performance.now()
    rows.value = data.rows
    await nextTick()
    // бюджет рендера: только построение модели и отрисовка окна, без времени запроса
    window.__renderMs = Math.round(performance.now() - renderStart)
    lastLoadedKey = JSON.stringify(paramValues)
    lastLoadedAt.value = new Date()
    elapsedMs.value = Math.round(performance.now() - started)
    status.value = 'ok'
    syncUrl()
    await nextTick()
  } catch (e) {
    if (e.name === 'AbortError') return
    error.value = { code: e.code || 'ERROR', message: e.message }
  } finally {
    loading.value = false
  }
}

// отчёт с параметрами-справочниками нельзя запрашивать до загрузки списков:
// обязательный параметр ещё пуст, и первый же запрос вернул бы 422
const needsLookups = (tpl) =>
  (tpl.params || []).some((p) => p.source?.kind === 'lookup' || p.source?.kind === 'static')

onMounted(async () => {
  try {
    // Черновик из админки: report.html?report=<id>&draft=1 читает тело из localStorage,
    // а не из API — так можно посмотреть несохранённые правки.
    let draft = null
    if (params.get('draft') === '1') {
      const raw = localStorage.getItem(`rep:draft:${reportId}`)
      if (raw) {
        try {
          draft = JSON.parse(raw)
        } catch {
          draft = null
        }
      }
    }
    template.value = draft || (await api.template(reportId))
    stylesCache = new WeakMap()
    Object.assign(paramValues, defaultParams(template.value))
    if (!needsLookups(template.value)) await load()
  } catch (e) {
    error.value = { code: e.code || 'ERROR', message: e.message }
  }
})

function onParamsReady() {
  if (!loading.value && !lastLoadedKey) load()
}

// Автовыбор параметра (в т.ч. из справочника) должен сразу строить отчёт: дебаунс на изменение значений.
let reloadTimer = null
let lastLoadedKey = ''
watch(
  () => ({ ...paramValues }),
  () => {
    if (!template.value) return
    // пропускаем перезагрузку, если значения не изменились с последней загрузки
    const key = JSON.stringify(paramValues)
    if (key === lastLoadedKey) return
    clearTimeout(reloadTimer)
    reloadTimer = setTimeout(() => load(), 250)
  },
  { deep: true }
)

onBeforeUnmount(() => {
  clearTimeout(reloadTimer)
  stopLive()
  stopRefresh()
  if (controller) controller.abort()
})


// --- блоки ---

// Первый табличный блок — источник экспорта и печати (в шаблонах он один).
const tableBlock = computed(() => (template.value?.blocks || []).find((b) => b.type === 'table'))
const printMode = ref(false)
const exporting = ref(false)
const printing = ref(false)
// Автообновление (ADR-39): период задаётся шаблоном, вкладка в фоне не опрашивает сервер.
const refreshTimer = ref(null)
const refreshPaused = ref(false)
const lastLoadedAt = ref(null)
const refreshSeconds = computed(() => {
  const value = Number(template.value?.refreshSeconds)
  return Number.isInteger(value) && value > 0 ? value : 0
})
const lastLoadedLabel = computed(() =>
  lastLoadedAt.value ? lastLoadedAt.value.toLocaleTimeString('ru-RU') : ''
)

// Обновление по событию (ADR-44): одно SSE-соединение на страницу, сервер сам
// присылает событие после публикации шаблона; переподключение — штатное у EventSource.
const liveMode = computed(() => template.value?.live === true)
let liveSource = null

function startLive() {
  stopLive()
  if (!liveMode.value || refreshPaused.value) return
  liveSource = new EventSource(`${API_BASE}/api/report/events?report=${encodeURIComponent(reportId)}`)
  liveSource.addEventListener('change', async () => {
    // шаблон мог измениться целиком: подтягиваем его, затем данные
    try {
      template.value = await api.template(reportId)
    } catch (e) {
      /* заголовок обновим при следующем событии */
    }
    await load()
  })
}

function stopLive() {
  if (liveSource) {
    liveSource.close()
    liveSource = null
  }
}

// первый старт, смена режима и пауза переключают поток
watch([liveMode, refreshPaused], () => startLive(), { immediate: true })

// период приходит из шаблона: меняется — перезапускаем таймер
watch(() => template.value?.refreshSeconds, () => startRefresh(), { immediate: true })
// Печать строит полный DOM: при большом объёме браузер «вешается» на секунды, поэтому лимит скромный.
const PRINT_LIMIT = 5000

async function exportExcel() {
  const block = tableBlock.value
  if (!block || !template.value) return
  exporting.value = true
  try {
    // ExcelJS грузится лениво: в бандл отчёта он не попадает
    const { buildWorkbook, exportFileName } = await import('./export/xlsx.js')
    const blob = await buildWorkbook({
      block,
      rows: rows.value,
      styles: stylesFor(block),
      params: { ...paramValues },
      labels: paramLabels.value,
      meta: { id: reportId, title: template.value.title }
    })
    const url = URL.createObjectURL(blob)
    const link = document.createElement('a')
    link.href = url
    link.download = exportFileName(reportId)
    document.body.appendChild(link)
    link.click()
    link.remove()
    URL.revokeObjectURL(url)
  } catch (e) {
    error.value = { code: 'EXPORT_FAILED', message: e.message }
  } finally {
    exporting.value = false
  }
}

async function printReport() {
  if (printing.value) return
  if (rows.value.length > PRINT_LIMIT) {
    error.value = {
      code: 'PRINT_LIMIT',
      message: `в отчёте ${rows.value.length} строк: печать рассчитана на ${PRINT_LIMIT}. Для больших отчётов используйте Excel`
    }
    return
  }
  printing.value = true
  printMode.value = true
  try {
    // Ключевой момент: window.print() снимает уже отрисованный макет. Без ожидания кадра
    // в PDF попадали диаграммы (canvas готов сразу) и пустое место вместо таблицы.
    await nextTick()
    await new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(resolve)))
    await new Promise((resolve) => setTimeout(resolve, 250)) // запас на снятие снапшота раскладки
    window.addEventListener('afterprint', () => { printMode.value = false }, { once: true })
    window.print()
  } finally {
    printing.value = false
  }
}

// Переход из подытога группы в детальный отчёт (ADR-29):
// group.<field> — значение группы из строки, param.<name> — текущий параметр отчёта.
function onDrill({ row, spec }) {
  if (!spec?.report) return
  const groupFields = (tableBlock.value?.columns || []).filter((c) => c.group).map((c) => c.field)
  const query = new URLSearchParams()
  query.set('report', spec.report)
  for (const [name, ref] of Object.entries(spec.params || {})) {
    if (typeof ref !== 'string') continue
    if (ref.startsWith('group.')) {
      const index = groupFields.indexOf(ref.slice(6))
      if (index >= 0) query.set(name, row.groupPath?.[index] ?? '')
    } else if (ref.startsWith('param.')) {
      query.set(name, paramValues[ref.slice(6)] ?? '')
    } else {
      query.set(name, ref)
    }
  }
  window.location.href = 'report.html?' + query.toString()
}

// Drill-through с диаграммы (ADR-43): ссылки в params — категория точки (`x`),
// имя серии (`series`), текущий параметр (`param.<name>`) или литерал.
function onChartDrill({ spec, x, series }) {
  if (!spec?.report) return
  const query = new URLSearchParams()
  query.set('report', spec.report)
  for (const [name, ref] of Object.entries(spec.params || {})) {
    if (typeof ref !== 'string') continue
    if (ref === 'x') query.set(name, x ?? '')
    else if (ref === 'series') query.set(name, series ?? '')
    else if (ref.startsWith('param.')) query.set(name, paramValues[ref.slice(6)] ?? '')
    else query.set(name, ref)
  }
  window.location.href = 'report.html?' + query.toString()
}

// Стили блока считаются один раз: слияние пресета, шаблона и блока не должно идти на каждый кадр.
let stylesCache = new WeakMap()
function stylesFor(block) {
  if (!template.value || !block) return null
  if (!stylesCache.has(block)) {
    stylesCache.set(block, resolveStyles(template.value, block))
  }
  return stylesCache.get(block)
}

function renderText(text) {
  // для справочников показываем подпись (NAME), иначе — само значение
  return text.replace(/\{\{\s*([a-z][a-z0-9_]*)\s*\}\}/g, (_, name) => paramLabels.value[name] || paramValues[name] || '')
}

function tableColumns(block) {
  return block.columns.map((c) => ({
    field: c.field || c.title,
    title: c.title,
    width: c.width || 150,
    align: c.align || (c.aggregate || c.format === 'percent' ? 'right' : 'left')
  }))
}

</script>

<style>
.head-right {
  display: flex;
  align-items: center;
  gap: 12px;
  flex-wrap: wrap;
}
.tools {
  display: flex;
  gap: 6px;
}
.tool {
  font: inherit;
  font-size: 13px;
  padding: 4px 10px;
  border: 1px solid #cfd6de;
  border-radius: 6px;
  background: #fff;
  cursor: pointer;
}
.tool:hover:not(:disabled) {
  background: #f2f6fd;
}
.tool:disabled {
  opacity: 0.55;
  cursor: default;
}
.refresh {
  font-size: 12px;
  color: #5b6472;
  white-space: nowrap;
}
.refresh.paused {
  color: #8a6d3b;
}
.ref-time {
  color: #8b97a6;
}

@media print {
  .no-print,
  .params,
  .tv-bar,
  .tv-broken {
    display: none !important;
  }
  .page {
    padding: 0;
    max-width: none;
  }
  .block.full,
  .block.half {
    width: 100% !important;
    break-inside: auto;
  }
  .rt {
    height: auto !important;
    overflow: visible !important;
    border: 1px solid #333;
  }
  .rt-table thead {
    position: static;
    display: table-header-group;
  }
  .rt-table tr {
    break-inside: avoid;
  }
  .chart-box {
    break-inside: avoid;
  }
}
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
