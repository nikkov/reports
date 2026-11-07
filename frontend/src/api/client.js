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
  // серверная агрегация (ADR-37): GROUP BY по groupBy, агрегаты по measures
  aggregate: (reportId, params, spec, signal, dataset) =>
    request('/api/report/aggregate', {
      method: 'POST',
      body: JSON.stringify({
        reportId,
        params,
        ...(dataset ? { dataset } : {}),
        groupBy: spec.groupBy || [],
        measures: spec.measures || []
      }),
      signal
    }),

  // dataset — ключ из datasets шаблона (ADR-36); без него сервер отдаёт первый набор
  data: (reportId, params, signal, dataset) =>
    request('/api/report/data', {
      method: 'POST',
      body: JSON.stringify(dataset ? { reportId, params, dataset } : { reportId, params }),
      signal
    }),

  // --- администрирование шаблонов (ADR-26) ---
  // каталог с серверным поиском и пагинацией (ADR-41)
  reportsPage: ({ q = '', folder = '', tag = '', limit = 10, offset = 0 } = {}) => {
    const qs = new URLSearchParams()
    if (q) qs.set('q', q)
    if (folder) qs.set('folder', folder)
    if (tag) qs.set('tag', tag)
    qs.set('limit', String(limit))
    qs.set('offset', String(offset))
    return request(`/api/reports?${qs.toString()}`)
  },

  adminReports: () => request('/api/admin/reports'),

  // рассылка по расписанию (ADR-46)
  subscriptions: () => request('/api/admin/subscriptions'),
  createSubscription: (payload) => request('/api/admin/subscriptions', { method: 'POST', body: JSON.stringify(payload) }),
  updateSubscription: (id, payload) =>
    request(`/api/admin/subscriptions/${id}`, { method: 'PUT', body: JSON.stringify(payload) }),
  deleteSubscription: (id) => request(`/api/admin/subscriptions/${id}`, { method: 'DELETE' }),
  runSubscription: (id) => request(`/api/admin/subscriptions/${id}/run`, { method: 'POST' }),
  // сырой шаблон для редактора: с SQL справочников, в отличие от публичного /api/reports/{id}
  adminTemplate: (id) => request(`/api/admin/reports/${encodeURIComponent(id)}`),
  adminHistory: (id) => request(`/api/admin/reports/${encodeURIComponent(id)}/history`),
  // черновик и публикация (ADR-31)
  adminDraft: (id) => request(`/api/admin/reports/${encodeURIComponent(id)}/draft`),
  saveDraft: (id, body) =>
    request(`/api/admin/reports/${encodeURIComponent(id)}/draft`, {
      method: 'PUT',
      body: JSON.stringify({ body })
    }),
  publishDraft: (id) =>
    request(`/api/admin/reports/${encodeURIComponent(id)}/publish`, { method: 'POST' }),
  discardDraft: (id) =>
    request(`/api/admin/reports/${encodeURIComponent(id)}/draft`, { method: 'DELETE' }),
  adminRollback: (id, snapshot) =>
    request(
      `/api/admin/reports/${encodeURIComponent(id)}/rollback/${encodeURIComponent(snapshot)}`,
      { method: 'POST' }
    ),
  createReport: (id, title, body) =>
    request('/api/reports', { method: 'POST', body: JSON.stringify({ id, title, body }) }),
  updateReport: (id, body) =>
    request(`/api/reports/${encodeURIComponent(id)}`, {
      method: 'PUT',
      body: JSON.stringify({ body })
    }),
  deleteReport: (id) =>
    request(`/api/reports/${encodeURIComponent(id)}`, { method: 'DELETE' }),
  validateReport: (id, body) =>
    request(`/api/reports/${encodeURIComponent(id)}/validate`, {
      method: 'POST',
      body: JSON.stringify({ body })
    })
}
