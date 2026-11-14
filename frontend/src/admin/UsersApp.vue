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
          <th>Роли доступа</th>
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
          <td class="roles" :data-roles="(u.roles || []).join(',')">
            <label v-for="r in roles" :key="r.key" class="role-check">
              <input
                type="checkbox"
                :value="r.key"
                :checked="(u.roles || []).includes(r.key)"
                :disabled="busy === u.id"
                @change="toggleRole(u, r.key, $event.target.checked)"
              />
              <span :title="r.title">{{ r.key }}</span>
            </label>
            <span v-if="!roles.length" class="muted small">ролей нет</span>
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

    <section class="roles-admin">
      <h2>Роли доступа</h2>
      <p class="muted small">
        Роли указываются в шаблоне отчёта (<code>accessRoles</code>) и выдаются пользователям.
        Пустой список у отчёта — доступ всем авторизованным; администратор видит всё.
      </p>
      <ul class="role-list">
        <li v-for="r in roles" :key="r.id">
          <b>{{ r.key }}</b> — {{ r.title }}
          <button type="button" class="tool danger" :disabled="saving" @click="removeRole(r)">Удалить</button>
        </li>
        <li v-if="!roles.length" class="muted small">ролей пока нет</li>
      </ul>
      <form class="role-new" @submit.prevent="addRole">
        <input v-model.trim="newRole.key" placeholder="finance" autocomplete="off" />
        <input v-model.trim="newRole.title" placeholder="Финансы" autocomplete="off" />
        <button type="submit" class="tool primary" :disabled="saving">Добавить роль</button>
      </form>
    </section>

    <form class="form" @submit.prevent="create">
      <h2>Новый пользователь</h2>
      <label>
        Логин
        <input v-model="form.login" placeholder="analyst_1 или CORP\ivanov" required />
        <span class="hint">
          латинские буквы в любом регистре, цифры, «_», «.», «-», «\», «@»; от 3 до 128 символов.
          Доменная учётная запись — в виде CORP\ivanov или ivanov@corp.example
        </span>
      </label>
      <label>
        Роль
        <select v-model="form.role">
          <option value="viewer">viewer</option>
          <option value="author">author</option>
          <option value="admin">admin</option>
        </select>
      </label>
      <label v-if="roles.length">
        Роли доступа
        <span class="roles form-roles">
          <label v-for="r in roles" :key="r.key" class="role-check">
            <input type="checkbox" :value="r.key" v-model="form.roles" />
            <span :title="r.title">{{ r.key }}</span>
          </label>
        </span>
      </label>
      <label>
        Пароль
        <input v-model="form.password" type="password" autocomplete="new-password" />
        <span class="hint">
          не короче 8 символов; пусто — учётная запись без пароля, вход только через доверенный
          прокси-сервер (доменная аутентификация)
        </span>
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

const form = reactive({ login: '', role: 'viewer', password: '', roles: [] })
const roles = ref([])
const newRole = reactive({ key: '', title: '' })

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

async function addRole() {
  saving.value = true
  error.value = null
  notice.value = null
  try {
    await api.createAccessRole({ key: newRole.key.trim(), title: newRole.title.trim() })
    notice.value = `Роль ${newRole.key} добавлена`
    newRole.key = ''
    newRole.title = ''
    await loadRoles()
    await load()
  } catch (e) {
    error.value = e.message
  } finally {
    saving.value = false
  }
}

async function removeRole(role) {
  if (!window.confirm(`Удалить роль «${role.key}»? Она снимется у всех пользователей.`)) return
  error.value = null
  try {
    await api.deleteAccessRole(role.id)
    notice.value = `Роль ${role.key} удалена`
    await loadRoles()
    await load()
  } catch (e) {
    error.value = e.message
  }
}

// роли пользователя (ADR-102): меняются отдельно от системной роли
async function toggleRole(user, key, on) {
  const next = new Set(user.roles || [])
  if (on) next.add(key)
  else next.delete(key)
  busy.value = user.id
  error.value = null
  try {
    await api.updateUser(user.id, { role: user.role, enabled: user.enabled, roles: [...next] })
    notice.value = `Роли ${user.login}: ${[...next].join(', ') || 'нет'}`
    await load()
  } catch (e) {
    error.value = e.message
    await load()
  } finally {
    busy.value = null
  }
}

async function create() {
  saving.value = true
  error.value = null
  notice.value = null
  try {
    await api.createUser({
      login: form.login.trim(),
      role: form.role,
      password: form.password,
      roles: [...form.roles]
    })
    notice.value = `Пользователь ${form.login} создан`
    form.login = ''
    form.password = ''
    form.role = 'viewer'
    form.roles = []
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

onMounted(async () => {
  await loadRoles()
  await load()
})
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

/* Роли доступа (ADR-102) */
.roles-admin {
  margin: 18px 0 8px;
}
.role-list {
  list-style: none;
  padding: 0;
  margin: 8px 0;
}
.role-list li {
  display: flex;
  gap: 10px;
  align-items: center;
  padding: 4px 0;
}
.role-new {
  display: flex;
  gap: 8px;
  flex-wrap: wrap;
}
.role-new input {
  min-width: 180px;
}
td.roles {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
}
.role-check {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  font-size: 12px;
  white-space: nowrap;
}
</style>
