// ADR-114: сервер отдаёт строки массивами (меньше байт и времени на разбор).
// Движок работает с объектами по именам колонок — приведение в одном месте.
export function normalizeDataset(body) {
  if (!body || !Array.isArray(body.rows) || !body.rows.length) return body
  if (!Array.isArray(body.rows[0])) return body
  const names = (body.columns || []).map((column) => column.name)
  body.rows = body.rows.map((cells) => {
    const row = {}
    for (let i = 0; i < names.length; i += 1) row[names[i]] = cells[i] ?? null
    return row
  })
  return body
}
