<template>
  <div class="wrap">
    <form class="card" @submit.prevent="submit">
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
import { ref } from 'vue'

const login = ref('')
const password = ref('')
const busy = ref(false)
const error = ref('')

function nextUrl() {
  const next = new URLSearchParams(location.search).get('next')
  if (!next || !next.startsWith('/') || next.startsWith('//')) return '/'
  return next
}

async function submit() {
  if (busy.value) return
  busy.value = true
  error.value = ''
  try {
    const res = await fetch('/api/auth/login', {
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
  background: #f6f8fb;
  color: #1c1f23;
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
  background: #fff;
  border: 1px solid #dfe3e8;
  border-radius: 12px;
  padding: 24px;
}
h1 {
  margin: 0;
  font-size: 20px;
}
.sub {
  margin: -6px 0 6px;
  color: #6b7684;
  font-size: 13px;
}
label {
  display: grid;
  gap: 4px;
  font-size: 12px;
  color: #4a5361;
}
input {
  font: inherit;
  font-size: 14px;
  padding: 8px 10px;
  border: 1px solid #cfd6de;
  border-radius: 8px;
}
button {
  font: inherit;
  font-size: 14px;
  padding: 9px 12px;
  border: 0;
  border-radius: 8px;
  background: #17427f;
  color: #fff;
  cursor: pointer;
}
button:disabled {
  opacity: 0.6;
  cursor: default;
}
.error {
  margin: 0;
  font-size: 13px;
  color: #8c1f1f;
  background: #fdeaea;
  border: 1px solid #f3cfcf;
  border-radius: 8px;
  padding: 8px 10px;
}
.hint {
  margin: 0;
  font-size: 11px;
  color: #8b97a6;
}
code {
  background: #f2f5f9;
  padding: 1px 4px;
  border-radius: 4px;
}
</style>
