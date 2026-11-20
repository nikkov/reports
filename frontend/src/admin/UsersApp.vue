<template>
  <!-- Пользователи (ADR-172): компактная таблица, фильтры и модальное окно создания.
       Выдача ролей и смена пароля — в диалоге «Изменить», чтобы строка оставалась низкой. -->
  <div class="users">
    <header class="page-head">
      <h1>Пользователи</h1>
      <button type="button" class="tool primary" data-open-create @click="openCreate">
        Добавить пользователя
      </button>
    </header>
    <p class="muted">
      Системная роль: <b>viewer</b> — читает отчёты, <b>author</b> — создаёт и правит черновики,
      <b>admin</b> — публикует, удаляет, управляет пользователями и рассылкой. Роли доступа
      (вкладка «Роли») ограничивают отчёты. Отключение и смена пароля завершают активные сессии.
    </p>

    <p v-if="error" class="err">{{ error }}</p>
    <p v-if="notice" class="ok">{{ notice }}</p>

    <!-- Фильтры: логин, системная роль, роль доступа, состояние -->
    <div class="filters" data-filters>
      <label class="f-search">
        <span>Логин</span>
        <input v-model.trim="filters.q" placeholder="часть логина" autocomplete="off" data-filter-login />
      </label>
      <label>
        <span>Системная роль</span>
        <select v-model="filters.role" data-filter-role>
          <option value="">все</option>
          <option value="viewer">viewer</option>
          <option value="author">author</option>
          <option value="admin">admin</option>
        </select>
      </label>
      <label>
        <span>Роль доступа</span>
        <select v-model="filters.access" data-filter-access>
          <option value="">все</option>
          <option value="__none">без ролей доступа</option>
          <option v-for="r in roles" :key="r.key" :value="r.key">{{ r.key }}</option>
        </select>
      </label>
      <label>
        <span>Состояние</span>
        <select v-model="filters.state" data-filter-state>
          <option value="">все</option>
          <option value="on">включён</option>
          <option value="off">отключён</option>
        </select>
      </label>
      <button v-if="hasFilters" type="button" class="link" data-reset-filters @click="resetFilters">
        сбросить
      </button>
      <span class="muted small counter" data-counter>
        показано {{ filtered.length }} из {{ items.length }}
      </span>
    </div>

    <table class="list" v-if="filtered.length" data-users-table>
      <thead>
        <tr>
          <th>Логин</th>
          <th>Роль</th>
          <th>Роли доступа</th>
          <th>Состояние</th>
          <th></th>
        </tr>
      </thead>
      <tbody>
        <tr v-for="u in filtered" :key="u.id" :class="{ off: !u.enabled }" :data-user="u.login">
          <td class="login"><b>{{ u.login }}</b><span v-if="u.login === me" class="me"> · это вы</span></td>
          <td><span class="role-tag">{{ u.role }}</span></td>
          <td class="access">
            <span v-for="k in (u.roles || []).slice(0, 3)" :key="k" class="chip">{{ k }}</span>
            <span
              v-if="(u.roles || []).length > 3"
              class="chip more"
              :title="(u.roles || []).join(', ')"
            >+{{ (u.roles || []).length - 3 }}</span>
            <span v-if="!(u.roles || []).length" class="muted small">—</span>
          </td>
          <td>
            <span :class="u.enabled ? 'on' : 'off-label'">{{ u.enabled ? 'включён' : 'отключён' }}</span>
          </td>
          <td class="actions">
            <button type="button" class="tool" :disabled="busy === u.id" @click="openEdit(u)">
              Изменить
            </button>
            <button type="button" class="tool danger" :disabled="busy === u.id" @click="remove(u)">
              Удалить
            </button>
          </td>
        </tr>
      </tbody>
    </table>
    <p v-else-if="items.length" class="muted">Никого не нашли — измените фильтры.</p>
    <p v-else class="muted">Пользователей нет.</p>

    <!-- Диалог создания и правки -->
    <div v-if="modal" class="overlay" @click.self="closeModal">
      <form class="dialog" @submit.prevent="submit">
        <h3>{{ modal === 'create' ? 'Новый пользователь' : `Пользователь ${edit.login}` }}</h3>

        <label v-if="modal === 'create'">
          Логин
          <input v-model.trim="form.login" placeholder="analyst_1 или CORP\ivanov" required data-form-login />
          <span class="hint">
            латинские буквы в любом регистре, цифры, «_», «.», «-», «\», «@»; от 3 до 128 символов.
            Доменная учётная запись — CORP\ivanov или ivanov@corp.example
          </span>
        </label>
        <p v-else class="form-meta">
          <code>{{ edit.login }}</code>
          <span class="muted small">
            создан {{ formatDate(edit.createdAt) }} · изменён {{ formatDate(edit.updatedAt) }}
          </span>
        </p>

        <label>
          Системная роль
          <select v-model="form.role" data-form-role>
            <option value="viewer">viewer</option>
            <option value="author">author</option>
            <option value="admin">admin</option>
          </select>
        </label>

        <label v-if="modal === 'edit'" class="check-row">
          <input type="checkbox" v-model="form.enabled" data-form-enabled />
          <span>Учётная запись включена</span>
        </label>

        <fieldset v-if="roles.length">
          <legend>Роли доступа</legend>
          <div class="role-grid">
            <label v-for="r in roles" :key="r.key" class="role-check">
              <input type="checkbox" :value="r.key" v-model="form.roles" />
              <span>{{ r.key }}</span>
              <span class="muted small">{{ r.title }}</span>
            </label>
          </div>
        </fieldset>

        <label>
          {{ modal === 'create' ? 'Пароль' : 'Новый пароль' }}
          <input
            v-model="form.password"
            type="password"
            autocomplete="new-password"
            :placeholder="modal === 'create' ? '' : 'пусто — не менять'"
            data-form-password
          />
          <span class="hint">
            не короче 8 символов; пусто — учётная запись без пароля, вход только через доверенный
            прокси-сервер (доменная аутентификация)
          </span>
        </label>

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

