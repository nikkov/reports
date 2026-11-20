<template>
  <!-- Киоск (ADR-164): ?kiosk=1 убирает шапку, параметры и кнопки — табло на стене цеха -->
  <div class="page" :class="{ kiosk: kioskMode }">
    <header class="head">
      <h1>{{ template?.title || reportId }}</h1>
      <div class="head-right">
        <span class="loader" v-if="loading">загрузка…</span>
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
        <ThemeToggle />
      </div>
    </header>

    <p v-if="error" class="error"><b>{{ error.code }}</b> {{ error.message }}</p>

    <ParamsForm
      v-if="template"
      :report-id="reportId"
      :draft="draftMode"
      :model-value="paramValues"
      :params="template.params || []"
      :loading="loading"
      :search-message="search.message"
      @update:model-value="onParams"
      @labels="paramLabels = $event"
      @ready="onParamsReady"
      @submit="load"
      @search="searchInReport"
    />

    <p v-if="template && !autoLoad && !loadedOnce && !loading" class="hint no-data">
      Данные не загружены — нажмите «Обновить».
    </p>

    <template v-if="template">
      <!-- Блоки встают в 12-колоночную сетку: span задаёт ширину, ряды собираются сами (ADR-96) -->
      <div class="blocks">
      <template v-for="(block, index) in template.blocks" :key="index">
      <section
        v-if="block.type === 'text' || block.type === 'legend' || block.type === 'image' || loadedOnce"
        class="block"
        :class="{ card: block.card === true }"
        :style="{ gridColumn: `span ${spanOf(block)}` }"
      >
        <p v-if="block.type === 'text'" class="text">
          <template v-for="(part, pi) in textPartsFor(block)" :key="pi">
            <a
              v-if="part.type === 'link'"
              :href="part.href"
              target="_blank"
              rel="noopener noreferrer"
            >{{ part.text }}</a>
            <template v-else>{{ part.text }}</template>
          </template>
        </p>

        <TableView
          v-else-if="block.type === 'table'"
          :block="block"
          :rows="rowsFor(block)"
          :height="heightFor(block)"
          :styles="stylesFor(block)"
          :print-mode="printMode"
          :load-group="(pathKey, spec) => loadGroupDetails(block, pathKey, spec)"
          :search-term="search.term"
          :search-row="searchRowFor(index)"
          :search-col="searchColFor(index)"
          @drill="onDrill"
        />

        <LegendBlock v-else-if="block.type === 'legend'" :block="block" />

        <ImageBlock v-else-if="block.type === 'image'" :block="block" />

        <KpiBlock
          v-else-if="block.type === 'kpi'"
          :block="block"
          :rows="rowsFor(block)"
          :spark-rows="block.sparkDataKey ? dataByKey[block.sparkDataKey] || [] : []"
          :height="heightFor(block)"
        />

        <ChartBlock
          v-else-if="block.type === 'chart'"
          :block="block"
          :rows="rowsFor(block)"
          :height="heightFor(block)"
          :styles="stylesFor(block)"
          :drill="block.drill || null"
          @drill="onChartDrill"
        />
      </section>
      </template>
      </div>
    </template>
  </div>
</template>

<script setup>
import ThemeToggle from '../components/ThemeToggle.vue'
import { computed, nextTick, onBeforeUnmount, onMounted, reactive, ref, watch } from 'vue'
import { api, API_BASE } from '../api/client.js'
import { resolveBlockHeight, resolveBlockSpan } from '../engine/layout.js'
import { textParts } from './text.js'
import ParamsForm from '../components/ParamsForm.vue'
import TableView from './TableView.vue'
import { columnFormat, formatCell } from './format.js'
import ChartBlock from './ChartBlock.vue'
import KpiBlock from './KpiBlock.vue'
import LegendBlock from './LegendBlock.vue'
import ImageBlock from './ImageBlock.vue'
import { resolveStyles } from '../template/styles.js'
import { looksLikeDateExpr, resolveDefaultValue } from '../template/dates.js'
import { drillAllowed, resolveDrill } from '../engine/drill.js'

