// Тонкий клиент API отчётов. Все запросы отменяемы (AbortController), ошибки типизированы.
// Префикс API определяется во время выполнения (ADR-53), поэтому сборку можно
// копировать на другой хост и в другой каталог без пересборки:
//   1) явное переопределение — <meta name="rep-api-base" content="/reports"> или window.REP_API_BASE
//      (или VITE_API_BASE при сборке, если так удобнее);
//   2) иначе API считается там же, где лежит приложение: /reports/report.html → /reports/api,
//      корень → /api.
function resolveApiBase() {
  const meta =
    typeof document !== 'undefined' ? document.querySelector('meta[name="rep-api-base"]') : null
  const explicit =
    (typeof window !== 'undefined' && window.REP_API_BASE) ||
    (meta && meta.getAttribute('content')) ||
    import.meta.env.VITE_API_BASE ||
    ''
  if (explicit) {
    let value = String(explicit).replace(/\/+$/, '')
    // допускаем и «префикс приложения», и полный адрес API: /reports и /reports/api
    if (value.endsWith('/api')) value = value.slice(0, -'/api'.length)
    return value
  }
  const dir = new URL('.', document.baseURI).pathname.replace(/\/+$/, '')
  return dir === '' ? '' : dir
}

export const API_BASE = resolveApiBase()
const BASE = API_BASE

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
  lookups: (reportId, param, params = {}, signal, draft = false) => {
    const q = new URLSearchParams()
    for (const [k, v] of Object.entries(params)) {
      if (v !== undefined && v !== null && v !== '') q.set(k, String(v))
    }
    // справочник черновика считается по его телу (ADR-82)
    if (draft) q.set('draft', '1')
    const suffix = q.toString() ? `?${q}` : ''
    return request(
      `/api/reports/${encodeURIComponent(reportId)}/lookups/${encodeURIComponent(param)}${suffix}`,
      { signal }
    )
  },
  // серверная агрегация (ADR-37): GROUP BY по groupBy, агрегаты по measures
  aggregate: (reportId, params, spec, signal, dataset, draft = false) =>
    request('/api/report/aggregate', {
      method: 'POST',
      body: JSON.stringify({
        reportId,
        params,
        ...(dataset ? { dataset } : {}),
        ...(draft ? { draft: true } : {}),
        groupBy: spec.groupBy || [],
        measures: spec.measures || []
      }),
      signal
    }),

  // детали одной группы серверной агрегации (ADR-65): field — колонка groupBy, value — ключ
  group: (reportId, params, spec, signal) =>
    request('/api/report/group', {
      method: 'POST',
      body: JSON.stringify({
        reportId,
        params,
        field: spec.field,
        value: spec.value === undefined ? null : spec.value,
        ...(spec.dataset ? { dataset: spec.dataset } : {}),
        ...(spec.draft ? { draft: true } : {}),
        ...(spec.limit ? { limit: spec.limit } : {}),
        ...(spec.offset ? { offset: spec.offset } : {}),
        ...(Array.isArray(spec.order) && spec.order.length ? { order: spec.order } : {})
      }),
      signal
    }),

  // dataset — ключ из datasets шаблона (ADR-36); без него сервер отдаёт первый набор
  // Строка для текстового блока (ADR-101): запрос обязан вернуть не более одной строки
  text: (reportId, params, dataset, draft = false, signal) =>
    request('/api/report/text', {
      method: 'POST',
      body: JSON.stringify({
        reportId,
        params,
        ...(dataset ? { dataset } : {}),
        ...(draft ? { draft: true } : {})
      }),
      signal
    }),

  data: (reportId, params, signal, dataset, draft = false) =>
    request('/api/report/data', {
      method: 'POST',
      body: JSON.stringify({
        reportId,
        params,
        ...(dataset ? { dataset } : {}),
        ...(draft ? { draft: true } : {})
      }),
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
  subscriptionRuns: (id, limit = 20) => request(`/api/admin/subscriptions/${id}/runs?limit=${limit}`),

  // пользователи и роли (ADR-50)
  users: () => request('/api/admin/users'),
  createUser: (payload) => request('/api/admin/users', { method: 'POST', body: JSON.stringify(payload) }),
  updateUser: (id, payload) =>
    request(`/api/admin/users/${id}`, { method: 'PUT', body: JSON.stringify(payload) }),
  deleteUser: (id) => request(`/api/admin/users/${id}`, { method: 'DELETE' }),

  // справочник ролей доступа к отчётам (ADR-102)
  accessRoles: () => request('/api/admin/access-roles'),
  createAccessRole: (payload) =>
    request('/api/admin/access-roles', { method: 'POST', body: JSON.stringify(payload) }),
  updateAccessRole: (id, payload) =>
    request(`/api/admin/access-roles/${id}`, { method: 'PUT', body: JSON.stringify(payload) }),
  deleteAccessRole: (id) => request(`/api/admin/access-roles/${id}`, { method: 'DELETE' }),

  // журнал действий (ADR-52)
  audit: ({ action = '', actor = '', from = '', to = '', limit = 50, offset = 0 } = {}) => {
    const qs = new URLSearchParams()
    if (action) qs.set('action', action)
    if (actor) qs.set('actor', actor)
    if (from) qs.set('from', from)
    if (to) qs.set('to', to)
    qs.set('limit', String(limit))
    qs.set('offset', String(offset))
    return request(`/api/admin/audit?${qs.toString()}`)
  },

  // выгрузка журнала (ADR-67): те же фильтры, что у списка
  auditExportUrl: ({ action = '', actor = '', from = '', to = '', excel = false } = {}) => {
    const qs = new URLSearchParams()
    if (action) qs.set('action', action)
    if (actor) qs.set('actor', actor)
    if (from) qs.set('from', from)
    if (to) qs.set('to', to)
    if (excel) qs.set('excel', '1')
    return `${API_BASE}/api/admin/audit/export.csv?${qs.toString()}`
  },
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