const props = defineProps({ session: { type: Object, default: null } })

const items = ref([])
const roles = ref([])
const error = ref(null)
const notice = ref(null)
const busy = ref(null)
const saving = ref(false)
const me = props.session?.login || ''

const filters = reactive({ q: '', role: '', access: '', state: '' })

const modal = ref(null) // 'create' | 'edit'
const edit = ref(null)
const modalError = ref(null)
const form = reactive({ login: '', role: 'viewer', password: '', roles: [], enabled: true })

const hasFilters = computed(
  () => Boolean(filters.q || filters.role || filters.access || filters.state)
)

// фильтрация на клиенте: список пользователей небольшой, серверный фильтр здесь не нужен
const filtered = computed(() => {
  const q = filters.q.toLowerCase()
  return items.value.filter((u) => {
    if (q && !u.login.toLowerCase().includes(q)) return false
    if (filters.role && u.role !== filters.role) return false
    if (filters.state === 'on' && !u.enabled) return false
    if (filters.state === 'off' && u.enabled) return false
    if (filters.access === '__none' && (u.roles || []).length) return false
    if (filters.access && filters.access !== '__none' && !(u.roles || []).includes(filters.access)) {
      return false
    }
    return true
  })
})

function resetFilters() {
  filters.q = ''
  filters.role = ''
  filters.access = ''
  filters.state = ''
}

function formatDate(value) {
  if (!value) return '—'
  const date = new Date(value)
  return Number.isNaN(date.getTime()) ? value : date.toLocaleString('ru-RU')
}

async function load() {
  error.value = null
  try {
    items.value = (await api.users()).items || []
  } catch (e) {
    error.value = e.message
  }
}

