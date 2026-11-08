<template>
  <div class="audit">
    <h1>Журнал действий</h1>
    <p class="muted">
      Кто и когда публиковал шаблоны, менял роли, отправлял рассылку. Записи только добавляются —
      изменить или удалить их через интерфейс нельзя.
    </p>

    <div class="filters">
      <label>
        Действие
        <select v-model="action" @change="reload">
          <option value="">все</option>
          <option v-for="a in actions" :key="a" :value="a">{{ a }}</option>
        </select>
      </label>
      <label>
        Пользователь
        <input v-model="actor" placeholder="admin, system…" @input="onActorInput" />
      </label>
      <label>
        С
        <input v-model="from" type="date" @change="reload" />
      </label>
      <label>
        по
        <input v-model="to" type="date" @change="reload" />
      </label>
      <button type="button" class="tool" @click="reload">Обновить</button>
      <a class="tool" :href="exportUrl(false)" download="audit-log.csv" data-export="csv">Выгрузить CSV</a>
      <a class="tool" :href="exportUrl(true)" download="audit-log.csv" data-export="excel">Excel</a>
      <span class="muted small">всего записей: {{ total }}</span>
    </div>

    <p v-if="error" class="err">{{ error }}</p>

    <table class="list" v-if="items.length">
      <thead>
        <tr>
          <th>Когда</th>
          <th>Кто</th>
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
          <td><code>{{ row.action }}</code></td>
          <td class="small">{{ row.objectType }} · {{ row.objectId }}</td>
          <td class="small details">{{ details(row.details) }}</td>
          <td class="small">{{ row.ip || '—' }}</td>
        </tr>
      </tbody>
    </table>
    <p v-else class="muted">Записей нет.</p>

    <div v-if="items.length < total" class="more">
      <button type="button" class="tool" :disabled="loading" @click="loadMore">
        {{ loading ? 'Загрузка…' : `Показать ещё (${total - items.length})` }}
      </button>
    </div>
  </div>
</template>

<script setup>
import { onMounted, ref } from 'vue'
import { api } from '../api/client.js'

const items = ref([])
const total = ref(0)
const action = ref('')
const actor = ref('')
const from = ref('')
const to = ref('')
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

function details(value) {
  if (!value || !Object.keys(value).length) return '—'
  return Object.entries(value)
    .map(([k, v]) => `${k}: ${v}`)
    .join(' · ')
}

// Выгрузка идёт по тем же фильтрам, что и список (ADR-67): архив должен совпадать с тем,
// что человек видит на экране. Отдельная вкладка не нужна — обычная ссылка с download.
function exportUrl(excel) {
  return api.auditExportUrl({
    action: action.value,
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
  padding: 18px 16px 60px;
  font: 14px/1.45 system-ui, sans-serif;
  color: #1e242c;
}
h1 {
  font-size: 20px;
  margin: 0 0 6px;
}
.muted {
  color: #6b7684;
}
.small {
  font-size: 12px;
}
.err {
  color: #b3261e;
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
  border: 1px solid #c9d1da;
  border-radius: 6px;
  font: inherit;
}
.tool {
  padding: 6px 12px;
  border: 1px solid #c9d1da;
  background: #fff;
  border-radius: 6px;
  cursor: pointer;
  font-size: 13px;
}
.list {
  width: 100%;
  border-collapse: collapse;
  background: #fff;
  border: 1px solid #dfe3e8;
}
.list th,
.list td {
  text-align: left;
  padding: 6px 10px;
  border-bottom: 1px solid #eef1f4;
  vertical-align: top;
}
.list th {
  background: #f7f9fb;
  font-weight: 600;
  font-size: 12px;
  text-transform: uppercase;
  letter-spacing: 0.02em;
}
.details {
  color: #4a5561;
}
.more {
  margin-top: 12px;
}
</style>
