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
        <input v-model="query" type="search" placeholder="Поиск по названию, описанию, тегу" />
        <span class="count">{{ pageItems.length }} из {{ total }}</span>
      </div>
      <ThemeToggle />
    </header>

    <div class="catalog">
      <!-- Каталог: папки, избранное и теги (ADR-30) -->
      <aside class="side">
        <nav class="folders">
          <button
            v-for="f in folderList"
            :key="f.key"
            type="button"
            class="folder"
            :class="{ active: activeFolder === f.key }"
            @click="activeFolder = f.key"
          >
            <span class="fname">{{ f.label }}</span>
            <span class="fcnt">{{ f.count }}</span>
          </button>
        </nav>

        <div class="tags" v-if="tagList.length">
          <p class="side-title">Теги</p>
          <button
            v-for="t in tagList"
            :key="t.key"
            type="button"
            class="tag"
            :class="{ active: activeTag === t.key }"
            @click="activeTag = activeTag === t.key ? '' : t.key"
          >{{ t.key }} <span class="fcnt">{{ t.count }}</span></button>
        </div>
      </aside>

      <main>
        <p v-if="error" class="error">Не удалось получить список отчётов: {{ error }}</p>
        <p v-else-if="loading" class="muted">загрузка…</p>
        <p v-else-if="!pageItems.length" class="muted">Отчётов не найдено.</p>

        <ul class="cards">
          <li v-for="r in pageItems" :key="r.id" class="card" :data-report="r.id" :data-folder="r.folder || ''">
            <div class="card-head">
              <button
                type="button"
                class="star"
                :class="{ on: favorites.has(r.id) }"
                :title="favorites.has(r.id) ? 'Убрать из избранного' : 'В избранное'"
                @click="toggleFavorite(r.id)"
              >{{ favorites.has(r.id) ? '★' : '☆' }}</button>
              <a :href="`report.html?report=${encodeURIComponent(r.id)}`" class="title">{{ r.title || r.id }}</a>
              <code class="id">{{ r.id }}</code>
              <span v-if="r.folder" class="folder-badge">{{ r.folder }}</span>
            </div>
            <p v-if="r.description" class="desc">{{ r.description }}</p>
            <ul v-if="r.tags && r.tags.length" class="tags-row">
              <li v-for="t in r.tags" :key="t" class="tag-chip" @click="activeTag = t">{{ t }}</li>
            </ul>
            <ul v-if="r.params && r.params.length" class="params">
              <li v-for="p in r.params" :key="p.name" :class="['badge', p.kind]">
                {{ p.label || p.name }}
                <span class="kind">{{ kindLabel(p) }}</span>
              </li>
            </ul>
            <a class="open" :href="`report.html?report=${encodeURIComponent(r.id)}`">Открыть →</a>
          </li>
        </ul>
      </main>
    </div>

        <div v-if="pageItems.length < total" class="more">
          <button type="button" class="more-btn" :disabled="loadingMore" @click="loadMore">
            {{ loadingMore ? 'Загрузка…' : `Показать ещё (${total - pageItems.length})` }}
          </button>
        </div>

    <footer class="foot">
      <a href="spike.html">Песочница рендерера</a> ·
      <a :href="`${API_BASE}/api/reports`">{{ API_BASE }}/api/reports</a> ·
      <a :href="`${API_BASE}/api/health`">{{ API_BASE }}/api/health</a>
      <span v-if="authLabel" class="auth">· {{ authLabel }}</span>
    </footer>
  </div>
</template>

<script setup>
import ThemeToggle from '../components/ThemeToggle.vue'
// Каталог отчётов (ADR-30): папки, теги и избранное из шаблона + localStorage.
// Данные — GET /api/reports (title, description, folder, tags, params).
import { computed, onMounted, ref, watch } from 'vue'
import { API_BASE, api } from '../api/client.js'

const props = defineProps({
  session: { type: Object, default: () => ({}) }
})

const authLabel = computed(() => {
  const s = props.session || {}
  if (!s.auth || !s.login) return ''
  return s.mode === 'proxy' ? `вход через прокси: ${s.login} (${s.role})` : `вход: ${s.login} (${s.role})`
})

