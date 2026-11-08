// Сессия пользователя (ADR-34): /api/auth/me и редирект на вход.
// Сервер всё равно проверяет права на каждом запросе — здесь только UX.
// Пути строятся от API-префикса и базы приложения (ADR-53): приложение живёт
// и в корне, и под /reports/ за реверс-прокси.
import { API_BASE } from '../api/client.js'

// абсолютный адрес страницы входа: работает и в корне, и под любым префиксом
const LOGIN_PAGE = new URL('login.html', document.baseURI).href

export async function me() {
  try {
    const res = await fetch(`${API_BASE}/api/auth/me`, { headers: { Accept: 'application/json' } })
    if (res.status === 401) return null
    if (!res.ok) return { auth: false }
    return await res.json()
  } catch {
    return { auth: false }
  }
}

export async function logout() {
  await fetch(`${API_BASE}/api/auth/logout`, { method: 'POST' })
  location.replace(LOGIN_PAGE)
}

// Возвращает сессию или уводит на страницу входа (если сервер требует авторизацию).
export async function ensureSession() {
  const session = await me()
  if (session === null) {
    const next = encodeURIComponent(location.pathname + location.search)
    location.replace(`${LOGIN_PAGE}?next=${next}`)
    return null
  }
  return session
}
