<template>
  <div class="page">
    <header class="head">
      <div>
        <h1>Сервер отчётов</h1>
        <p class="sub">
          Отчёты описываются шаблонами и хранятся в базе; данные считает СУБД, сервер только передаёт их.
        </p>
      </div>
      <div class="tools">
        <input v-model="query" type="search" placeholder="Поиск по названию или id" />
        <span class="count">{{ filtered.length }} из {{ reports.length }}</span>
      </div>
    </header>

    <p v-if="error" class="error">Не удалось получить список отчётов: {{ error }}</p>
    <p v-else-if="loading" class="muted">загрузка…</p>
    <p v-else-if="!filtered.length" class="muted">Отчётов не найдено.</p>

    <ul class="cards">
      <li v-for="r in filtered" :key="r.id" class="card" :data-report="r.id">
        <div class="card-head">
          <a :href="`report.html?report=${encodeURIComponent(r.id)}`" class="title">{{ r.title || r.id }}</a>
          <code class="id">{{ r.id }}</code>
        </div>
        <p v-if="r.description" class="desc">{{ r.description }}</p>
        <ul v-if="r.params && r.params.length" class="params">
          <li v-for="p in r.params" :key="p.name" :class="['badge', p.kind]">
            {{ p.label || p.name }}
            <span class="kind">{{ kindLabel(p) }}</span>
          </li>
        </ul>
        <a class="open" :href="`report.html?report=${encodeURIComponent(r.id)}`">Открыть →</a>
      </li>
    </ul>

    <footer class="foot">
      <a href="spike.html">Песочница рендерера</a> ·
      <a href="/api/reports">/api/reports</a> ·
      <a href="/api/health">/api/health</a>
    </footer>
  </div>
</template>

<script setup>
// Экран списка отчётов: GET /api/reports → заголовок, описание, состав параметров.
import { computed, onMounted, ref } from 'vue'
import { api } from '../api/client.js'

const reports = ref([])
const loading = ref(true)
const error = ref('')
const query = ref('')

const KIND = {
  lookup: 'справочник',
  static: 'набор',
  date: 'дата',
  datetime: 'дата/время',
  text: 'текст'
}

function kindLabel(p) {
  if (p.multiple) return 'мультивыбор'
  return KIND[p.kind] || p.kind || ''
}

const filtered = computed(() => {
  const q = query.value.trim().toLowerCase()
  if (!q) return reports.value
  return reports.value.filter(
    (r) => (r.title || '').toLowerCase().includes(q) || r.id.toLowerCase().includes(q)
  )
})

onMounted(async () => {
  try {
    reports.value = await api.reports()
  } catch (e) {
    error.value = e.message
  } finally {
    loading.value = false
  }
})
</script>

<style>
body {
  margin: 0;
  font-family: system-ui, -apple-system, 'Segoe UI', Roboto, sans-serif;
  color: #1c1f23;
  background: #f6f8fb;
}
</style>

<style scoped>
.page {
  max-width: 980px;
  margin: 0 auto;
  padding: 24px 20px 48px;
}
.head {
  display: flex;
  justify-content: space-between;
  align-items: flex-end;
  gap: 16px;
  flex-wrap: wrap;
  margin-bottom: 18px;
}
h1 {
  margin: 0 0 6px;
  font-size: 24px;
}
.sub {
  margin: 0;
  color: #5b6472;
  font-size: 13px;
  max-width: 560px;
}
.tools {
  display: flex;
  align-items: center;
  gap: 10px;
}
.tools input {
  font: inherit;
  font-size: 13px;
  padding: 6px 10px;
  border: 1px solid #cfd6de;
  border-radius: 8px;
  min-width: 240px;
  background: #fff;
}
.count {
  font-size: 12px;
  color: #7b8492;
  white-space: nowrap;
}
.cards {
  list-style: none;
  margin: 0;
  padding: 0;
  display: grid;
  gap: 12px;
}
.card {
  background: #fff;
  border: 1px solid #dfe3e8;
  border-radius: 10px;
  padding: 14px 16px;
  display: grid;
  gap: 8px;
}
.card-head {
  display: flex;
  align-items: baseline;
  gap: 10px;
  flex-wrap: wrap;
}
.title {
  font-size: 16px;
  font-weight: 600;
  color: #1a4fa0;
  text-decoration: none;
}
.title:hover {
  text-decoration: underline;
}
.id {
  font-size: 11px;
  color: #7b8492;
  background: #f2f5f9;
  border-radius: 4px;
  padding: 1px 6px;
}
.desc {
  margin: 0;
  font-size: 13px;
  color: #4a5361;
}
.params {
  list-style: none;
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
  margin: 0;
  padding: 0;
}
.badge {
  font-size: 12px;
  padding: 2px 8px;
  border-radius: 999px;
  background: #eef3fb;
  color: #26456f;
  border: 1px solid #dbe6f7;
}
.badge.lookup {
  background: #eaf5ee;
  border-color: #cfe8d8;
  color: #1f5a38;
}
.badge.static {
  background: #fdf3e6;
  border-color: #f3e0c4;
  color: #6b4a17;
}
.badge .kind {
  color: #7b8492;
  font-size: 11px;
}
.open {
  justify-self: start;
  font-size: 13px;
  color: #1a4fa0;
  text-decoration: none;
}
.foot {
  margin-top: 22px;
  font-size: 12px;
  color: #7b8492;
}
.foot a {
  color: #5b6b86;
}
.error {
  color: #8c1f1f;
  background: #fdeaea;
  border: 1px solid #f3cfcf;
  padding: 10px 12px;
  border-radius: 8px;
  font-size: 13px;
}
.muted {
  color: #7b8492;
  font-size: 13px;
}
</style>