const params = new URLSearchParams(location.search)
const reportId = params.get('report') || 'sales-summary'
// Киоск (ADR-164): табло без шапки и органов управления; включается ссылкой ?kiosk=1
const kioskMode = params.get('kiosk') === '1' || params.get('kiosk') === 'true'
// Просмотр черновика (ADR-82): данные и справочники считаются по телу черновика,
// а не по опубликованной версии. Иначе новый отчёт (опубликован каркас) падал
// с «в шаблоне не задан datasets» и «нет source.dataset».
const draftMode = params.get('draft') === '1'

const template = ref(null)
const rows = ref([])
const paramValues = reactive({})

// --- Поиск по всему набору данных (ADR-144) ---
// Раньше искали по DOM и находили только отрисованные строки. Теперь совпадения собираются
// по модели всех табличных блоков, а таблица подсвечивает видимые ячейки и прокручивается
// к строке активного совпадения.
const search = reactive({ term: '', matches: [], index: -1, message: '' })

function escapeRegExp(value) {
  return String(value).replace(/[.*+?^${}()|[\]\\]/g, '\\$&')
}

// Текст ячейки «как на экране»: поиск должен находить «15 000,00», а не только «15000» (ADR-145)
function displayText(block, column, value) {
  if (value === undefined || value === null) return ''
  const styles = stylesFor(block) || null
  const spec = columnFormat(column, styles?.columns?.[column.field])
  if (!spec) return String(value)
  const text = formatCell(spec.format, spec.decimals, value)
  return text === null ? '' : text
}

function collectMatches(rawTerm) {
  // неразрывный пробел в числах («15 000,00») приравниваем к обычному (ADR-145)
  const term = rawTerm.replace(/[\u00A0\u202F]/g, ' ')
  const re = new RegExp(escapeRegExp(term), 'i')
  const squash = (s) => String(s).replace(/[\u00A0\u202F]/g, ' ')
  const found = []
  const blocks = (template.value && template.value.blocks) || []
  blocks.forEach((block, blockIndex) => {
    if (block.type !== 'table') return
    const columns = block.columns || []
    const rows = rowsFor(block) || []
    rows.forEach((row, rowIndex) => {
      columns.forEach((column, colIndex) => {
        const value = row ? row[column.field] : undefined
        if (value === undefined || value === null) return
        const shown = displayText(block, column, value)
        if (re.test(squash(value)) || re.test(squash(shown))) {
          found.push({ block: blockIndex, row: rowIndex, col: colIndex })
        }
      })
    })
  })
  return found
}

function searchRowFor(blockIndex) {
  const match = search.matches[search.index]
  return match && match.block === blockIndex ? match.row : -1
}

function searchColFor(blockIndex) {
  const match = search.matches[search.index]
  return match && match.block === blockIndex ? match.col : -1
}

function searchInReport(rawTerm) {
  const term = String(rawTerm || '').trim()
  if (!term) {
    search.term = ''
    search.matches = []
    search.index = -1
    search.message = ''
    return
  }
  // тот же запрос при повторном нажатии — следующее вхождение
  if (term === search.term && search.matches.length) {
    if (search.index >= search.matches.length - 1) {
      // перебор закончился: сообщаем, что дальше искать нечего, следующее нажатие начнёт сначала
      search.message = 'Строка не найдена'
      search.index = -1
      return
    }
    search.index += 1
    search.message = `${search.index + 1} из ${search.matches.length}`
    return
  }
  search.term = term
  search.matches = collectMatches(term)
  search.index = search.matches.length ? 0 : -1
  search.message = search.matches.length ? `1 из ${search.matches.length}` : 'Строка не найдена'
}
const paramLabels = ref({})
const loading = ref(false)
// Автозагрузка (ADR-89): шаблон может требовать явного нажатия «Обновить»
const autoLoad = computed(() => template.value?.autoLoad !== false)
const loadedOnce = ref(false)
const error = ref(null)
const elapsedMs = ref(0)
const status = ref('')
let controller = null

