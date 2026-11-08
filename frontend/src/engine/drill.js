// Сборка параметров перехода «провалиться в детали» (ADR-43, ADR-72).
// Одна точка правды: и клик по диаграмме, и подстановка в URL идут через неё.
//
// Ссылки в drill.params:
//   x / series   — концы потока sankey, категория и серия точки;
//   node         — значение кликнутого узла sankey;
//   level.<field>— значение уровня цепочки (регион, категория, товар);
//   param.<name> — параметр текущего отчёта (период и т. п.);
//   что-то иное  — литерал.
//
// Переход невозможен, если нужен уровень, которого нет в кликнутом элементе:
// иначе в целевой отчёт ушёл бы фильтр шире, чем клик.

export function levelRefs(spec) {
  return Object.values(spec?.params || {})
    .filter((ref) => typeof ref === 'string' && ref.startsWith('level.'))
    .map((ref) => ref.slice(6))
}

// Можно ли переходить по этому элементу: все требуемые уровни известны
export function drillAllowed(spec, levels = {}) {
  return levelRefs(spec).every((field) => levels[field] !== undefined && levels[field] !== null)
}

// Параметры перехода: только то, что реально известно про элемент
export function resolveDrill(spec, { x = null, series = null, node = null, levels = {}, params = {} } = {}) {
  const out = []
  for (const [name, ref] of Object.entries(spec?.params || {})) {
    if (typeof ref !== 'string') continue
    if (ref === 'x') out.push([name, x ?? ''])
    else if (ref === 'series') out.push([name, series ?? ''])
    else if (ref === 'node') out.push([name, node ?? ''])
    else if (ref.startsWith('level.')) {
      const value = levels[ref.slice(6)]
      if (value === undefined || value === null) continue // уровень не участвует — параметр не ставим
      out.push([name, String(value)])
    } else if (ref.startsWith('param.')) out.push([name, params[ref.slice(6)] ?? ''])
    else out.push([name, ref])
  }
  return out
}
