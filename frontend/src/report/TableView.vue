<template>
  <div v-if="isGrouped" class="tv-bar">
    <button type="button" @click="collapseAll">Свернуть всё</button>
    <button type="button" :disabled="expandingAll" @click="expandAll">
      {{ expandingAll ? 'разворачиваю…' : 'Развернуть всё' }}
    </button>
    <span class="tv-info">{{ collapsed.size }} свёрнуто</span>
    <span v-if="loadedNote" class="tv-info">{{ loadedNote }}</span>
    <!-- детали приходят страницами: остаток добираем той же кнопкой (ADR-70) -->
    <button
      v-for="item in pendingMore"
      :key="item.id"
      type="button"
      class="tv-more"
      :disabled="loading.has(item.id)"
      @click="loadDetails(item.id, true)"
    >
      {{ loading.has(item.id) ? 'загрузка…' : `ещё ${item.left} · ${item.name}` }}
    </button>
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
    :drill="block.drill || null"
    @drill="emit('drill', $event)"
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
    :drill="block.drill || null"
    @drill="emit('drill', $event)"
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
import { computed, onMounted, ref, watch } from 'vue'
import ReportTable from '../components/ReportTable.vue'
import { buildTableModel, evaluateRowExpression } from '../engine/group.js'
import { analyzeExpression } from '../engine/expr.js'
import { evaluate } from '../engine/expr.js'

const emit = defineEmits(['drill'])

const props = defineProps({
  block: { type: Object, required: true },
  rows: { type: Array, required: true },
  height: { type: Number, default: 460 },
  // разрешённые стили шаблона (ADR-23): undefined = вид версии 1.0
  styles: { type: Object, default: null },
  // раскрытие группы серверной агрегации подгружает детали (ADR-65):
  // async (pathKey: string, spec: {field, value}) => {rows, total}
  loadGroup: { type: Function, default: null },
  // печать: весь отчёт без виртуализации (ADR-28)
  virtual: { type: Boolean, default: true },
  printMode: { type: Boolean, default: false }
})

import { columnFormat, formatCell } from './format.js'
import { ruleStyleFor } from './rules.js'
const collapsed = ref(new Set())
// Детали групп, подгруженные по требованию: путь группы → { rows, total }
// Размер страницы деталей группы: 200 строк за запрос (потолок сервера — 5000).
// Меньше 500: браузер рендерит страницу быстро, а остаток добирается кнопкой.
const PAGE_SIZE = 200
const details = ref(new Map())
const loading = ref(new Set())

const GROUP_SEP = '\u0001'

// Детали имеет смысл подгружать только там, где сервер отдал агрегат: в модели одна
// строка на группу, раскрывать нечего (ADR-37). Признак — объект aggregate у блока.
const serverAggregated = computed(() => Boolean(props.block.aggregate && typeof props.block.aggregate === 'object'))

// Ключ группы → сырые значения её групповых колонок (в модели они нормализованы
// в подписи, а серверу нужно исходное значение, включая NULL).
const rawGroupKeys = computed(() => {
  const map = new Map()
  if (!serverAggregated.value) return map
  const groupCols = leafCols.value.filter((c) => c.group)
  if (!groupCols.length) return map
  const nullLabel = props.block.nullText || '(пусто)'
  for (const row of props.rows) {
    const path = groupCols.map((c) => {
      const raw = row[c.field]
      return raw === null || raw === undefined || raw === '' ? nullLabel : String(raw)
    })
    map.set(path.join(GROUP_SEP), { path, row, cols: groupCols })
  }
  return map
})

// Группы, у которых загружены не все строки: для каждой — своя кнопка с остатком.
const pendingMore = computed(() => {
  const out = []
  for (const [id, item] of details.value) {
    const shown = item.rows?.length || 0
    const total = Number(item.total) || 0
    if (total <= shown) continue
    const entry = rawGroupKeys.value.get(id)
    const col = entry?.cols?.[entry.path.length - 1]
    // в строках группы значение её колонки пустое (ADR-63), поэтому берём ключ
    // из модели, а не текст ячейки
    const raw = col && entry?.row ? entry.row[col.field] : null
    const name = raw === null || raw === undefined || raw === '' ? '(пусто)' : String(raw)
    out.push({ id, left: total - shown, name })
  }
  return out
})

const loadedNote = computed(() => {
  let shown = 0
  let total = 0
  for (const item of details.value.values()) {
    shown += item.rows?.length || 0
    total += Number(item.total) || 0
  }
  if (!shown || total <= shown) return ''
  return `в группах показано ${shown} из ${total} строк`
})

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
    nullLabel: props.block.nullText || '(пусто)',
    details: details.value.size ? details.value : undefined
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

