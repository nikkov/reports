// Проброс SSE через прокси стенда (vite dev/preview): http-proxy отдаёт потоковый
// ответ как пустой (Content-Length: 0), поэтому поток /api/report/events читаем сами.
const SSE_PATH = '/api/report/events'

export function ssePassthrough(upstream = process.env.REP_API || 'http://127.0.0.1:8080') {
  const base = upstream.replace(/\/$/, '')
  const handler = async (req, res, next) => {
    if (!req.url || !req.url.startsWith(SSE_PATH)) return next()
    let source
    // отмена через AbortController: cancel() на уже читаемом потоке бросает ERR_INVALID_STATE
    const abort = new AbortController()
    try {
      source = await fetch(base + req.url, {
        headers: { cookie: req.headers.cookie || '', accept: 'text/event-stream' },
        signal: abort.signal
      })
    } catch (e) {
      res.writeHead(502, { 'content-type': 'text/plain; charset=utf-8' })
      res.end('upstream недоступен')
      return
    }
    res.writeHead(source.status, {
      'content-type': 'text/event-stream; charset=utf-8',
      'cache-control': 'no-cache',
      connection: 'keep-alive',
      'x-accel-buffering': 'no'
    })
    req.on('close', () => abort.abort())
    try {
      const reader = source.body.getReader()
      for (;;) {
        const { done, value } = await reader.read()
        if (done) break
        res.write(Buffer.from(value))
      }
    } catch (e) {
      /* клиент ушёл или стенд остановлен */
    }
    res.end()
  }
  return {
    name: 'rep-sse-passthrough',
    configureServer: (server) => {
      server.middlewares.use(handler)
    },
    configurePreviewServer: (server) => {
      server.middlewares.use(handler)
    }
  }
}
