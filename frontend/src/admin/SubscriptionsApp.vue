<template>
  <div class="subs">
    <h1>Рассылка отчётов</h1>
    <p class="muted">
      Подписка отправляет CSV-файл отчёта на почту по расписанию (cron, время сервера).
      Отправку выполняет сервер; ручная кнопка не сдвигает расписание.
    </p>

    <p v-if="error" class="err">{{ error }}</p>
    <p v-if="notice" class="ok">{{ notice }}</p>

    <table class="list" v-if="items.length">
      <thead>
        <tr>
          <th>Отчёт</th>
          <th>Расписание</th>
          <th>Получатели</th>
          <th>Следующий</th>
          <th>Последний</th>
          <th></th>
        </tr>
      </thead>
      <tbody>
        <tr v-for="s in items" :key="s.id" :class="{ off: !s.enabled }" :data-sub="s.id">
          <td>
            <b>{{ s.title }}</b>
            <div class="muted small">{{ s.reportId }} · {{ s.format }}</div>
          </td>
          <td><code>{{ s.cron }}</code></td>
          <td class="small">{{ s.recipients.join(', ') }}</td>
          <td class="small">{{ formatDate(s.nextRunAt) || '—' }}</td>
          <td class="small">
            <span v-if="s.lastRunAt">{{ formatDate(s.lastRunAt) }}</span>
            <span v-else class="muted">ещё не было</span>
            <div v-if="s.lastOk === false" class="err small">{{ s.lastError }}</div>
          </td>
          <td class="actions">
            <button type="button" class="tool" :disabled="busy === s.id" @click="run(s)">Отправить сейчас</button>
            <button type="button" class="tool" :disabled="busy === s.id" @click="toggle(s)">
              {{ s.enabled ? 'Выключить' : 'Включить' }}
            </button>
            <button type="button" class="tool danger" :disabled="busy === s.id" @click="remove(s)">Удалить</button>
          </td>
        </tr>
      </tbody>
    </table>
    <p v-else class="muted">Подписок пока нет — добавьте первую ниже.</p>

    <form class="form" @submit.prevent="save">
      <h2>{{ form.id ? 'Изменить подписку' : 'Новая подписка' }}</h2>
      <label>
        Отчёт
        <select v-model="form.reportId" required>
          <option value="" disabled>выберите отчёт</option>
          <option v-for="r in reports" :key="r.id" :value="r.id">{{ r.title }} ({{ r.id }})</option>
        </select>
      </label>
      <label>
        Название
        <input v-model="form.title" placeholder="как подписывать письмо" />
      </label>
      <label>
        Расписание (cron)
        <input v-model="form.cron" placeholder="0 9 * * 1-5" required />
        <span class="hint">минута час день месяц день_недели; «0 9 * * 1-5» — будни в 9:00</span>
      </label>
      <label>
        Получатели
        <input v-model="form.recipients" placeholder="po@example.test, analyst@example.test" required />
        <span class="hint">через запятую</span>
      </label>
      <label>
        Параметры отчёта (JSON)
        <textarea v-model="form.params" rows="4" spellcheck="false"></textarea>
        <span class="hint">например {"from":"2026-03-01","to":"2026-03-31"}</span>
      </label>
      <label class="inline">
        <input type="checkbox" v-model="form.enabled" />
        включена
      </label>
      <div class="buttons">
        <button type="submit" class="tool primary" :disabled="saving">Сохранить</button>
        <button type="button" class="tool" v-if="form.id" @click="reset">Отмена</button>
      </div>
    </form>
  </div>
</template>

<script setup>
import { onMounted, reactive, ref } from 'vue'
import { api } from '../api/client.js'

const items = ref([])
const reports = ref([])
const error = ref(null)
const notice = ref(null)
const busy = ref(null)
const saving = ref(false)

const emptyForm = () => ({ id: null, reportId: '', title: '', cron: '0 9 * * 1-5', recipients: '', params: '{}', enabled: true })
const form = reactive(emptyForm())

function reset() {
  Object.assign(form, emptyForm())
}

function formatDate(value) {
  if (!value) return ''
  const date = new Date(value)
  return Number.isNaN(date.getTime()) ? value : date.toLocaleString('ru-RU')
}

async function load() {
  error.value = null
  try {
    const data = await api.subscriptions()
    items.value = data.items || []
  } catch (e) {
    error.value = e.message
  }
}

function parseForm() {
  let params
  try {
    params = JSON.parse(form.params || '{}')
  } catch (e) {
    throw new Error('параметры должны быть корректным JSON')
  }
  const recipients = form.recipients
    .split(',')
    .map((s) => s.trim())
    .filter(Boolean)
  if (!recipients.length) throw new Error('укажите хотя бы одного получателя')
  return {
    reportId: form.reportId,
    title: form.title || form.reportId,
    cron: form.cron.trim(),
    params,
    recipients,
    format: 'csv',
    enabled: form.enabled
  }
}

async function save() {
  saving.value = true
  error.value = null
  notice.value = null
  try {
    const payload = parseForm()
    if (form.id) {
      await api.updateSubscription(form.id, payload)
      notice.value = 'Подписка обновлена'
    } else {
      await api.createSubscription(payload)
      notice.value = 'Подписка создана'
    }
    reset()
    await load()
  } catch (e) {
    error.value = e.message
  } finally {
    saving.value = false
  }
}

async function run(s) {
  busy.value = s.id
  error.value = null
  notice.value = null
  try {
    await api.runSubscription(s.id)
    notice.value = 'Отправка запущена — статус появится в журнале через несколько секунд'
    setTimeout(load, 3000)
  } catch (e) {
    error.value = e.message
  } finally {
    busy.value = null
  }
}

async function toggle(s) {
  busy.value = s.id
  try {
    await api.updateSubscription(s.id, {
      reportId: s.reportId,
      title: s.title,
      cron: s.cron,
      params: s.params,
      recipients: s.recipients,
      format: s.format,
      enabled: !s.enabled
    })
    await load()
  } catch (e) {
    error.value = e.message
  } finally {
    busy.value = null
  }
}

async function remove(s) {
  if (!window.confirm(`Удалить подписку «${s.title}»?`)) return
  busy.value = s.id
  try {
    await api.deleteSubscription(s.id)
    await load()
  } catch (e) {
    error.value = e.message
  } finally {
    busy.value = null
  }
}

function edit(s) {
  Object.assign(form, {
    id: s.id,
    reportId: s.reportId,
    title: s.title,
    cron: s.cron,
    recipients: s.recipients.join(', '),
    params: JSON.stringify(s.params || {}, null, 0),
    enabled: s.enabled
  })
}

onMounted(async () => {
  await load()
  try {
    reports.value = await api.reports()
  } catch (e) {
    error.value = e.message
  }
})
</script>

<style scoped>
.subs {
  max-width: 1100px;
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
  vertical-align: top;
}
.list th {
  background: #f7f9fb;
  font-weight: 600;
  font-size: 12px;
  text-transform: uppercase;
  letter-spacing: 0.02em;
}
.list tr.off {
  opacity: 0.55;
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
  max-width: 640px;
}
label {
  display: grid;
  gap: 4px;
  font-size: 13px;
}
label.inline {
  display: flex;
  align-items: center;
  gap: 6px;
}
input,
select,
textarea {
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