const rowCount = computed(() => rows.value.length)
const nf = new Intl.NumberFormat('ru-RU')

// Арифметика дат (ADR-92): последние вычисленные значения выражений по имени параметра.
// Нужны, чтобы отличить «пользователь не трогал» от «выбрал своё» и пересчитать * при обновлении.
const dateDefaults = {}

function isDateExprParam(p) {
  return (p?.type === 'date' || p?.type === 'datetime') && p.default !== undefined &&
    looksLikeDateExpr(p.default)
}

function defaultParams(tpl) {
  const out = {}
  for (const p of tpl.params || []) {
    const fromUrl = params.get(p.name)
    if (fromUrl !== null) out[p.name] = fromUrl
    else if (p.default !== undefined) {
      out[p.name] = resolveDefaultValue(p)
      if (isDateExprParam(p)) dateDefaults[p.name] = out[p.name]
    } else out[p.name] = ''
  }
  return out
}

// Пересчёт перед каждой загрузкой: значения, которых не касался пользователь, снова
// считаются от текущего момента («*-5h» не застывает на моменте открытия).
function refreshDateDefaults() {
  for (const p of template.value?.params || []) {
    if (!isDateExprParam(p)) continue
    const fresh = resolveDefaultValue(p)
    const cur = paramValues[p.name]
    const untouched = cur === undefined || cur === '' || cur === dateDefaults[p.name]
    if (untouched && fresh !== cur) paramValues[p.name] = fresh
    dateDefaults[p.name] = fresh
  }
}

function stopRefresh() {
  if (refreshTimer.value) {
    clearInterval(refreshTimer.value)
    refreshTimer.value = null
  }
}

function startRefresh() {
  stopRefresh()
  // при autoLoad: false обновление только по кнопке (ADR-89)
  if (!refreshSeconds.value || !autoLoad.value) return
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
  return api.group(reportId, { ...paramValues }, { ...spec, dataset: key || undefined, order, draft: draftMode })
}

function rowsFor(block) {
  const key = block?.dataKey || ''
  return dataByKey.value[key] || rows.value
}

async function load() {
  // * и смещения пересчитываются от момента загрузки, а не от момента открытия отчёта
  refreshDateDefaults()
  if (template.value && missingRequired().length) return // ждём автовыбор справочника
  if (controller) controller.abort()
  controller = new AbortController()
  loading.value = true
  error.value = null
  const started = performance.now()
  try {
    // набор нужен таблицам, диаграммам и KPI-карточкам (им — одна строка показателя)
    const blocks = (template.value?.blocks || []).filter(
      (b) => b.type === 'table' || b.type === 'chart' || b.type === 'kpi'
    )
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
        ? (await api.aggregate(reportId, { ...paramValues }, serverAgg, controller.signal, key, draftMode)).rows
        : (await api.data(reportId, { ...paramValues }, controller.signal, key, draftMode)).rows
    }
    // Строки для текстовых блоков (ADR-101): по запросу на блок, только у блоков с dataKey.
    // Запрос обязан вернуть ≤ 1 строку; больше — сервер отвечает 422 и отчёт не строится.
    const textBlocks = (template.value?.blocks || []).filter((b) => b.type === 'text' && b.dataKey)
    if (textBlocks.length) {
      const textData = {}
      for (const block of textBlocks) {
        const res = await api.text(reportId, { ...paramValues }, block.dataKey, draftMode, controller.signal)
        textData[block.dataKey] = res.row || {}
      }
      textRows.value = textData
    } else {
      textRows.value = {}
    }
    dataByKey.value = loaded
    const data = { rows: loaded[keys[0]] || [] }
    const renderStart = performance.now()
    rows.value = data.rows
    await nextTick()
    // бюджет рендера: только построение модели и отрисовка окна, без времени запроса
    window.__renderMs = Math.round(performance.now() - renderStart)
    loadedOnce.value = true
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
  window.addEventListener('resize', onResize)
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
      // localStorage пуст (другой браузер/вкладка) — берём сохранённый черновик из админки
      if (!draft) {
        try {
          draft = await api.adminDraft(reportId)
        } catch {
          draft = null
        }
      }
    }
    template.value = draft || (await api.template(reportId))
    stylesCache = new WeakMap()
    Object.assign(paramValues, defaultParams(template.value))
    if (!needsLookups(template.value) && autoLoad.value) await load()
  } catch (e) {
    error.value = { code: e.code || 'ERROR', message: e.message }
  }
})

