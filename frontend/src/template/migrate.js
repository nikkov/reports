// Версия формата шаблона и миграции (ADR-51) — та же логика, что backend/src/template_migrate.cc.
// Клиент поднимает шаблон до актуальной версии при открытии, сервер — при сохранении и публикации.
export const CURRENT_VERSION = 2

// Возвращает {ok, doc, fromVersion, toVersion, steps, error}. Исходный объект не меняется.
export function migrateTemplate(input) {
  if (!input || typeof input !== 'object' || Array.isArray(input)) {
    return { ok: false, error: 'шаблон должен быть JSON-объектом', steps: [] }
  }
  const doc = JSON.parse(JSON.stringify(input))
  let version = 0
  if (doc.version !== undefined) {
    if (!Number.isInteger(doc.version)) {
      return { ok: false, error: 'version должен быть целым числом', steps: [] }
    }
    version = doc.version
  }
  const fromVersion = version

  if (version > CURRENT_VERSION) {
    return {
      ok: false,
      error: `шаблон версии ${version}, поддерживается до ${CURRENT_VERSION} — обновите сервер`,
      steps: []
    }
  }

  const steps = []
  if (version < 1) {
    doc.version = 1
    steps.push('0→1: добавлена версия формата')
    version = 1
  }
  if (version < 2) {
    let charts = 0
    for (const block of Array.isArray(doc.blocks) ? doc.blocks : []) {
      if (!block || block.type !== 'chart' || block.chartType !== undefined) continue
      block.chartType = 'bar'
      charts += 1
    }
    steps.push(charts ? `1→2: у ${charts} диаграмм(ы) chartType по умолчанию «bar»` : '1→2: формат помечен версией 2')
    doc.version = 2
    version = 2
  }

  return { ok: true, doc, fromVersion, toVersion: version, steps }
}

export default migrateTemplate
