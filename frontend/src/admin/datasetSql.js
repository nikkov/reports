// Панель «Наборы данных» в редакторе шаблонов (ADR-134, расширена в ADR-161).
// SQL-набор правится обычным текстом, REST и join — секцией JSON: у них нет поля sql,
// и правка «через JSON шаблона целиком» была единственным способом.

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

// Наборы только с SQL — оставлено для совместимости вызовов и тестов.
export function listSqlDatasets(template) {
  return listDatasets(template).filter((d) => d.kind === 'sql')
}

// Что панель показывает в редакторе: всё, что можно править осмысленно.
export function listEditableDatasets(template) {
  return listDatasets(template)
}

// Текст для редактора: SQL как есть, rest/join — отформатированная JSON-секция.
export function datasetSpec(template, key) {
  const ds = template && template.datasets ? template.datasets[key] : null
  if (!ds) return { kind: '', text: '' }
  if (ds.rest) return { kind: 'rest', text: JSON.stringify(ds.rest, null, 2) }
  if (ds.join) return { kind: 'join', text: JSON.stringify(ds.join, null, 2) }
  return { kind: 'sql', text: typeof ds.sql === 'string' ? ds.sql : '' }
}

// Записать SQL набора в шаблон. Остальные поля набора и остальные наборы не меняются.
// Возвращает новый объект: исходный шаблон редактора не мутируем.
export function setDatasetSql(template, key, sql) {
  if (!template || !template.datasets || !template.datasets[key]) return template
  const next = { ...template, datasets: { ...template.datasets } }
  next.datasets[key] = { ...next.datasets[key], sql }
  return next
}

// Записать секцию rest/join из JSON-текста. Ошибка разбора возвращается как {error},
// шаблон при этом не меняется — автор видит, что именно не так.
export function setDatasetSpec(template, key, kind, text) {
  if (!template || !template.datasets || !template.datasets[key]) {
    return { template, error: `набор «${key}» не найден` }
  }
  if (kind !== 'rest' && kind !== 'join') return { template, error: `вид «${kind}» не редактируется как JSON` }
  let parsed
  try {
    parsed = JSON.parse(text)
  } catch (e) {
    return { template, error: `JSON не разбирается: ${e.message}` }
  }
  if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed)) {
    return { template, error: 'ожидался JSON-объект' }
  }
  const next = { ...template, datasets: { ...template.datasets } }
  next.datasets[key] = { ...next.datasets[key], [kind]: parsed }
  return { template: next, error: null }
}

// Число строк и признак многострочности — подсказка в панели.
export function sqlInfo(sql) {
  const text = typeof sql === 'string' ? sql : ''
  const lines = text.split('\n')
  return { lines: lines.length, multiline: lines.length > 1, chars: text.length }
}
