<template>
  <div class="shell">
    <aside class="sidebar">
      <div class="brand">
        <div>
          <h1>Шаблоны отчётов</h1>
      <a class="subs-link" href="subscriptions.html">Рассылка отчётов →</a>
      <a class="subs-link" href="users.html">Пользователи →</a>
          <p class="brand-sub">Администрирование · JSON + CodeMirror</p>
        </div>
        <button class="btn primary" @click="openCreate">Создать</button>
      </div>

      <input v-model="query" class="search" type="search" placeholder="Поиск по id или названию" />

      <p v-if="listLoading" class="muted">загрузка…</p>
      <p v-else-if="listError" class="banner err">{{ listError }}</p>
      <p v-else-if="!filtered.length" class="muted">Отчётов не найдено.</p>

      <ul class="reports">
        <li
          v-for="r in filtered"
          :key="r.id"
          class="report"
          :class="{ active: r.id === currentId }"
          :data-report="r.id"
        >
          <button class="report-main" type="button" @click="select(r.id)">
            <span class="report-title">{{ r.title || r.id }}</span>
            <span class="report-meta">
              <code>{{ r.id }}</code>
              <span>{{ r.size }} б</span>
              <span>{{ r.updated }}</span>
            </span>
          </button>
          <div class="report-actions">
            <button type="button" @click="select(r.id)">Изменить</button>
            <button type="button" @click="duplicate(r)">Дублировать</button>
            <button type="button" class="danger-text" @click="askDelete(r)">Удалить</button>
            <a :href="reportUrl(r.id)" target="_blank" rel="noopener">Открыть</a>
          </div>
        </li>
      </ul>

      <footer class="side-foot">
        <a href="index.html">← Список отчётов</a>
        <a href="/api/admin/reports">/api/admin/reports</a>
      </footer>
    </aside>

    <main class="main">
      <header class="head">
        <div class="head-title">
          <h2>{{ docTitle || currentId || 'Новый отчёт' }}</h2>
          <p class="head-meta">
            <template v-if="currentId">
              <code>{{ currentId }}</code>
              <span v-if="hasDraft" class="draft-chip">черновик{{ draftAt ? ' от ' + draftAt : '' }}</span>
              <span v-if="dirty" class="dirty">● не сохранено</span>
              <span v-else-if="!hasDraft" class="saved">опубликовано</span>
            </template>
            <template v-else>Выберите отчёт слева или создайте новый.</template>
          </p>
        </div>
        <div class="actions">
          <button class="btn" :disabled="!canEdit" @click="runValidate">Проверить</button>
          <button v-if="!currentId" class="btn primary" :disabled="!canEdit || saving" @click="save">
            {{ saving ? 'Сохранение…' : 'Создать' }}
          </button>
          <template v-else>
            <button class="btn" :disabled="!canEdit || saving" @click="saveDraftOnly">
              {{ saving ? 'Сохранение…' : 'Сохранить черновик' }}
            </button>
            <button class="btn primary" :disabled="!canEdit || publishing" @click="publishNow">
              {{ publishing ? 'Публикация…' : 'Опубликовать' }}
            </button>
            <button class="btn" :disabled="!hasDraft || publishing" @click="discardNow">Отменить черновик</button>
          </template>
          <button class="btn" :disabled="!canEdit" @click="openSaveAs">Сохранить как…</button>
          <button class="btn" :disabled="!currentId" @click="previewDraft">Предпросмотр (черновик)</button>
          <a
            class="btn link"
            :class="{ disabled: !currentId }"
            :href="currentId ? reportUrl(currentId) : undefined"
            target="_blank"
            rel="noopener"
            @click="onOpenReport"
          >Открыть отчёт</a>
          <button class="btn" :disabled="!currentId" @click="toggleHistory">
            {{ historyOpen ? 'Скрыть историю' : 'История' }}
          </button>
        </div>
      </header>

      <div class="statusbar">
        <span>Размер тела: <b>{{ byteSize }}</b> Б <span class="dim">({{ symbols }} симв.)</span></span>
        <span v-if="savedByteSize !== null" class="dim">в БД: {{ savedByteSize }} Б</span>
        <span class="spacer"></span>
        <span v-if="status" class="status" :class="statusKind">{{ status }}</span>
      </div>

      <section v-if="paramQueries.length" class="params">
        <h3>Параметры и их запросы <span class="dim">({{ paramQueries.length }})</span></h3>
        <div v-for="p in paramQueries" :key="p.name" class="param">
          <div class="param-head">
            <code class="pname">{{ p.name }}</code>
            <span class="ptitle">{{ p.label || '—' }}</span>
            <span class="tag">{{ p.type }}</span>
            <span class="tag kind">{{ p.kind }}</span>
            <span v-if="p.multiple" class="tag">мультивыбор</span>
            <span v-if="p.dependsOn" class="tag">зависит от {{ p.dependsOn }}</span>
            <span
              v-if="p.guardOk !== null"
              class="tag"
              :class="p.guardOk ? 'ok' : 'bad'"
              :title="p.guardOk ? 'SQL проходит серверную проверку' : 'SQL не пройдёт серверную проверку: только SELECT/WITH'"
            >{{ p.guardOk ? 'SELECT ok' : 'guard: не SELECT' }}</span>
          </div>
          <pre v-if="p.sql" class="psql">{{ p.sql }}</pre>
          <p v-else class="dim small">SQL не задан — параметр берёт значение из контрола ({{ p.kind }}).</p>
        </div>
      </section>

      <section v-if="errors.length || warnings.length" class="problems">
        <div v-if="errors.length" class="problem-group">
          <h3>Ошибки · {{ errors.length }}</h3>
          <ul>
            <li v-for="(e, i) in errors" :key="'e' + i">
              <button type="button" class="problem err" @click="jumpTo(e.path)">
                <code>{{ e.path }}</code><span>{{ e.message }}</span>
              </button>
            </li>
          </ul>
        </div>
        <div v-if="warnings.length" class="problem-group">
          <h3>Предупреждения · {{ warnings.length }}</h3>
          <ul>
            <li v-for="(w, i) in warnings" :key="'w' + i">
              <button type="button" class="problem warn" @click="jumpTo(w.path)">
                <code>{{ w.path }}</code><span>{{ w.message }}</span>
              </button>
            </li>
          </ul>
        </div>
      </section>

      <div ref="editorHost" class="editor" :class="{ empty: !canEdit }"></div>

      <section v-if="historyOpen" class="history">
        <div class="history-head">
          <h3>История сохранений</h3>
          <button class="btn small" type="button" :disabled="historyLoading" @click="loadHistory">
            Обновить
          </button>
        </div>
        <p v-if="historyLoading" class="muted">загрузка…</p>
        <p v-else-if="historyError" class="banner err">{{ historyError }}</p>
        <p v-else-if="!history.length" class="muted">Сохранённых версий пока нет.</p>
        <ul v-else class="snapshots">
          <li v-for="h in history" :key="h.id">
            <span class="snap-time">{{ h.saved }}</span>
            <span class="snap-title">{{ h.title || currentId }}</span>
            <span class="dim">{{ h.size }} Б</span>
            <button class="btn small" type="button" @click="rollback(h)">Откатить</button>
          </li>
        </ul>
      </section>
    </main>

    <!-- диалог создания -->
    <div v-if="modal === 'create'" class="overlay" @click.self="closeModal">
      <form class="dialog" @submit.prevent="createReport">
        <h3>Новый отчёт</h3>
        <label>
          id
          <input v-model.trim="form.id" ref="createIdInput" placeholder="sales-q1" autocomplete="off" />
        </label>
        <label>
          Название
          <input v-model.trim="form.title" placeholder="Сводная таблица продаж" autocomplete="off" />
        </label>
        <p v-if="form.error" class="banner err">{{ form.error }}</p>
        <p class="muted small">
          Будет создан минимальный шаблон с одним текстовым блоком — дальше отредактируйте JSON в редакторе.
        </p>
        <div class="dialog-actions">
          <button type="button" class="btn" @click="closeModal">Отмена</button>
          <button type="submit" class="btn primary" :disabled="form.busy">
            {{ form.busy ? 'Создание…' : 'Создать' }}
          </button>
        </div>
      </form>
    </div>

    <!-- диалог «сохранить как» -->
    <div v-if="modal === 'saveas'" class="overlay" @click.self="closeModal">
      <form class="dialog" @submit.prevent="saveAs">
        <h3>Сохранить как новый отчёт</h3>
        <label>
          Новый id
          <input v-model.trim="form.id" placeholder="my-report-copy" autocomplete="off" />
        </label>
        <label>
          Название
          <input v-model.trim="form.title" autocomplete="off" />
        </label>
        <p v-if="form.error" class="banner err">{{ form.error }}</p>
        <div class="dialog-actions">
          <button type="button" class="btn" @click="closeModal">Отмена</button>
          <button type="submit" class="btn primary" :disabled="form.busy">
            {{ form.busy ? 'Сохранение…' : 'Сохранить как' }}
          </button>
        </div>
      </form>
    </div>

    <!-- подтверждение удаления вводом id -->
    <div v-if="modal === 'delete'" class="overlay" @click.self="closeModal">
      <form class="dialog" @submit.prevent="confirmDelete">
        <h3>Удалить «{{ deleteTarget?.id }}»?</h3>
        <p class="muted small">
          Текущая версия попадёт в историю. Чтобы подтвердить, введите id отчёта:
          <code>{{ deleteTarget?.id }}</code>
        </p>
        <input v-model.trim="form.id" placeholder="id отчёта" autocomplete="off" :data-expected="deleteTarget?.id" />
        <p v-if="form.error" class="banner err">{{ form.error }}</p>
        <div class="dialog-actions">
          <button type="button" class="btn" @click="closeModal">Отмена</button>
          <button
            type="submit"
            class="btn danger"
            :disabled="form.busy || form.id !== deleteTarget?.id"
          >
            {{ form.busy ? 'Удаление…' : 'Удалить' }}
          </button>
        </div>
      </form>
    </div>
  </div>
