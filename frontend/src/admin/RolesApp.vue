<template>
  <!-- Роли доступа (ADR-171, ADR-173): компактная таблица, фильтры и модальное создание/правка,
       как на вкладке «Пользователи». Ключ роли после создания не меняется. -->
  <div class="roles-page">
    <header class="page-head">
      <h1>Роли доступа</h1>
      <button type="button" class="tool primary" data-open-create @click="openCreate">
        Добавить роль
      </button>
    </header>
    <p class="muted">
      Роль — ключ, который указывается в шаблоне отчёта (<code>accessRoles</code>) и выдаётся
      пользователям во вкладке «Пользователи». Пустой список у отчёта — доступ всем авторизованным,
      администратор видит всё. Удаление роли снимает её у всех пользователей.
    </p>

    <p v-if="error" class="err">{{ error }}</p>
    <p v-if="notice" class="ok">{{ notice }}</p>

    <!-- Фильтры: ключ или название, использование -->
    <div class="filters" data-filters>
      <label class="f-search">
        <span>Ключ или название</span>
        <input
          v-model.trim="filters.q"
          placeholder="finance или Финансы"
          autocomplete="off"
          data-filter-q
        />
      </label>
      <label>
        <span>Использование</span>
        <select v-model="filters.usage" data-filter-usage>
          <option value="">все</option>
          <option value="users">выдана пользователям</option>
          <option value="reports">закрывает отчёты</option>
          <option value="unused">не используется</option>
        </select>
      </label>
      <button v-if="hasFilters" type="button" class="link" data-reset-filters @click="resetFilters">
        сбросить
      </button>
      <span class="muted small counter" data-counter>
        показано {{ filtered.length }} из {{ items.length }}
      </span>
    </div>

    <table class="list" v-if="filtered.length" data-roles-table>
      <thead>
        <tr>
          <th>Ключ</th>
          <th>Название</th>
          <th>Пользователей</th>
          <th>Отчётов</th>
          <th></th>
        </tr>
      </thead>
      <tbody>
        <tr v-for="r in filtered" :key="r.id" :data-role="r.key">
          <td><code class="key">{{ r.key }}</code></td>
          <td class="title">{{ r.title }}</td>
          <td>
            <button
              v-if="usersIn(r.key).length"
              type="button"
              class="count"
              :title="usersIn(r.key).join(', ')"
            >{{ usersIn(r.key).length }}</button>
            <span v-else class="muted">0</span>
          </td>
          <td>
            <span
              v-if="reportsIn(r.key).length"
              class="count-static"
              :title="reportsIn(r.key).join(', ')"
            >{{ reportsIn(r.key).length }}</span>
            <span v-else class="muted">0</span>
          </td>
          <td class="actions">
            <button type="button" class="tool" :disabled="busy === r.id" @click="openEdit(r)">
              Изменить
            </button>
            <button type="button" class="tool danger" :disabled="busy === r.id" @click="remove(r)">
              Удалить
            </button>
          </td>
        </tr>
      </tbody>
    </table>
    <p v-else-if="items.length" class="muted">Ничего не нашли — измените фильтры.</p>
    <p v-else class="muted">Ролей пока нет — добавьте первую.</p>

    <!-- Диалог создания и переименования -->
    <div v-if="modal" class="overlay" @click.self="closeModal">
      <form class="dialog" @submit.prevent="submit">
        <h3>{{ modal === 'create' ? 'Новая роль доступа' : `Роль ${edit.key}` }}</h3>

        <label v-if="modal === 'create'">
          Ключ
          <input v-model.trim="form.key" placeholder="finance" autocomplete="off" data-form-key />
          <span class="hint">
            латиница в нижнем регистре, цифры, «_» и «-», от 2 до 32 символов. Ключ указывается в
            шаблоне (<code>accessRoles</code>) и после создания не меняется.
          </span>
        </label>
        <p v-else class="form-meta"><code class="key">{{ edit.key }}</code></p>

        <label>
          Название
          <input v-model.trim="form.title" placeholder="Финансы" autocomplete="off" data-form-title />
        </label>

        <div v-if="modal === 'edit'" class="usage">
          <p>
            <b>Пользователи:</b>
            <span v-if="usersIn(edit.key).length">{{ usersIn(edit.key).join(', ') }}</span>
            <span v-else class="muted">никому не выдана</span>
          </p>
          <p>
            <b>Отчёты:</b>
            <span v-if="reportsIn(edit.key).length">{{ reportsIn(edit.key).join(', ') }}</span>
            <span v-else class="muted">не закрывает ни одного отчёта</span>
          </p>
        </div>

        <p v-if="modalError" class="err">{{ modalError }}</p>
        <div class="buttons">
          <button type="submit" class="tool primary" :disabled="saving" data-form-submit>
            {{ saving ? 'Сохранение…' : modal === 'create' ? 'Создать' : 'Сохранить' }}
          </button>
          <button type="button" class="tool" @click="closeModal">Отмена</button>
        </div>
      </form>
    </div>
  </div>
