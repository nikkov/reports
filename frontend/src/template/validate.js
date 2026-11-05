// Валидация шаблона отчёта: структура (JSON Schema) + семантические правила.
// Блоки проверяются по своей ветке (text/table/chart), поэтому одна ошибка = одно сообщение,
// а не лавина от oneOf на все варианты сразу.
import Ajv2020 from 'ajv/dist/2020.js'
import schema from './schema.js'

const ajv = new Ajv2020({ allErrors: true, strict: false })

// Корневая проверка без содержимого блоков: их валидируем отдельно по типу.
const rootSchema = {
  $schema: schema.$schema,
  $id: 'https://rep.local/template.root.json',
  $defs: schema.$defs,
  type: 'object',
  additionalProperties: false,
  required: schema.required,
  properties: {
    ...schema.properties,
    blocks: { type: 'array', minItems: 1 }
  }
}

const validateRoot = ajv.compile(rootSchema)
const branchValidator = {
  text: ajv.compile({ $schema: schema.$schema, $defs: schema.$defs, $ref: '#/$defs/textBlock' }),
  table: ajv.compile({ $schema: schema.$schema, $defs: schema.$defs, $ref: '#/$defs/tableBlock' }),
  chart: ajv.compile({ $schema: schema.$schema, $defs: schema.$defs, $ref: '#/$defs/chartBlock' })
}

const translate = {
  'must be equal to constant': 'ожидалось значение',
  'must match pattern': 'не соответствует шаблону',
  'must have required property': 'отсутствует обязательное поле',
  'must NOT have additional properties': 'недопустимое поле',
  'must be string': 'ожидалась строка',
  'must be integer': 'ожидалось целое число',
  'must be boolean': 'ожидалось true/false',
  'must be array': 'ожидался массив',
  'must be object': 'ожидался объект',
  'must be equal to one of the allowed values': 'недопустимое значение',
  'must NOT have fewer than 1 items': 'пустой массив недопустим',
  'must NOT have fewer than 1 characters': 'пустое значение недопустимо',
  'must be number': 'ожидалось число'
}

function humanMessage(err) {
  let text = err.message || ''
  for (const [en, ru] of Object.entries(translate)) {
    if (text.includes(en)) text = text.replace(en, ru)
  }
  const p = err.params || {}
  if (p.additionalProperty) text += `: «${p.additionalProperty}»`
  if (p.missingProperty) text += `: «${p.missingProperty}»`
  if (p.allowedValues) text += `: ${p.allowedValues.join(', ')}`
  if (err.keyword === 'const' && p.allowedValue !== undefined) text += `: ${p.allowedValue}`
  return text
}

function localPath(err, prefix) {
  const parts = (err.instancePath || '').split('/').filter(Boolean)
  const p = err.params || {}
  if (err.keyword === 'required' && p.missingProperty) parts.push(p.missingProperty)
  if (err.keyword === 'additionalProperties' && p.additionalProperty) parts.push(p.additionalProperty)
  const local = parts.length ? parts.join('.') : '(корень)'
  return prefix ? `${prefix}.${local}` : local
}

/**
 * Проверяет шаблон.
 * @returns {{errors: Array<{path: string, message: string}>, warnings: Array<{path: string, message: string}>}}
 */
export function validateTemplate(template) {
  const errors = []
  const warnings = []

  if (!validateRoot(template)) {
    for (const err of validateRoot.errors || []) {
      errors.push({ path: localPath(err), message: humanMessage(err) })
    }
    // Структура корня сломана — дальше проверять нечего, кроме блоков, если массив есть.
    if (!Array.isArray(template?.blocks)) return { errors, warnings }
  }

  const params = template.params || []

  for (const [key, ds] of Object.entries(template.datasets || {})) {
    for (const [paramName, source] of Object.entries(ds.params || {})) {
      if (typeof source === 'string' && source.startsWith('$')) {
        const ref = source.slice(1)
        if (!params.some((p) => p.name === ref)) {
          errors.push({
            path: `datasets.${key}.params.${paramName}`,
            message: `ссылка на необъявленный параметр «${ref}»`
          })
        }
      }
    }
  }

  const datasetKeys = Object.keys(template.datasets || {})
  const columnsByDataKey = {}

  template.blocks.forEach((block, index) => {
    const base = `blocks.${index}`

    const validator = block && branchValidator[block.type]
    if (!validator) {
      errors.push({
        path: `${base}.type`,
        message: `неизвестный тип блока «${block?.type ?? '—'}» (допустимо: text, table, chart)`
      })
      return
    }

    if (!validator(block)) {
      for (const err of validator.errors || []) {
        errors.push({ path: localPath(err, base), message: humanMessage(err) })
      }
      return
    }

    if (block.type === 'text') {
      for (const m of block.text.matchAll(/\{\{\s*([a-z][a-z0-9_]*)\s*\}\}/g)) {
        if (!params.some((p) => p.name === m[1])) {
          errors.push({ path: `${base}.text`, message: `в тексте используется необъявленный параметр «${m[1]}»` })
        }
      }
      return
    }

    if (!datasetKeys.includes(block.dataKey)) {
      errors.push({
        path: `${base}.dataKey`,
        message: `набор данных «${block.dataKey}» не объявлен в datasets (есть: ${datasetKeys.join(', ') || '—'})`
      })
    }

    if (block.type === 'table') {
      columnsByDataKey[block.dataKey] = (columnsByDataKey[block.dataKey] || []).concat(block.columns)

      let seenLeaf = false
      block.columns.forEach((col, ci) => {
        const cpath = `${base}.columns.${ci}`
        const hasField = typeof col.field === 'string' && col.field.length > 0
        const hasExpr = typeof col.expression === 'string' && col.expression.length > 0

        if (!hasField && !hasExpr) {
          errors.push({ path: cpath, message: 'нужно указать field или expression' })
        }
        if (hasField && hasExpr) {
          errors.push({ path: cpath, message: 'нельзя указывать одновременно field и expression' })
        }
        if (col.group && seenLeaf) {
          errors.push({
            path: cpath,
            message: 'групповая колонка должна идти до обычных: порядок колонок задаёт порядок группировки'
          })
        }
        if (!col.group && !seenLeaf) seenLeaf = true

        if (col.subtotal && !col.group) {
          errors.push({ path: cpath, message: 'подытог допустим только у групповой колонки' })
        }
        if (!col.group && col.aggregate && !hasField) {
          errors.push({ path: cpath, message: 'агрегат требует поля field' })
        }
        if (col.format === 'percent' && hasField && !col.aggregate && !hasExpr) {
          warnings.push({
            path: cpath,
            message: 'percent без выражения покажет долю из значения поля как есть'
          })
        }
      })

      if (block.grandTotal && !block.columns.some((c) => !c.group)) {
        warnings.push({ path: `${base}.grandTotal`, message: 'нет листовых колонок для строки ИТОГО' })
      }
    }

    if (block.type === 'chart') {
      const cols = columnsByDataKey[block.dataKey] || []
      const known = new Set(cols.map((c) => c.field).filter(Boolean))
      if (known.size) {
        for (const field of ['xField', 'yField']) {
          if (!known.has(block[field])) {
            errors.push({
              path: `${base}.${field}`,
              message: `поле «${block[field]}» отсутствует в таблице этого набора данных`
            })
          }
        }
      }
    }
  })

  return { errors, warnings }
}

export default validateTemplate