</template>

<script setup>
// Админка шаблонов: список отчётов, редактор JSON на CodeMirror 6, проверка и история версий.
import { computed, nextTick, onBeforeUnmount, onMounted, reactive, ref } from 'vue'
import { basicSetup } from 'codemirror'
import { EditorState } from '@codemirror/state'
import { EditorView } from '@codemirror/view'
import { json, jsonParseLinter } from '@codemirror/lang-json'
import { lintGutter, linter } from '@codemirror/lint'
import { api } from '../api/client.js'
import validateTemplate from '../template/validate.js'
import migrateTemplate from '../template/migrate.js'

const ID_RE = /^[a-z][a-z0-9-]*$/

const reports = ref([])
const query = ref('')
const listLoading = ref(true)
const listError = ref('')

const currentId = ref('')
const savedTitle = ref('')
const text = ref('')
const savedText = ref('')
const savedByteSize = ref(null)

const status = ref('')
const statusKind = ref('info')
const errors = ref([])
const warnings = ref([])
const saving = ref(false)
const publishing = ref(false)
const hasDraft = ref(false)
const draftAt = ref('')

const historyOpen = ref(false)
const history = ref([])
const historyLoading = ref(false)
const historyError = ref('')

const modal = ref('')
const form = reactive({ id: '', title: '', error: '', busy: false })
const deleteTarget = ref(null)
const createIdInput = ref(null)
const editorHost = ref(null)

