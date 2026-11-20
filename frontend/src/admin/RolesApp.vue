<template>
  <!-- Роли доступа (ADR-171): отдельная вкладка. Ключ роли указывается в шаблоне (accessRoles)
       и выдаётся пользователям; здесь — справочник, кто и что этой ролью закрыто. -->
  <div class="roles-page">
    <h1>Роли доступа</h1>
    <p class="muted">
      Роль — это ключ, который указывается в шаблоне отчёта (<code>accessRoles</code>) и выдаётся
      пользователям во вкладке «Пользователи». Пустой список у отчёта — доступ всем авторизованным,
      администратор видит всё. Удаление роли снимает её у всех пользователей.
    </p>

    <p v-if="error" class="err">{{ error }}</p>
    <p v-if="notice" class="ok">{{ notice }}</p>

    <table class="list" v-if="items.length">
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
        <tr v-for="r in items" :key="r.id" :data-role="r.key">
          <td><code>{{ r.key }}</code></td>
          <td>
            <input v-if="editId === r.id" v-model.trim="editTitle" class="inline-input" />
            <span v-else>{{ r.title }}</span>
          </td>
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
            <span v-if="reportsIn(r.key).length" :title="reportsIn(r.key).join(', ')">
              {{ reportsIn(r.key).length }}
            </span>
            <span v-else class="muted">0</span>
          </td>
          <td class="actions">
            <template v-if="editId === r.id">
              <button type="button" class="tool primary" :disabled="saving" @click="saveTitle(r)">
                Сохранить
              </button>
              <button type="button" class="tool" @click="cancelEdit">Отмена</button>
            </template>
            <template v-else>
              <button type="button" class="tool" :disabled="saving" @click="startEdit(r)">
                Переименовать
              </button>
              <button type="button" class="tool danger" :disabled="saving" @click="remove(r)">
                Удалить
              </button>
            </template>
          </td>
        </tr>
      </tbody>
    </table>
    <p v-else class="muted">Ролей пока нет — добавьте первую ниже.</p>

    <form class="form" @submit.prevent="add">
      <h2>Новая роль</h2>
      <label>
        Ключ
        <input v-model.trim="form.key" placeholder="finance" autocomplete="off" data-new-role-key />
        <span class="hint">
          латиница в нижнем регистре, цифры, «_» и «-», от 2 до 32 символов; по ключу роль
          указывается в шаблоне
        </span>
      </label>
      <label>
        Название
        <input v-model.trim="form.title" placeholder="Финансы" autocomplete="off" data-new-role-title />
      </label>
      <button type="submit" class="tool primary" :disabled="saving" data-add-role>
        {{ saving ? 'Сохранение…' : 'Добавить роль' }}
      </button>
    </form>
  </div>
</template>

<script setup>
import { onMounted, reactive, ref } from 'vue'
import { api } from '../api/client.js'

const items = ref([])
const users = ref([])
const reports = ref([])
const error = ref(null)
const notice = ref(null)
const saving = ref(false)
const editId = ref(null)
const editTitle = ref('')

const form = reactive({ key: '', title: '' })

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

// кто носит роль и какие отчёты ею закрыты — видно сразу, без перехода на другие вкладки
function usersIn(key) {
  return users.value.filter((u) => (u.roles || []).includes(key)).map((u) => u.login)
}
function reportsIn(key) {
  return reports.value.filter((r) => (r.accessRoles || []).includes(key)).map((r) => r.id)
}

async function add() {
  saving.value = true
  error.value = null
  notice.value = null
  try {
    await api.createAccessRole({ key: form.key.trim(), title: form.title.trim() })
    notice.value = `Роль ${form.key} добавлена`
    form.key = ''
    form.title = ''
    await load()
  } catch (e) {
    error.value = e.message
  } finally {
    saving.value = false
  }
}

function startEdit(role) {
  editId.value = role.id
  editTitle.value = role.title
  error.value = null
}

