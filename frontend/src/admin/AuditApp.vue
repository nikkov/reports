<template>
  <div class="audit">
    <div class="audit-head">
    <h1>Журнал действий</h1>
    <p class="muted">
      Кто и когда публиковал шаблоны, менял роли, отправлял рассылку. Записи только добавляются —
      изменить или удалить их через интерфейс нельзя.
    </p>

    <div class="filters">
      <label>
        Действие
        <select v-model="action" data-filter="action" @change="reload">
          <option value="">все</option>
          <option v-for="a in actions" :key="a" :value="a">{{ a }}</option>
        </select>
      </label>
      <label>
        Уровень
        <select v-model="level" data-filter="level" @change="reload">
          <option value="">любой</option>
          <option v-for="l in levels" :key="l" :value="l">{{ l }}</option>
        </select>
      </label>
      <label>
        Пользователь
        <input v-model="actor" placeholder="admin, system…" @input="onActorInput" />
      </label>
      <label>
        С
        <input v-model="from" type="date" @change="reload" data-filter-from />
      </label>
      <label>
        по
        <input v-model="to" type="date" @change="reload" data-filter-to />
      </label>
      <button
        v-if="!isDefaultPeriod"
        type="button"
        class="tool"
        data-reset-period
        @click="resetPeriod"
      >текущий месяц</button>
      <button type="button" class="tool" @click="reload">Обновить</button>
      <a class="tool" :href="exportUrl(false)" download="audit-log.csv" data-export="csv">Выгрузить CSV</a>
      <a class="tool" :href="exportUrl(true)" download="audit-log.csv" data-export="excel">Excel</a>
      <span class="muted small">всего записей: {{ total }}</span>
    </div>

    <p v-if="error" class="err">{{ error }}</p>
    </div>

    <div class="table-wrap" v-if="items.length" data-table-wrap>
    <table class="list">
      <thead>
        <tr>
          <th>Когда</th>
          <th>Кто</th>
          <th>Уровень</th>
          <th>Действие</th>
          <th>Объект</th>
          <th>Детали</th>
          <th>Адрес</th>
        </tr>
      </thead>
      <tbody>
        <tr v-for="row in items" :key="row.id" :data-audit="row.action">
          <td class="small">{{ formatDate(row.at) }}</td>
          <td><b>{{ row.actor }}</b></td>
          <td><span class="lvl" :class="`lvl-${row.level || 'info'}`">{{ row.level || '—' }}</span></td>
          <td><code>{{ row.action }}</code></td>
          <td class="small">{{ row.objectType }} · {{ row.objectId }}</td>
          <td class="small details">{{ details(row.details) }}</td>
          <td class="small">{{ row.ip || '—' }}</td>
        </tr>
      </tbody>
    </table>
    </div>
    <p v-else class="muted">Записей нет.</p>

    <div v-if="items.length < total" class="more">
      <button type="button" class="tool" :disabled="loading" @click="loadMore">
        {{ loading ? 'Загрузка…' : `Показать ещё (${total - items.length})` }}
      </button>
    </div>
  </div>
</template>

<script setup>
import { computed, onMounted, ref } from 'vue'
import { api } from '../api/client.js'

