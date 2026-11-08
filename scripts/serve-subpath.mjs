#!/usr/bin/env node
// Стенд «как за реверс-прокси»: отдаёт собранный фронтенд под префиксом (/reports)
// и проксирует <префикс>/api на бэкенд. Нужен для проверки ADR-53 без nginx.
//
//   node scripts/serve-subpath.mjs --port 5180 --prefix /reports --api http://127.0.0.1:8080
import { createServer } from 'node:http'
import { readFile, stat } from 'node:fs/promises'
import { extname, join, normalize, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const root = resolve(fileURLToPath(new URL('../frontend/dist', import.meta.url)))
const args = process.argv.slice(2)
const arg = (name, fallback) => {
  const i = args.indexOf(`--${name}`)
  return i >= 0 && args[i + 1] ? args[i + 1] : fallback
}
const port = Number(arg('port', '5180'))
const prefix = arg('prefix', '/reports').replace(/\/$/, '')
const api = arg('api', 'http://127.0.0.1:8080').replace(/\/$/, '')

const TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.ico': 'image/x-icon',
  '.woff2': 'font/woff2',
  '.map': 'application/json; charset=utf-8'
}

async function proxy(req, res, path) {
  const target = api + path
  const headers = { ...req.headers, host: new URL(api).host }
  delete headers['accept-encoding'] // SSE и chunked проще без сжатия
  let upstream
  try {
    upstream = await fetch(target, {
      method: req.method,
      headers,
      body: ['GET', 'HEAD'].includes(req.method) ? undefined : req,
      duplex: 'half'
    })
  } catch (e) {
    res.writeHead(502, { 'content-type': 'text/plain; charset=utf-8' })
    res.end(`upstream недоступен: ${e.message}`)
    return
  }
  const out = {}
  upstream.headers.forEach((value, key) => {
    if (['content-encoding', 'content-length', 'transfer-encoding'].includes(key)) return
    out[key] = value
  })
  res.writeHead(upstream.status, out)
  if (!upstream.body) {
    res.end()
    return
  }
  // поток без буферизации: SSE и большие xlsx идут как есть
  req.on('close', () => upstream.body?.cancel?.().catch?.(() => {}))
  for await (const chunk of upstream.body) res.write(chunk)
  res.end()
}

const server = createServer(async (req, res) => {
  const url = new URL(req.url, `http://${req.headers.host}`)
  if (url.pathname === prefix) {
    res.writeHead(302, { location: `${prefix}/` })
    res.end()
    return
  }
  if (!url.pathname.startsWith(`${prefix}/`)) {
    res.writeHead(404, { 'content-type': 'text/plain; charset=utf-8' })
    res.end('вне префикса приложения')
    return
  }
  const rest = url.pathname.slice(prefix.length) // '/report.html' или '/api/…'
  if (rest.startsWith('/api/')) {
    await proxy(req, res, rest + url.search)
    return
  }

  const relative = normalize(rest === '/' ? '/index.html' : rest).replace(/^(\.\.[/\\])+/, '')
  const file = join(root, relative)
  if (!file.startsWith(root)) {
    res.writeHead(403)
    res.end()
    return
  }
  try {
    const info = await stat(file)
    if (!info.isFile()) throw new Error('not a file')
    const body = await readFile(file)
    res.writeHead(200, { 'content-type': TYPES[extname(file)] || 'application/octet-stream' })
    res.end(req.method === 'HEAD' ? undefined : body)
  } catch {
    // страница есть, а файла нет — отдаём каталог
    try {
      const body = await readFile(join(root, 'index.html'))
      res.writeHead(200, { 'content-type': TYPES['.html'] })
      res.end(body)
    } catch {
      res.writeHead(404, { 'content-type': 'text/plain; charset=utf-8' })
      res.end('не найдено')
    }
  }
})

server.listen(port, '127.0.0.1', () => {
  console.log(`фронтенд под ${prefix}/ на http://127.0.0.1:${port}${prefix}/ , API → ${api}`)
})