</template>

<script setup>
import { computed, onMounted, reactive, ref } from 'vue'
import { api } from '../api/client.js'

const items = ref([])
const users = ref([])
const reports = ref([])
const error = ref(null)
const notice = ref(null)
const saving = ref(false)
const busy = ref(null)

const filters = reactive({ q: '', usage: '' })

const modal = ref(null) // 'create' | 'edit'
const edit = ref(null)
const modalError = ref(null)
const form = reactive({ key: '', title: '' })

// кто носит роль и какие отчёты ею закрыты — видно сразу, без перехода на другие вкладки
function usersIn(key) {
  return users.value.filter((u) => (u.roles || []).includes(key)).map((u) => u.login)
}
function reportsIn(key) {
  return reports.value.filter((r) => (r.accessRoles || []).includes(key)).map((r) => r.id)
}

const hasFilters = computed(() => Boolean(filters.q || filters.usage))

const filtered = computed(() => {
  const q = filters.q.toLowerCase()
  return items.value.filter((r) => {
    if (q && !r.key.toLowerCase().includes(q) && !(r.title || '').toLowerCase().includes(q)) {
      return false
    }
    const hasUsers = usersIn(r.key).length > 0
    const hasReports = reportsIn(r.key).length > 0
    if (filters.usage === 'users' && !hasUsers) return false
    if (filters.usage === 'reports' && !hasReports) return false
    if (filters.usage === 'unused' && (hasUsers || hasReports)) return false
    return true
  })
})

function resetFilters() {
  filters.q = ''
  filters.usage = ''
}

async function load() {
  error.value = null
  try {
    const [roles, people, tpls] = await Promise.all([api.accessRoles(), api.users(), api.adminReports()])
    items.value = roles.items || []
    users.value = people.items || []
    reports.value = tpls || []
  } catch (e) {
    error.value = e.message
  }
}

function openCreate() {
  modalError.value = null
  edit.value = null
  Object.assign(form, { key: '', title: '' })
  modal.value = 'create'
}

function openEdit(role) {
  modalError.value = null
  edit.value = role
  Object.assign(form, { key: role.key, title: role.title })
  modal.value = 'edit'
}

function closeModal() {
  modal.value = null
  edit.value = null
  modalError.value = null
}

async function submit() {
  saving.value = true
  modalError.value = null
  notice.value = null
  try {
    if (modal.value === 'create') {
      await api.createAccessRole({ key: form.key.trim(), title: form.title.trim() })
      notice.value = `Роль ${form.key} добавлена`
    } else {
      await api.updateAccessRole(edit.value.id, { title: form.title })
      notice.value = `Роль ${edit.value.key} переименована`
    }
    await load()
    closeModal()
  } catch (e) {
    modalError.value = e.message
  } finally {
    saving.value = false
  }
}

async function remove(role) {
  const inReports = reportsIn(role.key)
  const inUsers = usersIn(role.key)
  const parts = [`Удалить роль «${role.key}»?`]
  if (inUsers.length) parts.push(`Она снимется у пользователей: ${inUsers.join(', ')}.`)
  if (inReports.length) parts.push(`Отчёты ${inReports.join(', ')} станут доступны всем авторизованным.`)
  if (!window.confirm(parts.join(' '))) return
  busy.value = role.id
  error.value = null
  try {
    await api.deleteAccessRole(role.id)
    notice.value = `Роль ${role.key} удалена`
    if (edit.value && edit.value.id === role.id) closeModal()
    await load()
  } catch (e) {
    error.value = e.message
  } finally {
    busy.value = null
  }
}

onMounted(load)
</script>

