<template>
  <div class="wrap">
    <div v-if="proxyMode" class="card">
      <h1>Сервер отчётов</h1>
      <p class="sub">Вход выполняется на реверс-прокси</p>
      <p class="hint">
        Сервер работает в режиме <code>REP_AUTH=proxy</code>: имя пользователя приходит от прокси
        в HTTP-заголовке. Вход и выход выполняются средствами прокси, отдельного пароля у приложения нет.
      </p>
      <p v-if="proxyError" class="error">{{ proxyError }}</p>
    </div>

    <form v-else class="card" @submit.prevent="submit">
      <h1>Сервер отчётов</h1>
      <p class="sub">Вход в систему</p>

      <label>Логин
        <input v-model="login" type="text" autocomplete="username" autofocus required />
      </label>
      <label>Пароль
        <input v-model="password" type="password" autocomplete="current-password" required />
      </label>

      <p v-if="error" class="error">{{ error }}</p>
      <button type="submit" :disabled="busy">{{ busy ? 'Проверка…' : 'Войти' }}</button>
      <p class="hint">Пользователей создаёт <code>scripts/auth-init.sh</code>.</p>
    </form>
  </div>
</template>

<script setup>
// Страница входа (ADR-34): cookie rep_session ставит сервер, роль приходит в ответе.
import { onMounted, ref } from 'vue'
import { API_BASE } from '../api/client.js'

const login = ref('')
const password = ref('')
const busy = ref(false)
const error = ref('')
const proxyMode = ref(false)
const proxyError = ref('')

// В режиме прокси формы быть не должно: приложение всё равно не примет пароль.
onMounted(async () => {
  try {
    const res = await fetch(`${API_BASE}/api/health`)
    const health = await res.json()
    proxyMode.value = health.authMode === 'proxy'
    if (proxyMode.value) {
      const me = await fetch(`${API_BASE}/api/auth/me`)
      if (me.status === 401 || me.status === 403) {
        proxyError.value =
          'Прокси не передал имя пользователя. Проверьте настройку заголовка и списка доверенных прокси.'
      }
    }
  } catch {
    // сервер недоступен — покажем обычную форму, submit сообщит об ошибке
  }
})

function nextUrl() {
  const next = new URLSearchParams(location.search).get('next')
  if (!next || !next.startsWith('/') || next.startsWith('//')) {
    return new URL('.', document.baseURI).pathname
  }
  return next
}

async function submit() {
  if (busy.value) return
  busy.value = true
  error.value = ''
  try {
    const res = await fetch(`${API_BASE}/api/auth/login`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ login: login.value, password: password.value })
    })
    const payload = await res.json().catch(() => ({}))
    if (!res.ok) {
      error.value =
        payload?.error?.code === 'BAD_CREDENTIALS'
          ? 'Неверный логин или пароль'
          : payload?.error?.message || `Не удалось войти (${res.status})`
      return
    }
    location.replace(nextUrl())
  } catch (e) {
    error.value = 'Сервер недоступен: ' + e.message
  } finally {
    busy.value = false
  }
}
</script>

<style>
body {
  margin: 0;
  font-family: system-ui, -apple-system, 'Segoe UI', Roboto, sans-serif;
  background: var(--rep-bg);
  color: var(--rep-text);
}
</style>

<style scoped>
.wrap {
  min-height: 100vh;
  display: grid;
  place-items: center;
  padding: 24px;
}
.card {
  width: 340px;
  display: grid;
  gap: 12px;
  background: var(--rep-surface);
  border: 1px solid var(--rep-border);
  border-radius: 12px;
  padding: 24px;
}
h1 {
  margin: 0;
  font-size: 20px;
}
.sub {
  margin: -6px 0 6px;
  color: var(--rep-text-muted);
  font-size: 13px;
}
label {
  display: grid;
  gap: 4px;
  font-size: 12px;
  color: var(--rep-text-muted);
}
input {
  font: inherit;
  font-size: 14px;
  padding: 8px 10px;
  border: 1px solid var(--rep-border-strong);
  border-radius: 8px;
}
button {
  font: inherit;
  font-size: 14px;
  padding: 9px 12px;
  border: 0;
  border-radius: 8px;
  background: var(--rep-accent-hover);
  color: var(--rep-surface);
  cursor: pointer;
}
button:disabled {
  opacity: 0.6;
  cursor: default;
}
.error {
  margin: 0;
  font-size: 13px;
  color: var(--rep-danger-strong);
  background: var(--rep-danger-bg);
  border: 1px solid var(--rep-danger-bg);
  border-radius: 8px;
  padding: 8px 10px;
}
.hint {
  margin: 0;
  font-size: 11px;
  color: var(--rep-text-faint);
}
code {
  background: var(--rep-bg);
  padding: 1px 4px;
  border-radius: 4px;
}
</style>
