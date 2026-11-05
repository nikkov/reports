// Тонкий клиент API отчётов. Все запросы отменяемы (AbortController), ошибки типизированы.
const BASE = import.meta.env.VITE_API_BASE || ''

async function request(path, options = {}) {
  const resp = await fetch(`${BASE}${path}`, {
    headers: { 'Content-Type': 'application/json' },
    ...options
  })
  const text = await resp.text()
  let body = null
  try {
    body = text ? JSON.parse(text) : null
  } catch {
    body = null
  }
  if (!resp.ok) {
    const error = new Error(body?.error?.message || `HTTP ${resp.status}`)
    error.code = body?.error?.code || 'HTTP_' + resp.status
    error.status = resp.status
    throw error
  }
  return body
}

export const api = {
  health: () => request('/api/health'),
  reports: () => request('/api/reports'),
  template: (id) => request(`/api/reports/${encodeURIComponent(id)}`),
  // справочник параметра: SQL живёт в шаблоне, адрес — отчёт + имя параметра (ADR-24)
  lookups: (reportId, param, params = {}, signal) => {
    const q = new URLSearchParams()
    for (const [k, v] of Object.entries(params)) {
      if (v !== undefined && v !== null && v !== '') q.set(k, String(v))
    }
    const suffix = q.toString() ? `?${q}` : ''
    return request(
      `/api/reports/${encodeURIComponent(reportId)}/lookups/${encodeURIComponent(param)}${suffix}`,
      { signal }
    )
  },
  data: (reportId, params, signal) =>
    request('/api/report/data', {
      method: 'POST',
      body: JSON.stringify({ reportId, params }),
      signal
    })
}
