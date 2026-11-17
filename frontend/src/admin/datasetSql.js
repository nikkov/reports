// Работа с многострочным SQL в редакторе шаблонов (ADR-134).
// Панель «Наборы данных» правит datasets.<key>.sql как обычный текст, а в JSON-документ
// значение возвращается одним полем — экранирование делает JSON.stringify.

// Список наборов для боковой панели: ключ, вид и текущий SQL.
export function listDatasets(template) {
  const datasets = (template && template.datasets) || {}
  return Object.keys(datasets).map((key) => {
    const ds = datasets[key] || {}
    const kind = ds.rest ? 'rest' : ds.join ? 'join' : 'sql'
    const sql = typeof ds.sql === 'string' ? ds.sql : ''
    return { key, kind, sql }
  })
}

// Наборы для SQL-панели: REST и join-источники не показываем — в них нет поля sql,
// и редактировать нечего (ADR-138).
export function listSqlDatasets(template) {
  return listDatasets(template).filter((d) => d.kind === 'sql')
}

// Записать SQL набора в шаблон. Остальные поля набора и остальные наборы не меняются.
// Возвращает новый объект: исходный шаблон редактора не мутируем.
export function setDatasetSql(template, key, sql) {
  if (!template || !template.datasets || !template.datasets[key]) return template
  const next = { ...template, datasets: { ...template.datasets } }
  next.datasets[key] = { ...next.datasets[key], sql }
  return next
}

// Число строк в SQL и признак того, что запрос действительно многострочный —
// подсказка в панели, чтобы автор видел, что переносы сохранены.
export function sqlInfo(sql) {
  const text = typeof sql === 'string' ? sql : ''
  const lines = text.split('\n')
  return { lines: lines.length, multiline: lines.length > 1, chars: text.length }
}