function cancelEdit() {
  editId.value = null
  editTitle.value = ''
}

async function saveTitle(role) {
  if (!editTitle.value) {
    error.value = 'название роли не может быть пустым'
    return
  }
  saving.value = true
  error.value = null
  try {
    await api.updateAccessRole(role.id, { title: editTitle.value })
    notice.value = `Роль ${role.key} переименована`
    cancelEdit()
    await load()
  } catch (e) {
    error.value = e.message
  } finally {
    saving.value = false
  }
}

async function remove(role) {
  const inUse = reportsIn(role.key)
  const warning = inUse.length
    ? `Роль «${role.key}» закрывает отчёты: ${inUse.join(', ')}. После удаления они станут доступны всем авторизованным.`
    : `Удалить роль «${role.key}»? Она снимется у всех пользователей.`
  if (!window.confirm(warning)) return
  saving.value = true
  error.value = null
  try {
    await api.deleteAccessRole(role.id)
    notice.value = `Роль ${role.key} удалена`
    await load()
  } catch (e) {
    error.value = e.message
  } finally {
    saving.value = false
  }
}

onMounted(load)
</script>

<style scoped>
.roles-page {
  padding: 16px 20px 20px;
  max-width: 1040px;
  overflow: auto;
  height: 100%;
  color: var(--rep-text);
}
h1 {
  font-size: 18px;
  margin: 0 0 6px;
}
h2 {
  font-size: 14px;
  margin: 0 0 8px;
}
.muted {
  color: var(--rep-text-muted);
  font-size: 12.5px;
  margin: 0 0 12px;
}
.err {
  color: var(--rep-danger, #8c1f1f);
  font-size: 13px;
}
.ok {
  color: var(--rep-success, #1f6f3f);
  font-size: 13px;
}
.list {
  width: 100%;
  border-collapse: collapse;
  font-size: 13px;
  margin-bottom: 18px;
}
.list th {
  text-align: left;
  font-weight: 600;
  font-size: 12px;
  color: var(--rep-text-muted);
  border-bottom: 1px solid var(--rep-border);
  padding: 6px 8px;
}
.list td {
  border-bottom: 1px solid var(--rep-border);
  padding: 6px 8px;
  vertical-align: middle;
}
.actions {
  display: flex;
  gap: 6px;
  justify-content: flex-end;
}
.count {
  border: none;
  background: none;
  padding: 0;
  font: inherit;
  color: var(--rep-accent);
  text-decoration: underline dotted;
  cursor: help;
}
.inline-input {
  font: inherit;
  padding: 3px 6px;
  border: 1px solid var(--rep-border);
  border-radius: 5px;
  background: var(--rep-surface);
  color: var(--rep-text);
}
.form {
  display: flex;
  flex-direction: column;
  gap: 8px;
  max-width: 460px;
}
.form label {
  display: flex;
  flex-direction: column;
  gap: 4px;
  font-size: 12.5px;
  color: var(--rep-text-muted);
}
.form input {
  font: inherit;
  padding: 6px 8px;
  border: 1px solid var(--rep-border);
  border-radius: 6px;
  background: var(--rep-surface);
  color: var(--rep-text);
}
.hint {
  font-size: 11.5px;
  color: var(--rep-text-muted);
}
.tool {
  font: inherit;
  font-size: 12px;
  padding: 5px 10px;
  border: 1px solid var(--rep-border);
  border-radius: 6px;
  background: var(--rep-surface);
  color: var(--rep-text);
  cursor: pointer;
}
.tool:hover {
  background: var(--rep-surface-hover);
}
.tool.primary {
  border-color: var(--rep-accent);
  color: var(--rep-accent);
  align-self: flex-start;
}
.tool.danger {
  color: var(--rep-danger, #8c1f1f);
}
.tool:disabled {
  opacity: 0.6;
  cursor: default;
}
</style>