let view = null
let suppressUpdate = false
// Тела, которые уже загружены/сохранены в этой сессии: серверный GET отдаёт снимок
// in-memory кэша на момент старта, поэтому свежие правки надёжнее держать локально.
const bodyCache = new Map()
const bodyKey = (id) => `rep:admin:body:${id}`

function rememberBody(id, tpl) {
  bodyCache.set(id, tpl)
  try {
    localStorage.setItem(bodyKey(id), JSON.stringify(tpl))
  } catch {
    // приватный режим/переполнение — не критично, кэш в памяти уже обновлён
  }
}

function forgetBody(id) {
  bodyCache.delete(id)
  try {
    localStorage.removeItem(bodyKey(id))
  } catch {
    /* ignore */
  }
}

function readRemembered(id) {
  try {
    const raw = localStorage.getItem(bodyKey(id))
    return raw ? JSON.parse(raw) : null
  } catch {
    return null
  }
}

const filtered = computed(() => {
  const q = query.value.trim().toLowerCase()
  if (!q) return reports.value
  return reports.value.filter(
    (r) => (r.title || '').toLowerCase().includes(q) || r.id.toLowerCase().includes(q)
  )
})

const canEdit = computed(() => text.value.trim().length > 0)
const dirty = computed(() => canEdit.value && text.value !== savedText.value)
const docTitle = computed(() => {
  try {
    const t = JSON.parse(text.value)
    return typeof t?.title === 'string' ? t.title : ''
  } catch {
    return savedTitle.value
  }
})
const byteSize = computed(() => new TextEncoder().encode(text.value).length)
const symbols = computed(() => text.value.length)

function reportUrl(id) {
  return `report.html?report=${encodeURIComponent(id)}`
}

function setStatus(message, kind = 'info') {
  status.value = message
  statusKind.value = kind
}

function clearProblems() {
  errors.value = []
  warnings.value = []
}

function humanError(e) {
  switch (e?.code) {
    case 'TEMPLATE_INVALID':
      return `Шаблон не принят: ${e.message}`
    case 'REPORT_EXISTS':
      return `Отчёт уже существует: ${e.message}`
    case 'ADMIN_DISABLED':
      return 'Админка выключена на сервере: запустите сервис с REP_ADMIN=1'
    case 'REPORT_NOT_FOUND':
      return `Отчёт не найден: ${e.message}`
    case 'SNAPSHOT_NOT_FOUND':
      return 'Версия для отката не найдена (возможно, история обновилась)'
    case 'INVALID_ID':
      return `Недопустимый id: ${e.message}`
    default:
      return e?.message || 'Неизвестная ошибка'
  }
}

async function loadList() {
  listLoading.value = true
  listError.value = ''
  try {
    reports.value = await api.adminReports()
  } catch (e) {
    listError.value = humanError(e)
  } finally {
    listLoading.value = false
  }
}

function refreshCurrentRow() {
  const row = reports.value.find((r) => r.id === currentId.value)
  if (row) savedByteSize.value = row.size
}

function setDoc(value) {
  if (!view) return
  suppressUpdate = true
  view.dispatch({ changes: { from: 0, to: view.state.doc.length, insert: value } })
  suppressUpdate = false
  text.value = value
}

async function showTemplate(id, tpl) {
  currentId.value = id
  savedTitle.value = tpl.title || ''
  const pretty = JSON.stringify(tpl, null, 2)
  setDoc(pretty)
  savedText.value = pretty
  refreshCurrentRow()
  historyOpen.value = false
  clearProblems()
  setStatus('')
  rememberBody(id, tpl)

  // черновик (ADR-31): редактор открывает его, если он есть; публичный отчёт не меняется
  hasDraft.value = false
  draftAt.value = ''
  const row = reports.value.find((r) => r.id === id)
  if (row?.hasDraft) {
    hasDraft.value = true
    draftAt.value = row.draftAt || ''
    try {
      const draft = await api.adminDraft(id)
      const migratedDraft = migrateTemplate(draft)
      const body = migratedDraft.ok ? migratedDraft.doc : draft
      setDoc(JSON.stringify(body, null, 2)) // редактор синхронизируем через CodeMirror
      rememberBody(id, body)
    } catch {
      /* черновик мог быть опубликован другим пользователем — остаёмся на опубликованной версии */
    }
  }
}

