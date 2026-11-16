// Демонстрационный REST-источник для примеров ADR-104 (sales-crm).
// Отдаёт справочник регионов с менеджерами — так отчёт объединяет PostgreSQL и REST.
//
//   node scripts/rest-demo.mjs [порт]        # по умолчанию 8099
//
// В шаблоне: connections.crm.baseUrl = "${CRM_BASE_URL}" (http://127.0.0.1:8099),
// а на сервере нужен allowlist: REP_REST_ALLOW_HOSTS=127.0.0.1:8099
import http from 'node:http'

const port = Number(process.argv[2] || process.env.REST_DEMO_PORT || 8099)
// BASIC-аутентификация демо-сервиса (ADR-121): по умолчанию demo / demo-pass
const AUTH_USER = process.env.CRM_USER || 'demo'
const AUTH_PASS = process.env.CRM_PASSWORD || 'demo-pass'

function basicOk(req) {
  const header = req.headers.authorization || ''
  if (!header.startsWith('Basic ')) return false
  const decoded = Buffer.from(header.slice(6), 'base64').toString('utf8')
  const idx = decoded.indexOf(':')
  if (idx < 0) return false
  return decoded.slice(0, idx) === AUTH_USER && decoded.slice(idx + 1) === AUTH_PASS
}

function requireBasic(req, res) {
  if (basicOk(req)) return true
  res.writeHead(401, {
    'Content-Type': 'application/json; charset=utf-8',
    'WWW-Authenticate': 'Basic realm="crm"'
  })
  res.end(JSON.stringify({ error: 'unauthorized' }))
  return false
}

const regions = [
  { id: 1, name: 'Север', manager: 'Иванов А.', active: true },
  { id: 2, name: 'Юг', manager: 'Петрова М.', active: true },
  { id: 3, name: 'Запад', manager: 'Сидоров К.', active: true },
  { id: 4, name: 'Восток', manager: 'Кузнецова О.', active: true },
  { id: 5, name: 'Центр', manager: 'Смирнов Д.', active: true }
]

let requests = 0 // счётчик обращений: по нему e2e проверяет кеш REST (ADR-104)
// Цены по категориям: POST с телом-фильтром (ADR-117). Категории приходят из другого
// набора (SQL), шаблон тела собирает сервер отчётов подстановкой $набор.колонка.
const prices = {
  'Электроника': { id: 1, price: 125000, currency: 'RUB', updated: '2026-11-01' },
  'Бытовая техника': { id: 2, price: 89000, currency: 'RUB', updated: '2026-11-01' },
  'Мебель': { id: 3, price: 43000, currency: 'RUB', updated: '2026-11-01' },
  'Аксессуары': { id: 4, price: 210000, currency: 'RUB', updated: '2026-11-01' }
}

function readJson(req) {
  return new Promise((resolve) => {
    let raw = ''
    req.on('data', (chunk) => {
      raw += chunk
      if (raw.length > 1_000_000) req.destroy()
    })
    req.on('end', () => {
      try {
        resolve(raw ? JSON.parse(raw) : {})
      } catch {
        resolve(null)
      }
    })
  })
}

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, `http://${req.headers.host}`)
  // GET с категорией в пути (ADR-118): проверка плейсхолдера в path и заголовка набора
  if (url.pathname.startsWith('/api/prices/')) {
    if (!requireBasic(req, res)) return
    // путь может прийти как есть, urlencode или дважды закодированным — разбираем все варианты
    const rawName = url.pathname.split('/').pop()
    let category = rawName
    for (let i = 0; i < 2; i += 1) {
      try {
        const decoded = decodeURIComponent(category)
        if (decoded === category) break
        category = decoded
      } catch {
        break
      }
    }
    const row = prices[category]
    if (!row) {
      res.writeHead(404, { 'Content-Type': 'application/json; charset=utf-8' })
      res.end(JSON.stringify({ error: 'unknown category', category }))
      return
    }
    requests += 1
    res.writeHead(200, { 'Content-Type': 'application/json; charset=utf-8' })
    res.end(
      JSON.stringify({
        items: [
          {
            category_id: row.id,
            category,
            price: row.price,
            currency: row.currency,
            tenant: req.headers['x-tenant'] || ''
          }
        ],
        total: 1
      })
    )
    return
  }
  if (url.pathname === '/api/prices' && req.method === 'POST') {
    if (!requireBasic(req, res)) return
    const body = await readJson(req)
    if (!body) {
      res.writeHead(400, { 'Content-Type': 'application/json; charset=utf-8' })
      res.end(JSON.stringify({ error: 'bad json' }))
      return
    }
    requests += 1
    // фильтр: { filter: { categories: [ { id_category: 1, name: "Север" } ] } }
    const filter = body?.filter?.categories
    const items = []
    if (Array.isArray(filter)) {
      for (const item of filter) {
        // принимаем и объект {id_category, name}, и просто имя категории
        const name = typeof item === 'object' && item !== null ? item.name : String(item)
        const row = prices[name]
        if (!row) continue
        items.push({
          category_id: row.id,
          category: name,
          price: row.price,
          currency: row.currency,
          updated: row.updated
        })
      }
    }
    res.writeHead(200, { 'Content-Type': 'application/json; charset=utf-8' })
    res.end(JSON.stringify({ items, total: items.length, echo: body }))
    return
  }
  if (url.pathname === '/api/_stats') {
    res.writeHead(200, { 'Content-Type': 'application/json' })
    res.end(JSON.stringify({ requests }))
    return
  }
  if (url.pathname === '/api/regions') {
    requests += 1
    // фильтр active=1 поддерживает пример параметризованного REST-набора
    const active = url.searchParams.get('active')
    const items = active === '1' ? regions.filter((r) => r.active) : regions
    res.writeHead(200, { 'Content-Type': 'application/json; charset=utf-8' })
    res.end(JSON.stringify({ items, total: items.length }))
    return
  }
  if (url.pathname === '/api/rows') {
    requests += 1
    // 2500 записей: проверка пагинации (page/limit) в шаблоне
    const page = Number(url.searchParams.get('page') || 1)
    const limit = Number(url.searchParams.get('limit') || 1000)
    const all = Array.from({ length: 2500 }, (_, i) => ({ id: i + 1, name: 'Строка ' + (i + 1) }))
    const from = (page - 1) * limit
    const items = all.slice(from, from + limit)
    res.writeHead(200, { 'Content-Type': 'application/json; charset=utf-8' })
    res.end(JSON.stringify({ items, total: all.length, page }))
    return
  }
  if (url.pathname === '/api/rows-offset') {
    requests += 1
    // смещение вместо номера страницы: проверка rest.page.mode = "offset"
    const offset = Number(url.searchParams.get('offset') || 0)
    const limit = Number(url.searchParams.get('limit') || 1000)
    const all = Array.from({ length: 2500 }, (_, i) => ({ id: i + 1, name: 'Строка ' + (i + 1) }))
    res.writeHead(200, { 'Content-Type': 'application/json; charset=utf-8' })
    res.end(JSON.stringify({ items: all.slice(offset, offset + limit), total: all.length, offset }))
    return
  }
  if (url.pathname === '/api/health') {
    res.writeHead(200, { 'Content-Type': 'application/json' })
    res.end(JSON.stringify({ ok: true }))
    return
  }
  res.writeHead(404, { 'Content-Type': 'application/json' })
  res.end(JSON.stringify({ error: 'not found' }))
})

server.listen(port, '127.0.0.1', () => {
  console.log(`REST-демо на http://127.0.0.1:${port}/api/regions`)
})