<style scoped>
.roles-page {
  padding: 16px 16px 48px;
  max-width: 1100px;
  margin: 0 auto;
  height: 100%;
  overflow: auto;
  font: 13px/1.45 system-ui, sans-serif;
  color: var(--rep-text);
}
.page-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  flex-wrap: wrap;
}
h1 {
  font-size: 18px;
  margin: 0 0 4px;
}
h3 {
  font-size: 15px;
  margin: 0 0 6px;
}
.muted {
  color: var(--rep-text-muted);
  font-size: 12.5px;
}
.small {
  font-size: 12px;
}
.err {
  color: var(--rep-danger-strong);
}
.ok {
  color: var(--rep-ok);
}

/* Фильтры */
.filters {
  display: flex;
  align-items: flex-end;
  gap: 10px;
  flex-wrap: wrap;
  padding: 8px 0 10px;
  border-bottom: 1px solid var(--rep-border);
  margin-bottom: 8px;
}
.filters label {
  display: grid;
  gap: 3px;
  font-size: 11.5px;
  color: var(--rep-text-muted);
}
.f-search input {
  min-width: 220px;
}
.filters input,
.filters select {
  box-sizing: border-box;
  padding: 4px 6px;
  border: 1px solid var(--rep-border-input);
  border-radius: 5px;
  font: inherit;
  font-size: 12.5px;
  background: var(--rep-surface);
  color: var(--rep-text);
}
.link {
  border: none;
  background: none;
  padding: 0 0 4px;
  font: inherit;
  font-size: 12px;
  color: var(--rep-focus);
  text-decoration: underline;
  cursor: pointer;
}
.counter {
  margin-left: auto;
  padding-bottom: 4px;
}

/* Таблица: строка минимальной высоты */
.list {
  width: 100%;
  border-collapse: collapse;
  background: var(--rep-surface);
  border: 1px solid var(--rep-border);
}
.list th,
.list td {
  text-align: left;
  padding: 4px 8px;
  border-bottom: 1px solid var(--rep-code-bg);
  vertical-align: middle;
  line-height: 1.25;
}
.list th {
  background: var(--rep-bg);
  font-weight: 600;
  font-size: 11px;
  text-transform: uppercase;
  letter-spacing: 0.02em;
  color: var(--rep-text-muted);
}
.list td {
  height: 28px;
}
.key {
  font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
  font-size: 11.5px;
  padding: 1px 6px;
  border: 1px solid var(--rep-border);
  border-radius: 999px;
}
.title {
  max-width: 420px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.count {
  border: none;
  background: none;
  padding: 0;
  font: inherit;
  color: var(--rep-focus);
  text-decoration: underline dotted;
  cursor: help;
}
.count-static {
  cursor: help;
}
.actions {
  display: flex;
  gap: 4px;
  justify-content: flex-end;
  white-space: nowrap;
}
.tool {
  padding: 3px 9px;
  border: 1px solid var(--rep-border-input);
  background: var(--rep-surface);
  border-radius: 5px;
  cursor: pointer;
  font-size: 12px;
  color: var(--rep-text);
}
.tool:hover {
  background: var(--rep-surface-hover);
}
.tool.primary {
  background: var(--rep-focus);
  border-color: var(--rep-focus);
  color: var(--rep-surface);
}
.tool.danger {
  color: var(--rep-danger-strong);
}
.tool:disabled {
  opacity: 0.6;
  cursor: default;
}

/* Диалог */
.overlay {
  position: fixed;
  inset: 0;
  background: rgba(15, 20, 25, 0.45);
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 20px;
  z-index: 50;
}
.dialog {
  background: var(--rep-surface);
  color: var(--rep-text);
  border: 1px solid var(--rep-border);
  border-radius: 10px;
  padding: 16px 18px;
  width: min(520px, 100%);
  max-height: 90vh;
  overflow: auto;
  display: grid;
  gap: 10px;
  box-shadow: 0 12px 32px rgba(0, 0, 0, 0.25);
}
.dialog label {
  display: grid;
  gap: 4px;
  font-size: 12.5px;
  color: var(--rep-text-muted);
}
.dialog input {
  box-sizing: border-box; /* padding не должен расширять поле за подложку (ADR-178) */
  padding: 5px 8px;
  border: 1px solid var(--rep-border-input);
  border-radius: 6px;
  font: inherit;
  font-size: 13px;
  background: var(--rep-surface);
  color: var(--rep-text);
}
.form-meta {
  margin: 0;
}
.usage {
  display: grid;
  gap: 2px;
  font-size: 12px;
  color: var(--rep-text-muted);
}
.usage p {
  margin: 0;
}
.hint {
  color: var(--rep-text-faint);
  font-size: 11.5px;
}
.buttons {
  display: flex;
  gap: 8px;
  justify-content: flex-end;
}
</style>