const reports = ref([])
const loading = ref(true)
const error = ref('')
const query = ref('')
const activeFolder = ref('*')
const activeTag = ref('')

const FAVORITES_KEY = 'rep-favorites'
const favorites = ref(new Set())

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

function loadFavorites() {
  try {
    const raw = JSON.parse(localStorage.getItem(FAVORITES_KEY) || '[]')
    favorites.value = new Set(Array.isArray(raw) ? raw : [])
  } catch {
    favorites.value = new Set()
  }
}

function toggleFavorite(id) {
  const next = new Set(favorites.value)
  if (next.has(id)) next.delete(id)
  else next.add(id)
  favorites.value = next
  localStorage.setItem(FAVORITES_KEY, JSON.stringify([...next]))
}

const folderList = computed(() => {
  const counts = new Map()
  for (const r of reports.value) {
    const key = r.folder || ''
    counts.set(key, (counts.get(key) || 0) + 1)
  }
  const items = [{ key: '*', label: 'Все отчёты', count: reports.value.length }]
  items.push({ key: '★', label: 'Избранное', count: reports.value.filter((r) => favorites.value.has(r.id)).length })
  for (const [key, count] of [...counts.entries()].sort((a, b) => a[0].localeCompare(b[0], 'ru'))) {
    items.push({ key: key || '__none', label: key || 'Без папки', count })
  }
  return items.filter((f) => f.key !== '★' || f.count > 0)
})

const tagList = computed(() => {
  const counts = new Map()
  for (const r of reports.value) for (const t of r.tags || []) counts.set(t, (counts.get(t) || 0) + 1)
  return [...counts.entries()]
    .sort((a, b) => b[1] - a[1] || a[0].localeCompare(b[0], 'ru'))
    .map(([key, count]) => ({ key, count }))
})

// Каталог читается страницами с сервера (ADR-41): поиск, папка и тег — параметры запроса.
const pageItems = ref([])
const total = ref(0)
const pageSize = 10
const loadingMore = ref(false)

async function loadPage({ reset = true } = {}) {
  if (reset) {
    pageItems.value = []
    total.value = 0
  }
  // избранное — локальный список: фильтруем на клиенте
  if (activeFolder.value === '★') {
    pageItems.value = reports.value.filter((r) => favorites.value.has(r.id))
    total.value = pageItems.value.length
    return
  }
  loadingMore.value = !reset
  try {
    const folder = activeFolder.value === '*' ? '' : activeFolder.value === '__none' ? '__none__' : activeFolder.value
    const page = await api.reportsPage({
      q: query.value.trim(),
      folder,
      tag: activeTag.value,
      limit: pageSize,
      offset: reset ? 0 : pageItems.value.length
    })
    pageItems.value = reset ? page.items : [...pageItems.value, ...page.items]
    total.value = page.total
  } catch (e) {
    error.value = e.message
  } finally {
    loadingMore.value = false
  }
}

function loadMore() {
  loadPage({ reset: false })
}

let queryTimer
watch(query, () => {
  clearTimeout(queryTimer)
  queryTimer = setTimeout(() => loadPage(), 250)
})
watch([activeFolder, activeTag], () => loadPage())