async function loadTemplate(id, force = false) {
  if (!force && dirty.value && !confirm('Есть несохранённые изменения. Перейти без сохранения?')) {
    return
  }
  clearProblems()
  setStatus('')
  // версия формата (ADR-51): старый шаблон показываем уже мигрированным,
  // сохранится он тоже в актуальной версии
  const migrated = (value) => {
    const result = migrateTemplate(value)
    if (!result.ok) throw Object.assign(new Error(result.error), { code: 'VERSION' })
    if (result.steps.length) {
      setStatus(`Шаблон переведён в версию ${result.toVersion}: ${result.steps.join('; ')}`, 'warn')
    }
    return result.doc
  }
  try {
    let tpl = bodyCache.get(id)
    if (!tpl) {
      tpl = await api.adminTemplate(id)
      rememberBody(id, tpl)
    }
    showTemplate(id, migrated(tpl))
  } catch (e) {
    // Если сервер не отдаёт тело (устаревший in-memory снимок после создания/правки),
    // показываем последнюю версию, сохранённую этой админкой.
    const fallback = e?.code === 'REPORT_NOT_FOUND' ? readRemembered(id) : null
    if (fallback) {
      showTemplate(id, fallback)
      setStatus('Сервер не отдал шаблон — показана локально сохранённая версия', 'warn')
      return
    }
    setStatus(humanError(e), 'err')
    errors.value = [{ path: '(загрузка)', message: humanError(e) }]
  }
}

async function select(id) {
  if (id === currentId.value) return
  await loadTemplate(id)
}

// Параметры шаблона и их запросы: администратору нужно видеть SQL справочников,
// не разыскивая его в JSON. guard — та же проверка «только SELECT», что на сервере.
const paramQueries = computed(() => {
  try {
    const tpl = JSON.parse(text.value)
    return (tpl.params || []).map((p) => {
      const sql = p?.source?.sql || ''
      const kind = p?.source?.kind || p?.type || ''
      const normalized = sql.replace(/\s+/g, ' ').trim().toLowerCase()
      const guardOk = !sql
        ? null
        : (normalized.startsWith('select') || normalized.startsWith('with')) &&
          !/[;]|--|\/\*/.test(sql) &&
          !/\b(insert|update|delete|drop|alter|create|truncate|grant|revoke|copy|merge|call)\b/.test(normalized)
      return {
        name: p.name || '',
        label: p.label || '',
        type: p.type || '',
        kind,
        multiple: !!p.multiple,
        dependsOn: p?.source?.dependsOn || '',
        sql,
        guardOk
      }
    })
  } catch {
    return []
  }
})

function parseDraft() {
  try {
    return { value: JSON.parse(text.value) }
  } catch (e) {
    const match = /position (\d+)/.exec(String(e.message))
    let where = ''
    if (match) {
      const pos = Number(match[1])
      const before = text.value.slice(0, pos)
      const line = before.split('\n').length
      const column = pos - before.lastIndexOf('\n')
      where = ` (строка ${line}, колонка ${column})`
    }
    errors.value = [{ path: 'JSON', message: `Невалидный JSON${where}: ${e.message}` }]
    warnings.value = []
    setStatus('JSON не разбирается — исправьте синтаксис', 'err')
    return { error: true }
  }
}

async function runValidate() {
  clearProblems()
  setStatus('Проверка…')
  const parsed = parseDraft()
  if (parsed.error) return
  const tpl = parsed.value
  if (!tpl || typeof tpl !== 'object' || Array.isArray(tpl)) {
    errors.value = [{ path: '(корень)', message: 'Шаблон должен быть JSON-объектом' }]
    setStatus('Проверка не пройдена', 'err')
    return
  }

  // 1) клиентская валидация: та же схема и правила, что у сервера
  try {
    const result = validateTemplate(tpl)
    errors.value = result.errors
    warnings.value = result.warnings
  } catch (e) {
    errors.value = [{ path: '(валидатор)', message: String(e.message || e) }]
    warnings.value = []
  }

  // 2) серверная проверка: JSON Schema + SQL/семантика на бэкенде
  const reportId = currentId.value || tpl.id || 'draft'
  try {
    const res = await api.validateReport(reportId, tpl)
    if (!res?.ok) {
      // сервер отдаёт полный список замечаний с путями — показываем их как есть
      const serverErrors = (res?.errors || []).map((e) => ({ path: `(сервер) ${e.path}`, message: e.message }))
      errors.value = [
        ...errors.value,
        ...(serverErrors.length ? serverErrors : [{ path: '(сервер)', message: res?.error || 'сервер отклонил шаблон' }])
      ]
    }
    if (res?.warnings?.length) {
      warnings.value = [
        ...warnings.value,
        ...res.warnings.map((w) => ({ path: `(сервер) ${w.path}`, message: w.message }))
      ]
    }
  } catch (e) {
    errors.value = [...errors.value, { path: '(сервер)', message: humanError(e) }]
  }

  if (errors.value.length) {
    setStatus(`Проверка не пройдена: ошибок ${errors.value.length}`, 'err')
  } else if (warnings.value.length) {
    setStatus(`Проверка пройдена, предупреждений ${warnings.value.length}`, 'warn')
  } else {
    setStatus('Проверка пройдена: ошибок нет', 'ok')
  }
}

function jumpTo(path) {
  if (!view) return
  const segments = String(path || '')
    .split('.')
    .filter((s) => s && !s.startsWith('(') && !/^\d+$/.test(s))
  const needle = segments.length ? segments[segments.length - 1] : ''
  if (!needle) return

  const doc = view.state.doc
  let targetLine = 0
  for (let i = 1; i <= doc.lines && !targetLine; i++) {
    if (doc.line(i).text.includes(`"${needle}"`)) targetLine = i
  }
  // путь может указывать на элемент массива — тогда ищем хотя бы сам сегмент
  if (!targetLine) {
    for (let i = 1; i <= doc.lines && !targetLine; i++) {
      if (doc.line(i).text.includes(needle)) targetLine = i
    }
  }
  if (!targetLine) {
    setStatus(`Не удалось найти «${needle}» в тексте`, 'info')
    return
  }
  const line = doc.line(targetLine)
  view.dispatch({ selection: { anchor: line.from, head: line.to }, scrollIntoView: true })
  view.focus()
}

