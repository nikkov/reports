<template>
  <div class="subs">
    <h1>Рассылка отчётов</h1>
    <p class="muted">
      Подписка отправляет файл отчёта (CSV или XLSX) на почту по расписанию. Время cron считается
      в часовом поясе подписки, а не сервера. При сбое SMTP сервер сам повторяет отправку
      с растущей паузой; ручная кнопка не сдвигает расписание.
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
        <template v-for="s in items" :key="s.id">
        <tr :class="{ off: !s.enabled }" :data-sub="s.id">
          <td>
            <b>{{ s.title }}</b>
            <div class="muted small">{{ s.reportId }} · {{ s.format }} · {{ s.timezone }}</div>
          </td>
          <td>
            <code>{{ s.cron }}</code>
            <div v-if="s.retryAt" class="retry small" :data-retry="s.id">
              повтор {{ formatDate(s.retryAt) }} (попытка {{ s.retryCount + 1 }})
            </div>
          </td>
          <td class="small">{{ s.recipients.join(', ') }}</td>
          <td class="small">{{ formatDate(s.nextRunAt) || '—' }}</td>
          <td class="small">
            <span v-if="s.lastRunAt">{{ formatDate(s.lastRunAt) }}</span>
            <span v-else class="muted">ещё не было</span>
            <div v-if="s.lastOk === false" class="err small">{{ s.lastError }}</div>
          </td>
          <td class="actions">
            <button type="button" class="tool" :disabled="busy === s.id" @click="run(s)">Отправить сейчас</button>
            <button type="button" class="tool" @click="openJournal(s)">
              {{ journalId === s.id ? 'Скрыть журнал' : 'Журнал' }}
            </button>
            <button type="button" class="tool" :disabled="busy === s.id" @click="toggle(s)">
              {{ s.enabled ? 'Выключить' : 'Включить' }}
            </button>
            <button type="button" class="tool danger" :disabled="busy === s.id" @click="remove(s)">Удалить</button>
          </td>
        </tr>
        <tr v-if="journalId === s.id" class="journal-row">
          <td colspan="6">
            <div class="journal">
              <div v-if="journalLoading" class="muted small">загружаю журнал…</div>
              <table v-else-if="journal.length" class="runs">
                <thead>
                  <tr>
                    <th>Когда</th>
                    <th>Результат</th>
                    <th>Строк</th>
                    <th>Писем</th>
                    <th>Длительность</th>
                    <th>Ошибка</th>
                  </tr>
                </thead>
                <tbody>
                  <tr v-for="(r, i) in journal" :key="i" :class="{ bad: !r.ok }">
                    <td class="small">{{ formatDate(r.startedAt) }}</td>
                    <td class="small">{{ r.ok ? 'отправлено' : 'ошибка' }}</td>
                    <td class="small">{{ r.rows }}</td>
                    <td class="small">{{ r.recipients }}</td>
                    <td class="small">{{ r.durationMs }} мс</td>
                    <td class="small err">{{ r.error || '—' }}</td>
                  </tr>
                </tbody>
              </table>
              <p v-else class="muted small">Отправок ещё не было.</p>
            </div>
          </td>
        </tr>
        </template>
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
        Часовой пояс расписания
        <input v-model="form.timezone" list="tz-list" placeholder="Europe/Moscow" required />
        <datalist id="tz-list">
          <option value="UTC" />
          <option value="Europe/Moscow" />
          <option value="Europe/Kaliningrad" />
          <option value="Europe/Yekaterinburg" />
          <option value="Asia/Novosibirsk" />
          <option value="Asia/Vladivostok" />
        </datalist>
        <span class="hint">IANA-идентификатор: «0 9 * * 1-5» + Europe/Moscow = 9:00 по Москве</span>
      </label>
      <label>
        Получатели
        <input v-model="form.recipients" placeholder="po@example.test, analyst@example.test" required />
        <span class="hint">через запятую</span>
      </label>
      <label>
        Формат вложения
        <select v-model="form.format">
          <option value="csv">CSV (Excel открывает двойным щелчком)</option>
          <option value="xlsx">XLSX (книга Excel)</option>
        </select>
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
const journalId = ref(null)
const journal = ref([])
const journalLoading = ref(false)

const emptyForm = () => ({
  id: null,
  reportId: '',
  title: '',
  cron: '0 9 * * 1-5',
  timezone: 'UTC',
  recipients: '',
  params: '{}',
  format: 'csv',
  enabled: true
})
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
    timezone: form.timezone.trim() || 'UTC',
    params,
    recipients,
    format: form.format,
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

async function loadJournal(id) {
  journalLoading.value = true
  try {
    journal.value = (await api.subscriptionRuns(id)).items || []
  } catch (e) {
    error.value = e.message
    journal.value = []
  } finally {
    journalLoading.value = false
  }
}

async function openJournal(s) {
  if (journalId.value === s.id) {
    journalId.value = null
    journal.value = []
    return
  }
  journalId.value = s.id
  await loadJournal(s.id)
}

async function run(s) {
  busy.value = s.id
  error.value = null
  notice.value = null
  try {
    const before = journalId.value === s.id ? journal.value.length : 0
    if (journalId.value !== s.id) {
      journalId.value = s.id
      await loadJournal(s.id)
    }
    await api.runSubscription(s.id)
    notice.value = 'Отправка запущена — жду результат в журнале…'
    // сервер отправляет в отдельном потоке: опрашиваем журнал, пока не появится новая запись
    for (let attempt = 0; attempt < 10; attempt += 1) {
      await new Promise((r) => setTimeout(r, 1200))
      await loadJournal(s.id)
      if (journal.value.length > before) {
        const latest = journal.value[0]
        notice.value = latest.ok
          ? `Отправлено: строк ${latest.rows}, писем ${latest.recipients}, ${latest.durationMs} мс`
          : `Отправка не удалась: ${latest.error}`
        break
      }
    }
    await load()
  } catch (e) {
    error.value = e.message
  } finally {
    busy.value = false
  }
}

async function toggle(s) {
  busy.value = s.id
  try {
    await api.updateSubscription(s.id, {
      reportId: s.reportId,
      title: s.title,
      cron: s.cron,
      timezone: s.timezone,
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
    timezone: s.timezone || 'UTC',
    recipients: s.recipients.join(', '),
    params: JSON.stringify(s.params || {}, null, 0),
    format: s.format || 'csv',
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
.retry {
  color: #8a5a00;
  background: #fff4dd;
  border-radius: 4px;
  padding: 1px 5px;
  margin-top: 3px;
  display: inline-block;
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
.journal-row td {
  background: #f7f9fb;
}
.runs {
  width: 100%;
  border-collapse: collapse;
  font-size: 12px;
}
.runs th,
.runs td {
  text-align: left;
  padding: 4px 8px;
  border-bottom: 1px solid #e6ebf0;
}
.runs tr.bad td {
  color: #b3261e;
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