onMounted(async () => {
  loadFavorites()
  try {
    reports.value = await api.reports() // полный список: счётчики папок и избранное
    await loadPage()
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
  color: var(--rep-text);
  background: var(--rep-bg);
}
</style>

<style scoped>
.page {
  max-width: 1180px;
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
  color: var(--rep-text-muted);
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
  border: 1px solid var(--rep-border-strong);
  border-radius: 8px;
  min-width: 240px;
  background: var(--rep-surface);
}
.count {
  font-size: 12px;
  color: var(--rep-text-dim);
  white-space: nowrap;
}
.catalog {
  display: grid;
  grid-template-columns: 210px minmax(0, 1fr);
  gap: 18px;
  align-items: start;
}
.side {
  position: sticky;
  top: 16px;
  display: grid;
  gap: 14px;
}
.folders {
  display: grid;
  gap: 2px;
}
.folder {
  display: flex;
  justify-content: space-between;
  gap: 8px;
  font: inherit;
  font-size: 13px;
  text-align: left;
  padding: 6px 10px;
  border: 1px solid transparent;
  border-radius: 8px;
  background: none;
  color: var(--rep-accent);
  cursor: pointer;
}
.folder:hover {
  background: var(--rep-bg);
}
.folder.active {
  background: var(--rep-accent-soft);
  border-color: var(--rep-accent-soft);
  color: var(--rep-accent-hover);
  font-weight: 600;
}
.fcnt {
  color: var(--rep-text-faint);
  font-size: 11px;
}
.side-title {
  margin: 0 0 6px;
  font-size: 11px;
  text-transform: uppercase;
  letter-spacing: 0.04em;
  color: var(--rep-text-faint);
}
.tags {
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
}
.tag {
  font: inherit;
  font-size: 11px;
  padding: 2px 8px;
  border-radius: 999px;
  border: 1px solid var(--rep-accent-soft);
  background: var(--rep-surface);
  color: var(--rep-accent);
  cursor: pointer;
}
.tag.active {
  background: var(--rep-accent-hover);
  border-color: var(--rep-accent-hover);
  color: var(--rep-surface);
}
.cards {
  list-style: none;
  margin: 0;
  padding: 0;
  display: grid;
  gap: 12px;
}
.card {
  background: var(--rep-surface);
  border: 1px solid var(--rep-border);
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
.star {
  font: inherit;
  font-size: 15px;
  line-height: 1;
  border: 0;
  background: none;
  color: var(--rep-focus);
  cursor: pointer;
  padding: 0;
}
.star.on {
  color: var(--rep-warn);
}
.title {
  font-size: 16px;
  font-weight: 600;
  color: var(--rep-accent);
  text-decoration: none;
}
.title:hover {
  text-decoration: underline;
}
.id {
  font-size: 11px;
  color: var(--rep-text-dim);
  background: var(--rep-bg);
  border-radius: 4px;
  padding: 1px 6px;
}
.folder-badge {
  font-size: 11px;
  color: var(--rep-text-muted);
  background: var(--rep-bg);
  border: 1px solid var(--rep-accent-soft);
  border-radius: 999px;
  padding: 1px 8px;
}
.desc {
  margin: 0;
  font-size: 13px;
  color: var(--rep-text-muted);
}
.tags-row {
  list-style: none;
  display: flex;
  gap: 6px;
  flex-wrap: wrap;
  margin: 0;
  padding: 0;
}
.tag-chip {
  font-size: 11px;
  color: var(--rep-text-dim);
  background: var(--rep-accent-soft);
  border-radius: 4px;
  padding: 1px 6px;
  cursor: pointer;
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
  background: var(--rep-bg);
  color: var(--rep-accent);
  border: 1px solid var(--rep-accent-soft);
}
.badge.lookup {
  background: var(--rep-ok-bg);
  border-color: var(--rep-surface-alt);
  color: var(--rep-ok);
}
.badge.static {
  background: var(--rep-warn-bg);
  border-color: var(--rep-warn-bg);
  color: var(--rep-warn);
}
.badge .kind {
  color: var(--rep-text-dim);
  font-size: 11px;
}
.open {
  justify-self: start;
  font-size: 13px;
  color: var(--rep-accent);
  text-decoration: none;
}
.foot {
  margin-top: 22px;
  font-size: 12px;
  color: var(--rep-text-dim);
}
.foot a {
  color: var(--rep-text-dim);
}
.error {
  color: var(--rep-danger-strong);
  background: var(--rep-danger-bg);
  border: 1px solid var(--rep-danger-bg);
  padding: 10px 12px;
  border-radius: 8px;
  font-size: 13px;
}
.muted {
  color: var(--rep-text-dim);
  font-size: 13px;
}
@media (max-width: 720px) {
  .catalog {
    grid-template-columns: 1fr;
  }
  .side {
    position: static;
  }
}
</style>
