// Сессия пользователя (ADR-34): /api/auth/me и редирект на вход.
// Сервер всё равно проверяет права на каждом запросе — здесь только UX.
export async function me() {
  try {
    const res = await fetch('/api/auth/me', { headers: { Accept: 'application/json' } })
    if (res.status === 401) return null
    if (!res.ok) return { auth: false }
    return await res.json()
  } catch {
    return { auth: false }
  }
}

export async function logout() {
  await fetch('/api/auth/logout', { method: 'POST' })
  location.replace('/login.html')
}

// Возвращает сессию или уводит на страницу входа (если сервер требует авторизацию).
export async function ensureSession() {
  const session = await me()
  if (session === null) {
    const next = encodeURIComponent(location.pathname + location.search)
    location.replace(`/login.html?next=${next}`)
    return null
  }
  return session
}