async function loadRoles() {
  try {
    roles.value = (await api.accessRoles()).items || []
  } catch (e) {
    error.value = e.message
  }
}

function openCreate() {
  modalError.value = null
  edit.value = null
  Object.assign(form, { login: '', role: 'viewer', password: '', roles: [], enabled: true })
  modal.value = 'create'
}

function openEdit(user) {
  modalError.value = null
  edit.value = user
  Object.assign(form, {
    login: user.login,
    role: user.role,
    password: '',
    roles: [...(user.roles || [])],
    enabled: user.enabled
  })
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
  error.value = null
  notice.value = null
  try {
    if (modal.value === 'create') {
      await api.createUser({
        login: form.login.trim(),
        role: form.role,
        password: form.password,
        roles: [...form.roles]
      })
      notice.value = `Пользователь ${form.login} создан`
    } else {
      const password = form.password.trim()
      if (password && password.length < 8) {
        modalError.value = 'пароль короче 8 символов'
        return
      }
      await api.updateUser(edit.value.id, {
        role: form.role,
        enabled: form.enabled,
        roles: [...form.roles],
        ...(password ? { password } : {})
      })
      notice.value = `Пользователь ${edit.value.login} сохранён`
    }
    await load()
    closeModal()
  } catch (e) {
    modalError.value = e.message
  } finally {
    saving.value = false
  }
}

async function remove(u) {
  if (!window.confirm(`Удалить пользователя ${u.login}?`)) return
  busy.value = u.id
  error.value = null
  try {
    await api.deleteUser(u.id)
    notice.value = `Пользователь ${u.login} удалён`
    if (edit.value && edit.value.id === u.id) closeModal()
    await load()
  } catch (e) {
    error.value = e.message
  } finally {
    busy.value = null
  }
}

onMounted(async () => {
  await loadRoles()
  await load()
})
</script>

<style scoped>
.users {
  max-width: 1100px;
  margin: 0 auto;
  padding: 16px 16px 48px;
  font: 13px/1.45 system-ui, sans-serif;
  color: var(--rep-text);
  height: 100%;
  overflow: auto;
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
.me {
  color: var(--rep-text-faint);
  font-size: 11.5px;
}
.on {
  color: var(--rep-ok);
}
.off-label {
  color: var(--rep-danger-strong);
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
  min-width: 180px;
}
.filters input,
.filters select {
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
.list tr.off {
  opacity: 0.6;
}
.list td.login {
  white-space: nowrap;
}
.role-tag {
  font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
  font-size: 11.5px;
  padding: 1px 6px;
  border: 1px solid var(--rep-border);
  border-radius: 999px;
  color: var(--rep-text-muted);
}
.chip {
  display: inline-block;
  font-size: 11px;
  padding: 0 6px;
  margin-right: 4px;
  border-radius: 999px;
  background: var(--rep-code-bg);
  color: var(--rep-text);
  white-space: nowrap;
}
.chip.more {
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
.dialog input,
.dialog select {
  padding: 5px 8px;
  border: 1px solid var(--rep-border-input);
  border-radius: 6px;
  font: inherit;
  font-size: 13px;
  background: var(--rep-surface);
  color: var(--rep-text);
}
.check-row {
  display: flex !important;
  align-items: center;
  gap: 8px;
  color: var(--rep-text) !important;
}
.check-row input {
  width: auto;
}
fieldset {
  border: 1px solid var(--rep-border);
  border-radius: 8px;
  padding: 6px 10px 10px;
  margin: 0;
}
legend {
  font-size: 11.5px;
  color: var(--rep-text-muted);
  padding: 0 4px;
}
.role-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 4px 12px;
}
.role-check {
  display: flex !important;
  align-items: center;
  gap: 6px;
  font-size: 12.5px;
  color: var(--rep-text) !important;
}
.role-check input {
  width: auto;
}
.form-meta {
  display: flex;
  flex-direction: column;
  gap: 2px;
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