function onParamsReady() {
  if (!autoLoad.value) return // только по кнопке «Обновить»
  if (!loading.value && !lastLoadedKey) load()
}

// Автовыбор параметра (в т.ч. из справочника) должен сразу строить отчёт: дебаунс на изменение значений.
let reloadTimer = null
let lastLoadedKey = ''
watch(
  () => ({ ...paramValues }),
  () => {
    if (!template.value) return
    // автозагрузка выключена — данные обновляет только кнопка «Обновить»
    if (!autoLoad.value) return
    // пропускаем перезагрузку, если значения не изменились с последней загрузки
    const key = JSON.stringify(paramValues)
    if (key === lastLoadedKey) return
    clearTimeout(reloadTimer)
    reloadTimer = setTimeout(() => load(), 250)
  },
  { deep: true }
)

onBeforeUnmount(() => {
  window.removeEventListener('resize', onResize)
  clearTimeout(reloadTimer)
  stopLive()
  stopRefresh()
  if (controller) controller.abort()
})


// --- блоки ---

// Первый табличный блок — источник экспорта и печати (в шаблонах он один).
const tableBlock = computed(() => (template.value?.blocks || []).find((b) => b.type === 'table'))
// ширина блока: span 1…12, старый width: 'half' — половина (ADR-96)
const spanOf = (block) => resolveBlockSpan(block)
const printMode = ref(false)