// Сохранить правку как черновик: публичный отчёт не меняется до публикации.
async function saveDraftOnly() {
  if (!canEdit.value || saving.value) return
  clearProblems()
  const parsed = parseDraft()
  if (parsed.error) return
  const body = parsed.value
  if (!body || typeof body !== 'object' || Array.isArray(body)) {
    errors.value = [{ path: '(корень)', message: 'Шаблон должен быть JSON-объектом' }]
    return
  }
  body.id = currentId.value
  saving.value = true
  setStatus('Сохранение черновика…')
  try {
    await api.saveDraft(currentId.value, body)
    hasDraft.value = true
    draftAt.value = new Date().toLocaleString('ru-RU', { day: '2-digit', month: '2-digit', hour: '2-digit', minute: '2-digit' })
    await loadList()
    setStatus('черновик сохранён — отчёт пока не изменился')
  } catch (e) {
    setStatus('не сохранено: ' + e.message, 'err')
  } finally {
    saving.value = false
  }
}

async function publishNow() {
  if (!canEdit.value || publishing.value) return
  publishing.value = true
  setStatus('Публикация…')
  try {
    await api.publishDraft(currentId.value)
    const published = await api.adminTemplate(currentId.value)
    setDoc(JSON.stringify(published, null, 2))
    savedText.value = text.value
    hasDraft.value = false
    draftAt.value = ''
    await loadList()
    rememberBody(currentId.value, published)
    setStatus('опубликовано')
  } catch (e) {
    setStatus('не опубликовано: ' + e.message, 'err')
  } finally {
    publishing.value = false
  }
}

async function discardNow() {
  if (!hasDraft.value) return
  try {
    await api.discardDraft(currentId.value)
    const published = await api.adminTemplate(currentId.value)
    setDoc(JSON.stringify(published, null, 2))
    savedText.value = text.value
    hasDraft.value = false
    draftAt.value = ''
    await loadList()
    setStatus('черновик отменён')
  } catch (e) {
    setStatus('не удалось отменить: ' + e.message, 'err')
  }
}

async function save() {
  if (!canEdit.value || saving.value) return
  clearProblems()
  const parsed = parseDraft()
  if (parsed.error) return
  const body = parsed.value
  if (!body || typeof body !== 'object' || Array.isArray(body)) {
    errors.value = [{ path: '(корень)', message: 'Шаблон должен быть JSON-объектом' }]
    return
  }

  const id = currentId.value || String(body.id || '').trim()
  if (!ID_RE.test(id)) {
    setStatus('В шаблоне не задан корректный id', 'err')
    errors.value = [{ path: 'id', message: 'id должен соответствовать ^[a-z][a-z0-9-]*$' }]
    return
  }
  body.id = id
  if (!body.title) body.title = id

  saving.value = true
  setStatus('Сохранение…')
  try {
    if (currentId.value) {
      await api.updateReport(id, body)
    } else {
      await api.createReport(id, body.title, body)
      currentId.value = id
    }
    rememberBody(id, body)
    savedTitle.value = body.title
    savedText.value = text.value
    await loadList()
    refreshCurrentRow()
    setStatus('Сохранено', 'ok')
  } catch (e) {
    const message = humanError(e)
    setStatus(message, 'err')
    errors.value = [{ path: `(сервер: ${e.code || 'ERROR'})`, message }]
  } finally {
    saving.value = false
  }
}

function openCreate() {
  form.id = ''
  form.title = ''
  form.error = ''
  form.busy = false
  modal.value = 'create'
  nextTick(() => createIdInput.value?.focus())
}

async function createReport() {
  form.error = ''
  if (!ID_RE.test(form.id)) {
    form.error = 'id задаётся латиницей: ^[a-z][a-z0-9-]*$'
    return
  }
  if (!form.title) {
    form.error = 'Укажите название отчёта'
    return
  }
  form.busy = true
  const body = {
    version: 1,
    id: form.id,
    title: form.title,
    blocks: [{ type: 'text', text: form.title }]
  }
  try {
    await api.createReport(form.id, form.title, body)
    const id = form.id
    closeModal()
    await loadList()
    showTemplate(id, body)
    setStatus('Отчёт создан', 'ok')
  } catch (e) {
    form.error = humanError(e)
  } finally {
    form.busy = false
  }
}

function openSaveAs() {
  form.id = currentId.value ? `${currentId.value}-copy` : ''
  form.title = docTitle.value ? `${docTitle.value} (копия)` : ''
  form.error = ''
  form.busy = false
  modal.value = 'saveas'
}

async function saveAs() {
  form.error = ''
  if (!ID_RE.test(form.id)) {
    form.error = 'id задаётся латиницей: ^[a-z][a-z0-9-]*$'
    return
  }
  const parsed = parseDraft()
  if (parsed.error) {
    form.error = 'Текущий текст — невалидный JSON'
    return
  }
  const body = parsed.value
  if (!body || typeof body !== 'object' || Array.isArray(body)) {
    form.error = 'Шаблон должен быть JSON-объектом'
    return
  }
  form.busy = true
  body.id = form.id
  body.title = form.title || form.id
  try {
    await api.createReport(form.id, body.title, body)
    const id = form.id
    closeModal()
    await loadList()
    showTemplate(id, body)
    setStatus('Сохранено как новый отчёт', 'ok')
  } catch (e) {
    form.error = humanError(e)
  } finally {
    form.busy = false
  }
}

