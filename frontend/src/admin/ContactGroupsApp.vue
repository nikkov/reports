<template>
  <!-- Группы адресатов (ADR-179): именованный список адресов для подписок. -->
  <div class="groups-page">
    <header class="page-head">
      <h1>Адресаты</h1>
      <button type="button" class="tool primary" data-open-create @click="openCreate">
        Добавить группу
      </button>
    </header>
    <p class="muted">
      Группа — имя для набора адресов. В подписке указывается группа, а не список адресов: правка
      состава здесь сразу меняет получателей всех подписок с этой группой.
    </p>

    <p v-if="error" class="err">{{ error }}</p>
    <p v-if="notice" class="ok">{{ notice }}</p>

    <div class="filters" data-filters>
      <label class="f-search">
        <span>Ключ, название или адрес</span>
        <input v-model.trim="filters.q" placeholder="finance, финансы, po@…" autocomplete="off" data-filter-q />
      </label>
      <button v-if="filters.q" type="button" class="link" data-reset-filters @click="filters.q = ''">
        сбросить
      </button>
      <span class="muted small counter" data-counter>показано {{ filtered.length }} из {{ items.length }}</span>
    </div>

    <table class="list" v-if="filtered.length" data-groups-table>
      <thead>
        <tr>
          <th>Ключ</th>
          <th>Название</th>
          <th>Адресов</th>
          <th>Подписок</th>
          <th></th>
        </tr>
      </thead>
      <tbody>
        <tr v-for="g in filtered" :key="g.id" :data-group="g.key">
          <td><code class="key">{{ g.key }}</code></td>
          <td class="title">{{ g.title }}</td>
          <td>
            <span v-if="g.emails.length" class="count" :title="g.emails.join(', ')">{{ g.emails.length }}</span>
            <span v-else class="muted">0</span>
          </td>
          <td>
            <span v-if="g.subscriptions" :title="`используется в ${g.subscriptions} подписке(ах)`">
              {{ g.subscriptions }}
            </span>
            <span v-else class="muted">0</span>
          </td>
          <td class="actions">
            <button type="button" class="tool" :disabled="busy === g.id" @click="openEdit(g)">
              Изменить
            </button>
            <button type="button" class="tool danger" :disabled="busy === g.id" @click="remove(g)">
              Удалить
            </button>
          </td>
        </tr>
      </tbody>
    </table>
    <p v-else-if="items.length" class="muted">Ничего не нашли — измените фильтр.</p>
    <p v-else class="muted">Групп пока нет — добавьте первую.</p>

    <div v-if="modal" class="overlay" @click.self="closeModal">
      <form class="dialog" @submit.prevent="submit">
        <h3>{{ modal === 'create' ? 'Новая группа адресатов' : `Группа ${edit.key}` }}</h3>

        <label v-if="modal === 'create'">
          Ключ
          <input v-model.trim="form.key" placeholder="finance" autocomplete="off" data-form-key />
          <span class="hint">
            строчная латиница, цифры, «_» и «-», от 2 до 32 символов. Ключ указывается в подписке и
            после создания не меняется.
          </span>
        </label>
        <p v-else class="form-meta"><code class="key">{{ edit.key }}</code></p>

        <label>
          Название
          <input v-model.trim="form.title" placeholder="Финансовый отдел" autocomplete="off" data-form-title />
        </label>

        <label>
          Адреса
          <textarea
            v-model="form.emails"
            rows="6"
            spellcheck="false"
            placeholder="po@example.test&#10;analyst@example.test"
            data-form-emails
          ></textarea>
          <span class="hint">
            по одному в строке (или через запятую); сейчас адресов: {{ parsedEmails.length }}
          </span>
        </label>

        <p v-if="modal === 'edit' && edit.subscriptions" class="usage">
          Группа используется в подписках: <b>{{ edit.subscriptions }}</b> — удалить её нельзя, пока они
          не переключены на другую группу или адреса.
        </p>

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
const error = ref(null)
const notice = ref(null)
const saving = ref(false)
const busy = ref(null)

const filters = reactive({ q: '' })

const modal = ref(null)
const edit = ref(null)
const modalError = ref(null)
const form = reactive({ key: '', title: '', emails: '' })

// адреса вводятся построчно или через запятую — разбираем оба варианта
const parsedEmails = computed(() =>
  form.emails
    .split(/[\n,;]+/)
    .map((s) => s.trim())
    .filter(Boolean)
)

const filtered = computed(() => {
  const q = filters.q.toLowerCase()
  if (!q) return items.value
  return items.value.filter(
    (g) =>
      g.key.toLowerCase().includes(q) ||
      (g.title || '').toLowerCase().includes(q) ||
      (g.emails || []).some((e) => e.toLowerCase().includes(q))
  )
})

async function load() {
  error.value = null
  try {
    const data = await api.contactGroups()
    items.value = data.items || []
  } catch (e) {
    error.value = e.message
  }
}

function openCreate() {
  modalError.value = null
  edit.value = null
  Object.assign(form, { key: '', title: '', emails: '' })
  modal.value = 'create'
}

function openEdit(group) {
  modalError.value = null
  edit.value = group
  Object.assign(form, { key: group.key, title: group.title, emails: (group.emails || []).join('\n') })
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
  const emails = parsedEmails.value
  try {
    if (!emails.length) throw new Error('добавьте хотя бы один адрес')
    if (modal.value === 'create') {
      await api.createContactGroup({ key: form.key.trim(), title: form.title.trim(), emails })
      notice.value = `Группа ${form.key} создана (${emails.length} адр.)`
    } else {
      await api.updateContactGroup(edit.value.id, { title: form.title.trim(), emails })
      notice.value = `Группа ${edit.value.key} сохранена (${emails.length} адр.)`
    }
    await load()
    closeModal()
  } catch (e) {
    modalError.value = e.message
  } finally {
    saving.value = false
  }
}

async function remove(group) {
  const warning = group.subscriptions
    ? `Группа «${group.key}» используется в подписках (${group.subscriptions}). Удалить нельзя, пока подписки на неё ссылаются.`
    : `Удалить группу «${group.key}» (${group.emails.length} адр.)?`
  if (!window.confirm(warning)) return
  busy.value = group.id
  error.value = null
  try {
    await api.deleteContactGroup(group.id)
    notice.value = `Группа ${group.key} удалена`
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
.groups-page {
  padding: 16px 20px 48px;
  width: 100%;
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
  min-width: 260px;
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
  color: var(--rep-focus);
  text-decoration: underline dotted;
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
.dialog textarea {
  box-sizing: border-box;
  width: 100%;
  padding: 5px 8px;
  border: 1px solid var(--rep-border-input);
  border-radius: 6px;
  font: inherit;
  font-size: 13px;
  background: var(--rep-surface);
  color: var(--rep-text);
}
.dialog textarea {
  font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
  font-size: 12px;
  resize: vertical;
}
.form-meta {
  margin: 0;
}
.usage {
  font-size: 12px;
  color: var(--rep-text-muted);
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
