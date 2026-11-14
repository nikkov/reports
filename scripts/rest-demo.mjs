// Демонстрационный REST-источник для примеров ADR-104 (sales-crm).
// Отдаёт справочник регионов с менеджерами — так отчёт объединяет PostgreSQL и REST.
//
//   node scripts/rest-demo.mjs [порт]        # по умолчанию 8099
//
// В шаблоне: connections.crm.baseUrl = "${CRM_BASE_URL}" (http://127.0.0.1:8099),
// а на сервере нужен allowlist: REP_REST_ALLOW_HOSTS=127.0.0.1:8099
import http from 'node:http'

const port = Number(process.argv[2] || process.env.REST_DEMO_PORT || 8099)

const regions = [
  { id: 1, name: 'Север', manager: 'Иванов А.', active: true },
  { id: 2, name: 'Юг', manager: 'Петрова М.', active: true },
  { id: 3, name: 'Запад', manager: 'Сидоров К.', active: true },
  { id: 4, name: 'Восток', manager: 'Кузнецова О.', active: true },
  { id: 5, name: 'Центр', manager: 'Смирнов Д.', active: true }
]

let requests = 0 // счётчик обращений: по нему e2e проверяет кеш REST (ADR-104)
const server = http.createServer((req, res) => {
  const url = new URL(req.url, `http://${req.headers.host}`)
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