const items = ref([])
const total = ref(0)
const action = ref('')
const level = ref('')
const levels = ['trace', 'debug', 'info', 'warning', 'error']
const actor = ref('')
// По умолчанию журнал открывается за текущий месяц: с 1-го числа по сегодня (ADR-176).
// Сервер трактует «по» включительно (at < date + 1), поэтому сегодняшние записи попадают.
function isoDate(date) {
  const pad = (n) => String(n).padStart(2, '0')
  return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())}`
}
const today = new Date()
const from = ref(isoDate(new Date(today.getFullYear(), today.getMonth(), 1)))
const to = ref(isoDate(today))
const error = ref(null)
const loading = ref(false)
const pageSize = 50

const actions = [
  'template.create',
  'template.update',
  'template.publish',
  'template.delete',
  'template.rollback',
  'user.create',
  'user.update',
  'user.delete',
  'subscription.create',
  'subscription.update',
  'subscription.delete',
  'subscription.run',
  'auth.login',
  'auth.login_failed'
]

function formatDate(value) {
  if (!value) return '—'
  const date = new Date(value)
  return Number.isNaN(date.getTime()) ? value : date.toLocaleString('ru-RU')
}

// Детали события: раньше объект context печатался как «[object Object]», и в журнале было
// нечитаемо. Теперь показываем текст события, а служебные поля (context/level/ts), которые
// дублируют колонки, не выводим (ADR-170).
function details(value) {
  if (!value || typeof value !== 'object') return value ? String(value) : '—'
  const skip = new Set(['context', 'level', 'ts', 'message'])
  const parts = []
  if (value.message) parts.push(String(value.message))
  for (const [k, v] of Object.entries(value)) {
    if (skip.has(k)) continue
    parts.push(`${k}: ${v && typeof v === 'object' ? JSON.stringify(v) : v}`)
  }
  return parts.length ? parts.join(' · ') : '—'
}

// Выгрузка идёт по тем же фильтрам, что и список (ADR-67): архив должен совпадать с тем,
// что человек видит на экране. Отдельная вкладка не нужна — обычная ссылка с download.
function exportUrl(excel) {
  return api.auditExportUrl({
    action: action.value,
    level: level.value,
    actor: actor.value.trim(),
    from: from.value,
    to: to.value,
    excel
  })
}

async function fetchPage(offset) {
  loading.value = true
  error.value = null
  try {
    const page = await api.audit({
      action: action.value,
      level: level.value,
      actor: actor.value.trim(),
      from: from.value,
      to: to.value,
      limit: pageSize,
      offset
    })
    items.value = offset ? [...items.value, ...page.items] : page.items
    total.value = page.total
  } catch (e) {
    error.value = e.message
  } finally {
    loading.value = false
  }
}

// «С» и «по» по умолчанию — текущий месяц; кнопка возвращает к нему после ручных правок
const isDefaultPeriod = computed(() => {
  const now = new Date()
  return from.value === isoDate(new Date(now.getFullYear(), now.getMonth(), 1)) &&
    to.value === isoDate(now)
})

function resetPeriod() {
  const now = new Date()
  from.value = isoDate(new Date(now.getFullYear(), now.getMonth(), 1))
  to.value = isoDate(now)
  return reload()
}

function reload() {
  return fetchPage(0)
}

function loadMore() {
  return fetchPage(items.value.length)
}

let actorTimer
function onActorInput() {
  clearTimeout(actorTimer)
  actorTimer = setTimeout(reload, 250)
}

onMounted(reload)
</script>

<style scoped>
.audit {
  max-width: 1200px;
  margin: 0 auto;
  padding: 18px 16px 16px;
  font: 14px/1.45 system-ui, sans-serif;
  color: var(--rep-text);
  /* Панель вписывается в высоту экрана: прокручивается только таблица (ADR-175) */
  height: 100%;
  display: flex;
  flex-direction: column;
  gap: 8px;
  overflow: hidden;
}
.audit-head {
  flex: 0 0 auto;
}
/* Единственная прокручиваемая область: сообщения журнала */
.table-wrap {
  flex: 1 1 auto;
  min-height: 0;
  overflow: auto;
  border: 1px solid var(--rep-border);
  background: var(--rep-surface);
}
.table-wrap .list {
  border: none;
}
/* Шапка таблицы остаётся на виду при прокрутке сообщений */
.list thead th {
  position: sticky;
  top: 0;
  background: var(--rep-bg);
  z-index: 2;
  box-shadow: inset 0 -1px 0 var(--rep-border);
}
h1 {
  font-size: 20px;
  margin: 18px 0 6px;
}
.muted {
  color: var(--rep-text-muted);
}
.small {
  font-size: 12px;
}
.err {
  color: var(--rep-danger-strong);
}
.lvl {
  font-size: 11px;
  padding: 1px 6px;
  border-radius: 8px;
  background: var(--rep-code-bg);
  color: var(--rep-accent);
}
.lvl-warning {
  background: var(--rep-surface-alt);
  color: var(--rep-warn);
}
.lvl-error {
  background: var(--rep-surface);
  color: var(--rep-danger-strong);
}
.lvl-trace,
.lvl-debug {
  background: var(--rep-accent-soft);
  color: var(--rep-accent);
}
.filters {
  display: flex;
  gap: 12px;
  align-items: end;
  margin: 14px 0 12px;
  flex-wrap: wrap;
}
.filters label {
  display: grid;
  gap: 4px;
  font-size: 13px;
}
input,
select {
  padding: 6px 8px;
  border: 1px solid var(--rep-border-input);
  border-radius: 6px;
  font: inherit;
}
.tool {
  padding: 6px 12px;
  border: 1px solid var(--rep-border-input);
  background: var(--rep-surface);
  border-radius: 6px;
  cursor: pointer;
  font-size: 13px;
}
.list {
  width: 100%;
  border-collapse: collapse;
  background: var(--rep-surface);
  border: 1px solid var(--rep-border);
}
.list th,
.list td {
  text-align: left;
  padding: 6px 10px;
  border-bottom: 1px solid var(--rep-code-bg);
  vertical-align: top;
}
.list th {
  background: var(--rep-bg);
  font-weight: 600;
  font-size: 12px;
  text-transform: uppercase;
  letter-spacing: 0.02em;
}
.details {
  color: var(--rep-accent);
}
.more {
  margin-top: 12px;
}
</style>