async function duplicate(row) {
  if (dirty.value && !confirm('Есть несохранённые изменения. Всё равно дублировать сохранённую версию?')) {
    return
  }
  try {
    let tpl = bodyCache.get(row.id) || readRemembered(row.id)
    if (!tpl) tpl = await api.adminTemplate(row.id)
    const existing = new Set(reports.value.map((r) => r.id))
    let newId = `${row.id}-copy`
    let n = 2
    while (existing.has(newId)) newId = `${row.id}-copy-${n++}`
    tpl.id = newId
    tpl.title = `${tpl.title || row.id} (копия)`
    await api.createReport(newId, tpl.title, tpl)
    await loadList()
    showTemplate(newId, tpl)
    setStatus(`Создана копия ${newId}`, 'ok')
  } catch (e) {
    setStatus(humanError(e), 'err')
  }
}

function askDelete(row) {
  deleteTarget.value = row
  form.id = ''
  form.error = ''
  form.busy = false
  modal.value = 'delete'
}

async function confirmDelete() {
  if (!deleteTarget.value || form.id !== deleteTarget.value.id) return
  form.busy = true
  form.error = ''
  const id = deleteTarget.value.id
  try {
    await api.deleteReport(id)
    forgetBody(id)
    const wasCurrent = currentId.value === id
    closeModal()
    await loadList()
    if (wasCurrent) {
      currentId.value = ''
      savedTitle.value = ''
      setDoc('')
      savedText.value = ''
      savedByteSize.value = null
      clearProblems()
      historyOpen.value = false
    }
    setStatus(`Отчёт ${id} удалён`, 'ok')
  } catch (e) {
    form.error = humanError(e)
  } finally {
    form.busy = false
  }
}

function previewDraft() {
  if (!currentId.value) return
  const key = `rep:draft:${currentId.value}`
  try {
    localStorage.setItem(key, text.value)
  } catch (e) {
    setStatus(`Не удалось сохранить черновик в localStorage: ${e.message}`, 'err')
    return
  }
  window.open(`${reportUrl(currentId.value)}&draft=1`, '_blank', 'noopener')
  setStatus(`Черновик сохранён в localStorage (${key}) и открыт в новой вкладке`, 'ok')
}

function onOpenReport(event) {
  if (!currentId.value) {
    event.preventDefault()
    setStatus('Сначала выберите отчёт', 'info')
  }
}

async function loadHistory() {
  if (!currentId.value) return
  historyLoading.value = true
  historyError.value = ''
  try {
    history.value = await api.adminHistory(currentId.value)
  } catch (e) {
    historyError.value = humanError(e)
    history.value = []
  } finally {
    historyLoading.value = false
  }
}

async function toggleHistory() {
  historyOpen.value = !historyOpen.value
  if (historyOpen.value) await loadHistory()
}

async function rollback(snapshot) {
  if (!currentId.value) return
  const ok = confirm(
    `Откатить «${currentId.value}» к версии от ${snapshot.saved}?\nТекущая версия будет сохранена в историю.`
  )
  if (!ok) return
  try {
    await api.adminRollback(currentId.value, snapshot.id)
    // после отката локальный кэш неактуален — перечитываем с сервера
    forgetBody(currentId.value)
    await loadTemplate(currentId.value, true)
    await loadHistory()
    await loadList()
    refreshCurrentRow()
    setStatus(`Откат к версии от ${snapshot.saved} выполнен`, 'ok')
  } catch (e) {
    setStatus(humanError(e), 'err')
    historyError.value = humanError(e)
  }
}

function closeModal() {
  modal.value = ''
  form.error = ''
  form.busy = false
  deleteTarget.value = null
}

function onKeydown(e) {
  if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 's') {
    e.preventDefault()
    save()
  }
  if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') {
    e.preventDefault()
    runValidate()
  }
  if (e.key === 'Escape' && modal.value) closeModal()
}

function onBeforeUnload(e) {
  if (!dirty.value) return
  e.preventDefault()
  e.returnValue = ''
}

onMounted(() => {
  view = new EditorView({
    parent: editorHost.value,
    state: EditorState.create({
      doc: '',
      extensions: [
        basicSetup,
        json(),
        lintGutter(),
        // пустой редактор не должен сразу краснеть: линтуем только непустой текст
        linter((v) => (v.state.doc.toString().trim() ? jsonParseLinter()(v) : [])),
        EditorView.updateListener.of((update) => {
          if (!update.docChanged || suppressUpdate) return
          text.value = update.state.doc.toString()
        }),
        EditorView.theme({
          '&': { height: '100%', fontSize: '13px', backgroundColor: '#fff' },
          '.cm-scroller': {
            fontFamily: "ui-monospace, SFMono-Regular, Menlo, Consolas, 'Liberation Mono', monospace",
            lineHeight: '1.55'
          },
          '.cm-content': { padding: '10px 0' },
          '.cm-gutters': { backgroundColor: '#f7f9fc', color: '#8a94a3', border: 'none' },
          '.cm-activeLineGutter': { backgroundColor: '#e9effa' }
        })
      ]
    })
  })
  loadList()
  window.addEventListener('keydown', onKeydown)
  window.addEventListener('beforeunload', onBeforeUnload)
})

onBeforeUnmount(() => {
  window.removeEventListener('keydown', onKeydown)
  window.removeEventListener('beforeunload', onBeforeUnload)
  if (view) view.destroy()
  view = null
})
</script>

<style>
html,
body,
#app {
  height: 100%;
}
body {
  margin: 0;
  font-family: system-ui, -apple-system, 'Segoe UI', Roboto, sans-serif;
  color: #1c1f23;
  background: #f6f8fb;
}
* {
  box-sizing: border-box;
}
</style>

