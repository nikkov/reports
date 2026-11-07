<template>
  <div class="users">
    <h1>Пользователи</h1>
    <p class="muted">
      Роли: <b>viewer</b> — читает отчёты, <b>author</b> — создаёт и правит черновики,
      <b>admin</b> — публикует, удаляет, управляет пользователями и рассылкой.
      Отключение и смена пароля завершают активные сессии пользователя.
    </p>

    <p v-if="error" class="err">{{ error }}</p>
    <p v-if="notice" class="ok">{{ notice }}</p>

    <table class="list" v-if="items.length">
      <thead>
        <tr>
          <th>Логин</th>
          <th>Роль</th>
          <th>Состояние</th>
          <th>Создан</th>
          <th>Изменён</th>
          <th></th>
        </tr>
      </thead>
      <tbody>
        <tr v-for="u in items" :key="u.id" :class="{ off: !u.enabled }" :data-user="u.login">
          <td><b>{{ u.login }}</b><span v-if="u.login === me" class="me"> · это вы</span></td>
          <td>
            <select :value="u.role" :disabled="busy === u.id" @change="changeRole(u, $event.target.value)">
              <option value="viewer">viewer</option>
              <option value="author">author</option>
              <option value="admin">admin</option>
            </select>
          </td>
          <td>
            <span :class="u.enabled ? 'on' : 'off-label'">{{ u.enabled ? 'включён' : 'отключён' }}</span>
          </td>
          <td class="small">{{ formatDate(u.createdAt) }}</td>
          <td class="small">{{ formatDate(u.updatedAt) }}</td>
          <td class="actions">
            <button type="button" class="tool" :disabled="busy === u.id" @click="toggle(u)">
              {{ u.enabled ? 'Отключить' : 'Включить' }}
            </button>
            <button type="button" class="tool" :disabled="busy === u.id" @click="resetPassword(u)">
              Сбросить пароль
            </button>
            <button type="button" class="tool danger" :disabled="busy === u.id" @click="remove(u)">Удалить</button>
          </td>
        </tr>
      </tbody>
    </table>
    <p v-else class="muted">Пользователей нет.</p>

    <form class="form" @submit.prevent="create">
      <h2>Новый пользователь</h2>
      <label>
        Логин
        <input v-model="form.login" placeholder="analyst_1" required />
        <span class="hint">строчные латинские буквы, цифры, «_», «.», «-»; от 3 до 32 символов</span>
      </label>
      <label>
        Роль
        <select v-model="form.role">
          <option value="viewer">viewer</option>
          <option value="author">author</option>
          <option value="admin">admin</option>
        </select>
      </label>
      <label>
        Пароль
        <input v-model="form.password" type="password" autocomplete="new-password" required />
        <span class="hint">не короче 8 символов</span>
      </label>
      <div class="buttons">
        <button type="submit" class="tool primary" :disabled="saving">Создать</button>
      </div>
    </form>
  </div>
</template>

<script setup>
import { onMounted, reactive, ref } from 'vue'
import { api } from '../api/client.js'

const props = defineProps({ session: { type: Object, default: null } })

const items = ref([])
const error = ref(null)
const notice = ref(null)
const busy = ref(null)
const saving = ref(false)
const me = props.session?.login || ''

const form = reactive({ login: '', role: 'viewer', password: '' })

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

async function create() {
  saving.value = true
  error.value = null
  notice.value = null
  try {
    await api.createUser({ login: form.login.trim(), role: form.role, password: form.password })
    notice.value = `Пользователь ${form.login} создан`
    form.login = ''
    form.password = ''
    form.role = 'viewer'
    await load()
  } catch (e) {
    error.value = e.message
  } finally {
    saving.value = false
  }
}

async function update(u, payload, okText) {
  busy.value = u.id
  error.value = null
  notice.value = null
  try {
    await api.updateUser(u.id, {
      role: payload.role ?? u.role,
      enabled: payload.enabled ?? u.enabled,
      password: payload.password ?? ''
    })
    notice.value = okText
    await load()
  } catch (e) {
    error.value = e.message
  } finally {
    busy.value = null
  }
}

function changeRole(u, role) {
  update(u, { role }, `Роль ${u.login}: ${u.role} → ${role}`)
}

function toggle(u) {
  update(u, { enabled: !u.enabled }, u.enabled ? `${u.login} отключён` : `${u.login} включён`)
}

function resetPassword(u) {
  const password = window.prompt(`Новый пароль для ${u.login} (не короче 8 символов):`, '')
  if (!password) return
  update(u, { password }, `Пароль ${u.login} изменён, активные сессии завершены`)
}

async function remove(u) {
  if (!window.confirm(`Удалить пользователя ${u.login}?`)) return
  busy.value = u.id
  try {
    await api.deleteUser(u.id)
    notice.value = `Пользователь ${u.login} удалён`
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
.users {
  max-width: 1000px;
  margin: 0 auto;
  padding: 18px 16px 60px;
  font: 14px/1.45 system-ui, sans-serif;
  color: #1e242c;
}
h1 {
  font-size: 20px;
  margin: 0 0 6px;
}
h2 {
  font-size: 15px;
  margin: 0 0 10px;
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
.ok {
  color: #1b6b3a;
}
.me {
  color: #8b97a6;
  font-size: 12px;
}
.on {
  color: #1b6b3a;
}
.off-label {
  color: #b3261e;
}
.list {
  width: 100%;
  border-collapse: collapse;
  margin: 14px 0 24px;
  background: #fff;
  border: 1px solid #dfe3e8;
}
.list th,
.list td {
  text-align: left;
  padding: 8px 10px;
  border-bottom: 1px solid #eef1f4;
  vertical-align: middle;
}
.list th {
  background: #f7f9fb;
  font-weight: 600;
  font-size: 12px;
  text-transform: uppercase;
  letter-spacing: 0.02em;
}
.list tr.off {
  opacity: 0.6;
}
.actions {
  display: flex;
  gap: 6px;
  flex-wrap: wrap;
}
.tool {
  padding: 5px 10px;
  border: 1px solid #c9d1da;
  background: #fff;
  border-radius: 6px;
  cursor: pointer;
  font-size: 13px;
}
.tool.primary {
  background: #1f6feb;
  border-color: #1f6feb;
  color: #fff;
}
.tool.danger {
  color: #b3261e;
}
.form {
  border: 1px solid #dfe3e8;
  background: #fff;
  padding: 16px;
  display: grid;
  gap: 10px;
  max-width: 480px;
}
label {
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
.hint {
  color: #8b97a6;
  font-size: 12px;
}
.buttons {
  display: flex;
  gap: 8px;
}
</style>