// Высота блоков «в процентах» считается от окна (ADR-85): при изменении размера
// окна блоки пересчитываются, иначе таблица осталась бы с прошлой высотой.
const viewportHeight = ref(typeof window === 'undefined' ? 0 : window.innerHeight)
function onResize() {
  viewportHeight.value = window.innerHeight
}
function heightFor(block) {
  return resolveBlockHeight(block?.type, block?.height, viewportHeight.value)
}
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
const liveMode = computed(() => template.value?.live === true && autoLoad.value)
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
  if (!template.value) return
  // В отчёте может быть несколько таблиц: каждая идёт на свой лист (ADR-97)
  const tables = (template.value.blocks || []).filter((b) => b.type === 'table')
  if (!tables.length) return
  exporting.value = true
  try {
    // ExcelJS грузится лениво: в бандл отчёта он не попадает
    const { buildWorkbook, exportFileName } = await import('./export/xlsx.js')
    const blob = await buildWorkbook({
      sheets: tables.map((block) => ({
        block,
        rows: rowsFor(block),
        styles: stylesFor(block)
      })),
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
// Подстановки перехода (ADR-43, ADR-72):
//   x / series      — концы потока sankey или категория/серия точки,
//   node            — значение кликнутого узла sankey,
//   level.<поле>    — значение уровня цепочки (регион, категория, товар),
//   param.<имя>     — параметр текущего отчёта (период и т. п.),
//   любая строка    — константа.
function onChartDrill({ spec, x, series, levels, node }) {
  if (!spec?.report) return
  // второй рубеж: тот же запрет, что в диаграмме — фильтр не должен быть шире клика
  if (!drillAllowed(spec, levels || {})) return
  const query = new URLSearchParams()
  query.set('report', spec.report)
  for (const [name, value] of resolveDrill(spec, { x, series, node, levels: levels || {}, params: paramValues })) {
    query.set(name, value)
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

// Текст блока: подписи параметров, строка из набора ({{data.<колонка>}}) и ссылки (ADR-101)
const textRows = ref({}) // dataKey → строка запроса ({} если запрос вернул 0 строк)

function textPartsFor(block) {
  return textParts(block.text, {
    labels: { ...paramValues, ...paramLabels.value },
    data: block.dataKey ? textRows.value[block.dataKey] || {} : null
  })
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
/* Совпадения поиска создаются вручную в DOM (ADR-143) */
mark.find-hit {
  background: var(--rep-warn-bg);
  color: inherit;
  padding: 0 1px;
  border-radius: 2px;
}
mark.find-hit.active {
  background: var(--rep-warn);
  outline: 2px solid var(--rep-warn);
}
.head-right {
  display: flex;
  align-items: center;
  gap: 12px;
  flex-wrap: wrap;
  /* шапка прижата вправо, переключатель темы — самая правая кнопка */
  margin-left: auto;
}
.tools {
  display: flex;
  gap: 6px;
}
.tool {
  font: inherit;
  font-size: 13px;
  padding: 4px 10px;
  border: 1px solid var(--rep-border-strong);
  border-radius: 6px;
  background: var(--rep-surface);
  cursor: pointer;
}
.tool:hover:not(:disabled) {
  background: var(--rep-bg);
}
.tool:disabled {
  opacity: 0.55;
  cursor: default;
}
.refresh {
  font-size: 12px;
  color: var(--rep-text-muted);
  white-space: nowrap;
}
.refresh.paused {
  color: var(--rep-warn);
}
.ref-time {
  color: var(--rep-text-faint);
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
  .rt {
    height: auto !important;
    overflow: visible !important;
    border: 1px solid var(--rep-text);
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
  background: var(--rep-bg);
  color: var(--rep-text);
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
  color: var(--rep-text-muted);
  font-size: 12px;
}
/* индикатор идущей загрузки: отдельный класс, чтобы .meta оставался метриками */
.loader {
  color: var(--rep-warn);
  font-size: 12px;
  font-style: italic;
}
.error {
  background: var(--rep-surface);
  border: 1px solid var(--rep-surface-alt);
  color: var(--rep-danger-strong);
  padding: 8px 12px;
  border-radius: 8px;
  font-size: 13px;
}
/* Сетка блоков: 12 колонок, ряды собираются автоматически.
   minmax(0, 1fr) не даёт широкой таблице распирать свою колонку. */
.blocks {
  display: grid;
  grid-template-columns: repeat(12, minmax(0, 1fr));
  gap: 16px;
  align-items: start; /* блоки в ряду каждый своей высоты, по верху */
}
.block {
  margin-bottom: 0;
  min-width: 0;
}
/* card: блок рисуется карточкой — общий фон, рамка и отступы (ADR-160) */
.block.card {
  box-sizing: border-box;
  padding: 10px 12px;
  background: var(--rep-surface);
  border: 1px solid var(--rep-border);
  border-radius: 8px;
}
.block.card .chart-box {
  border: 0;
  background: transparent;
  padding: 0;
}
@media print {
  .block.card {
    background: #ffffff;
  }
}
.text {
  font-size: 14px;
  margin: 6px 0 12px;
}
.chart-box {
  background: var(--rep-surface);
  border: 1px solid var(--rep-border);
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

/* Порядок важен: эти правила идут после сетки, иначе grid перебивает их по каскаду */
@media (max-width: 899px) {
  .blocks {
    grid-template-columns: minmax(0, 1fr);
  }
  .block {
    grid-column: 1 / -1 !important;
  }
}
@media print {
  .blocks {
    display: block;
  }
  .block {
    width: auto !important;
    margin-bottom: 16px;
  }
}

/* Киоск (ADR-164): табло на стене — только данные, без шапки и органов управления */
.page.kiosk {
  padding: 8px;
}
.page.kiosk .head,
.page.kiosk .params,
.page.kiosk .no-print {
  display: none !important;
}
.page.kiosk .blocks {
  gap: 10px;
}
</style>