<style scoped>
.subs-link {
  display: inline-block;
  margin: 0 0 10px;
  color: #1f6feb;
  text-decoration: none;
  font-size: 13px;
}
.subs-link:hover {
  text-decoration: underline;
}
/* Параметры и SQL справочников: видимая часть админки, а не 「найди в JSON」 */
.params {
  margin: 0 0 10px;
  padding: 10px 12px;
  background: #fff;
  border: 1px solid #dfe3e8;
  border-radius: 8px;
  max-height: none;
  display: grid;
  gap: 8px;
}
.params h3 {
  margin: 0 0 8px;
  font-size: 13px;
  font-weight: 600;
  color: #1c1f23;
}
.param {
  padding: 6px 8px;
  border: 1px solid #eef1f5;
  border-radius: 6px;
  background: #fbfcfe;
}
.param-head {
  display: flex;
  align-items: center;
  gap: 6px;
  flex-wrap: wrap;
  font-size: 12px;
}
.pname {
  font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
  background: #f2f5f9;
  border-radius: 4px;
  padding: 1px 6px;
  color: #1a4fa0;
}
.ptitle {
  color: #4a5361;
}
.tag {
  font-size: 11px;
  padding: 1px 6px;
  border-radius: 999px;
  background: #eef1f5;
  color: #5b6472;
}
.tag.kind {
  background: #eaf5ee;
  color: #1f5a38;
}
.tag.ok {
  background: #e6f4ea;
  color: #1f6b34;
}
.tag.bad {
  background: #fdeaea;
  color: #8c1f1f;
}
.psql {
  margin: 6px 0 0;
  padding: 6px 8px;
  background: #f7f9fc;
  border: 1px solid #e6eaef;
  border-radius: 6px;
  font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
  font-size: 11.5px;
  line-height: 1.45;
  white-space: pre-wrap;
  word-break: break-word;
  color: #26313d;
}
.small {
  font-size: 11.5px;
}
.shell {
  display: flex;
  height: 100%;
  min-height: 100vh;
}

/* ---------- левая колонка ---------- */
.sidebar {
  width: 360px;
  flex: 0 0 360px;
  display: flex;
  flex-direction: column;
  gap: 12px;
  padding: 18px 16px;
  background: #fff;
  border-right: 1px solid #e2e7ee;
  overflow: auto;
}
.brand {
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
  gap: 10px;
}
h1 {
  margin: 0;
  font-size: 17px;
  letter-spacing: -0.01em;
}
.brand-sub {
  margin: 3px 0 0;
  font-size: 11px;
  color: #7b8492;
}
.search {
  font: inherit;
  font-size: 13px;
  padding: 7px 10px;
  border: 1px solid #cfd6de;
  border-radius: 8px;
  background: #fbfcfe;
  width: 100%;
}
.search:focus,
.dialog input:focus {
  outline: 2px solid #b9cdf0;
  outline-offset: 0;
  border-color: #7ba3e0;
}
.reports {
  list-style: none;
  margin: 0;
  padding: 0;
  display: grid;
  gap: 6px;
}
.report {
  border: 1px solid #e3e8ef;
  border-radius: 9px;
  background: #fff;
  padding: 8px 10px;
  display: grid;
  gap: 4px;
}
.report.active {
  border-color: #9db9e8;
  background: #f5f9ff;
  box-shadow: 0 0 0 1px #d6e3f8 inset;
}
.report-main {
  all: unset;
  cursor: pointer;
  display: grid;
  gap: 3px;
}
.report-title {
  font-size: 13.5px;
  font-weight: 600;
  line-height: 1.3;
}
.report-meta {
  display: flex;
  flex-wrap: wrap;
  gap: 7px;
  font-size: 11px;
  color: #7b8492;
  align-items: baseline;
}
.report-meta code,
.head-meta code,
.problem code {
  font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
}
.report-meta code {
  background: #f1f4f9;
  border-radius: 4px;
  padding: 0 4px;
  color: #4a5567;
}
.report-actions {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  font-size: 11.5px;
}
.report-actions button,
.report-actions a {
  all: unset;
  cursor: pointer;
  color: #1a4fa0;
  padding: 1px 0;
}
.report-actions button:hover,
.report-actions a:hover {
  text-decoration: underline;
}
.report-actions .danger-text {
  color: #a32626;
}
.side-foot {
  margin-top: auto;
  padding-top: 10px;
  display: flex;
  justify-content: space-between;
  font-size: 11.5px;
}
.side-foot a {
  color: #5b6b86;
}