// Серверная агрегация: строк группы в браузере нет, пока их не подгрузили. Показывать
// группы «раскрытыми» (▾) при пустой таблице — обещание без данных: пользователь видит
// шеврон «раскрыто», а вложенных строк нет. Поэтому такие группы стартуют свёрнутыми:
// ▸ означает «раскрыть и подгрузить», что и происходит по клику.
// Отпечаток набора строк: длиной и крайними строками ловим смену данных,
// но не сбрасываем раскрытие, когда пришёл тот же набор (повторная загрузка).
let rowsSig = null
function signatureOf(rows) {
  const list = rows || []
  const edge = (row) => (row ? JSON.stringify(row) : '')
  return `${list.length}|${edge(list[0])}|${edge(list.length > 1 ? list[list.length - 1] : null)}`
}

function resetGroupState() {
  const sig = signatureOf(props.rows)
  if (sig === rowsSig) return // те же строки: раскрытые группы и кэш деталей остаются
  rowsSig = sig
  details.value = new Map()
  loading.value = new Set()
  collapsed.value = serverAggregated.value ? new Set(allGroupIds.value) : new Set()
}

watch([() => props.rows, serverAggregated], resetGroupState)
onMounted(resetGroupState)

function collapseAll() {
  collapsed.value = new Set(allGroupIds.value)
}

// «Развернуть всё» тянет группы до конца страницами, но не бесконечно: на широком
// периоде строки в группах измеряются тысячами, и открывать всё разом — верный способ
// подвесить вкладку. Потолок общий на операцию; что не влезло, видно по кнопке «ещё N».
const EXPAND_ALL_LIMIT = 20000
const expandingAll = ref(false)

async function loadAllPages(id, budget) {
  for (;;) {
    const item = details.value.get(id)
    const shown = item?.rows?.length || 0
    const total = Number(item?.total) || 0
    if (item && total <= shown) return // группа загружена полностью
    if (budget.left <= 0) return
    await loadDetails(id, Boolean(item))
    const after = details.value.get(id)?.rows?.length || 0
    budget.left -= Math.max(0, after - shown)
    if (after === shown) return // страница не пришла — не крутимся вхолостую
  }
}

async function expandAll() {
  if (serverAggregated.value && props.loadGroup) {
    expandingAll.value = true
    const budget = { left: EXPAND_ALL_LIMIT }
    try {
      for (const id of allGroupIds.value) await loadAllPages(id, budget)
    } finally {
      expandingAll.value = false
    }
  }
  collapsed.value = new Set()
}

// more = true — добрать следующую страницу уже загруженной группы (ADR-70).
// Кэш живёт в details: свёрнутая и снова раскрытая группа второй раз не запрашивается.
async function loadDetails(id, more = false) {
  if (loading.value.has(id)) return
  const loaded = details.value.get(id)
  if (loaded && !more) return
  const entry = rawGroupKeys.value.get(id)
  if (!entry || !props.loadGroup) return
  const groupCol = entry.cols[entry.path.length - 1]
  if (!groupCol) return
  loading.value = new Set(loading.value).add(id)
  try {
    const offset = more ? loaded?.rows?.length || 0 : 0
    const result = await props.loadGroup(id, {
      field: groupCol.field,
      value: entry.row[groupCol.field] ?? null,
      offset,
      limit: PAGE_SIZE
    })
    const rows = result?.rows || []
    const next = new Map(details.value)
    next.set(id, {
      rows: more ? [...(loaded?.rows || []), ...rows] : rows,
      total: result?.total ?? result?.rowCount ?? rows.length
    })
    details.value = next
  } catch (e) {
    // не молчим: без деталей группа останется свёрнутой, а текст ошибки уйдёт в консоль
    console.error('[report] не удалось загрузить детали группы', e)
  } finally {
    const rest = new Set(loading.value)
    rest.delete(id)
    loading.value = rest
  }
}

async function toggle(path) {
  const id = path.join('\u0001')
  const wasCollapsed = collapsed.value.has(id)
  // раскрываем группу серверной агрегации — сначала подгружаем её строки
  if (wasCollapsed && serverAggregated.value && props.loadGroup) await loadDetails(id)
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

.tv-more {
  font-size: 12px;
  padding: 2px 8px;
  border: 1px solid #c8d2de;
  border-radius: 5px;
  background: #fff;
  color: #1f2d3d;
  cursor: pointer;
}

.tv-more:disabled {
  color: #8a96a3;
  cursor: default;
}
</style>