/* ---------- правая колонка ---------- */
.main {
  flex: 1 1 auto;
  min-width: 0;
  display: flex;
  flex-direction: column;
  gap: 10px;
  padding: 16px 20px 20px;
  height: 100vh;
}
.head {
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
  gap: 14px;
  flex-wrap: wrap;
}
.head-title h2 {
  margin: 0 0 3px;
  font-size: 18px;
  letter-spacing: -0.01em;
}
.head-meta {
  margin: 0;
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 12px;
  color: #5b6472;
}
.head-meta code {
  background: #eef2f8;
  padding: 1px 6px;
  border-radius: 4px;
  color: #40506b;
}
.draft-chip {
  font-size: 11px;
  color: #6b4a17;
  background: #fdf3e6;
  border: 1px solid #f3e0c4;
  border-radius: 999px;
  padding: 1px 8px;
}
.dirty {
  color: #a05a00;
}
.saved {
  color: #2c7a4b;
}
.actions {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
  justify-content: flex-end;
}
.btn {
  all: unset;
  display: inline-flex;
  align-items: center;
  gap: 6px;
  cursor: pointer;
  font-size: 12.5px;
  line-height: 1.2;
  padding: 7px 11px;
  border: 1px solid #cfd6de;
  border-radius: 7px;
  background: #fff;
  color: #26303d;
  white-space: nowrap;
}
.btn:hover {
  background: #f3f6fa;
  border-color: #b9c3d0;
}
.btn.primary {
  background: #1a4fa0;
  border-color: #1a4fa0;
  color: #fff;
}
.btn.primary:hover {
  background: #17458c;
}
.btn.danger {
  background: #a32626;
  border-color: #a32626;
  color: #fff;
}
.btn.danger:hover {
  background: #8d1f1f;
}
.btn.small {
  padding: 4px 8px;
  font-size: 11.5px;
}
.btn[disabled],
.btn.disabled {
  opacity: 0.45;
  cursor: not-allowed;
  pointer-events: none;
}
.btn.link {
  text-decoration: none;
}

.statusbar {
  display: flex;
  gap: 12px;
  align-items: center;
  font-size: 11.5px;
  color: #5b6472;
  min-height: 17px;
}
.spacer {
  flex: 1;
}
.dim {
  color: #8a94a3;
}
.status.ok {
  color: #2c7a4b;
}
.status.err {
  color: #a32626;
}
.status.warn {
  color: #a05a00;
}

.problems {
  flex: 0 0 auto;
  background: #fff;
  border: 1px solid #e3e8ef;
  border-radius: 9px;
  padding: 8px 12px;
  max-height: 190px;
  overflow: auto;
  display: grid;
  gap: 6px;
}
.problem-group h3 {
  margin: 0 0 4px;
  font-size: 11px;
  text-transform: uppercase;
  letter-spacing: 0.06em;
  color: #7b8492;
}
.problem-group ul {
  list-style: none;
  margin: 0;
  padding: 0;
  display: grid;
  gap: 2px;
}
.problem {
  all: unset;
  cursor: pointer;
  display: flex;
  gap: 8px;
  align-items: baseline;
  font-size: 12px;
  padding: 3px 4px;
  border-radius: 5px;
  width: 100%;
}
.problem:hover {
  background: #f4f7fb;
}
.problem code {
  font-size: 11px;
  color: #40506b;
  background: #f1f4f9;
  border-radius: 4px;
  padding: 0 5px;
  white-space: nowrap;
}
.problem.err span {
  color: #8c1f1f;
}
.problem.warn span {
  color: #7a4c00;
}

.editor {
  flex: 1 1 auto;
  min-height: 320px;
  background: #fff;
  border: 1px solid #dfe5ec;
  border-radius: 10px;
  overflow: hidden;
}
.editor.empty {
  background: #f8fafc;
}
.banner {
  margin: 0;
  font-size: 12.5px;
  padding: 8px 10px;
  border-radius: 7px;
}
.banner.err {
  background: #fdeaea;
  border: 1px solid #f3cfcf;
  color: #8c1f1f;
}
.muted {
  margin: 0;
  color: #7b8492;
  font-size: 12.5px;
}
.small {
  font-size: 11.5px;
  line-height: 1.4;
}

/* ---------- история ---------- */
.history {
  flex: 0 0 auto;
  background: #fff;
  border: 1px solid #dfe5ec;
  border-radius: 10px;
  padding: 10px 14px;
  max-height: 220px;
  overflow: auto;
}
.history-head {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 4px;
}
.history-head h3 {
  margin: 0;
  font-size: 13px;
}
.snapshots {
  list-style: none;
  margin: 0;
  padding: 0;
  display: grid;
  gap: 2px;
}
.snapshots li {
  display: flex;
  align-items: center;
  gap: 10px;
  font-size: 12px;
  padding: 4px 2px;
  border-bottom: 1px solid #f0f3f7;
}
.snapshots li:last-child {
  border-bottom: none;
}
.snap-time {
  font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
  color: #40506b;
  font-size: 11.5px;
}
.snap-title {
  flex: 1;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  color: #4a5361;
}

/* ---------- диалоги ---------- */
.overlay {
  position: fixed;
  inset: 0;
  background: rgba(24, 32, 44, 0.35);
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 20px;
  z-index: 50;
}
.dialog {
  width: 420px;
  max-width: 100%;
  background: #fff;
  border-radius: 12px;
  box-shadow: 0 18px 50px rgba(22, 34, 51, 0.22);
  padding: 18px;
  display: grid;
  gap: 10px;
}
.dialog h3 {
  margin: 0;
  font-size: 16px;
}
.dialog label {
  display: grid;
  gap: 4px;
  font-size: 11.5px;
  color: #5b6472;
}
.dialog input {
  font: inherit;
  font-size: 13px;
  padding: 7px 9px;
  border: 1px solid #cfd6de;
  border-radius: 7px;
}
.dialog-actions {
  display: flex;
  justify-content: flex-end;
  gap: 8px;
  margin-top: 4px;
}

@media (max-width: 1100px) {
  .shell {
    display: block;
    height: auto;
  }
  .sidebar {
    width: 100%;
    flex: none;
    border-right: none;
    border-bottom: 1px solid #e2e7ee;
    max-height: 46vh;
  }
  .main {
    height: auto;
    min-height: 70vh;
  }
  .editor {
    height: 60vh;
    flex: none;
  }
}
</style>
