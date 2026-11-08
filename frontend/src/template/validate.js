// Валидация шаблона отчёта: структура (JSON Schema) + семантические правила.
// Блоки проверяются по своей ветке (text/table/chart), поэтому одна ошибка = одно сообщение,
// а не лавина от oneOf на все варианты сразу.
import Ajv2020 from 'ajv/dist/2020.js'
import schema from './schema.js'
import { analyzeExpression } from '../engine/expr.js'

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
// Условное форматирование (ADR-25): правила адресуются колонками таблицы, операторы закрытые.
function checkRules(rules, base, knownFields, errors, warnings) {
  if (!Array.isArray(rules)) return

  if (rules.length > 20) {
    warnings.push({ path: base, message: `правил ${rules.length}: шаблон сложно поддерживать` })
  }

  const walkWhen = (when, path) => {
    if (!when || typeof when !== 'object') return
    for (const group of ['all', 'any']) {
      if (Array.isArray(when[group])) {
        when[group].forEach((leaf, i) => walkWhen(leaf, `${path}.${group}.${i}`))
      }
    }
    if (!when.column) return
    if (knownFields.size && !knownFields.has(when.column)) {
      errors.push({ path: `${path}.column`, message: `правило ссылается на «${when.column}», которого нет в колонках таблицы` })
    }
    if (when.op === 'between' && !(Array.isArray(when.value) && when.value.length === 2)) {
      errors.push({ path: `${path}.value`, message: 'для between нужен массив из двух значений' })
    }
    if (when.op !== 'empty' && when.op !== 'notEmpty' && when.value === undefined) {
      errors.push({ path: `${path}.value`, message: `для оператора ${when.op} нужно значение` })
    }
  }

  rules.forEach((rule, i) => {
    const path = `${base}.${i}`
    walkWhen(rule.when, `${path}.when`)
    if (rule.target && knownFields.size && !knownFields.has(rule.target)) {
      errors.push({ path: `${path}.target`, message: `target «${rule.target}» отсутствует в колонках таблицы` })
    }
    const zones = rule.zones || ['detail', 'subtotal']
    if (!zones.length) errors.push({ path: `${path}.zones`, message: 'zones не может быть пустым' })
    if (rule.apply?.colors?.text && rule.apply.colors.background &&
        String(rule.apply.colors.text).toLowerCase() === String(rule.apply.colors.background).toLowerCase()) {
      warnings.push({ path: `${path}.apply.colors`, message: 'цвет текста правила совпадает с фоном' })
    }
  })
}

// Семантика стилей (ADR-23): схема проверяет форматы, здесь — то, что формой не выразить.
function checkStyles(styles, path, knownFields, errors, warnings, palette = {}) {
  if (!styles) return

  // цвет — hex/transparent или имя из styles.palette; слова вне палитры ловим здесь
  const checkColor = (value, p) => {
    if (typeof value !== 'string') return
    const isHex = /^#[0-9a-fA-F]{3}$|^#[0-9a-fA-F]{6}$/.test(value) || value === 'transparent'
    if (isHex) return
    if (!Object.prototype.hasOwnProperty.call(palette, value)) {
      errors.push({ path: p, message: `цвет «${value}» не hex и не объявлен в styles.palette` })
    }
  }
  for (const zone of ['header', 'body', 'subtotal', 'grandtotal']) {
    for (const key of ['text', 'background', 'border']) {
      const value = styles[zone]?.colors?.[key]
      if (value) checkColor(value, `${path}.${zone}.colors.${key}`)
    }
  }
  for (const key of ['text', 'background', 'border']) {
    if (styles.colors?.[key]) checkColor(styles.colors[key], `${path}.colors.${key}`)
  }
  for (const [field, style] of Object.entries(styles.columns || {})) {
    for (const key of ['text', 'background']) {
      if (style?.colors?.[key]) checkColor(style.colors[key], `${path}.columns.${field}.colors.${key}`)
    }
  }
  const zones = ['header', 'body', 'subtotal', 'grandtotal']
  for (const zone of zones) {
    const z = styles[zone]
    if (!z?.colors?.text || !z?.colors?.background) continue
    if (z.colors.text.toLowerCase() === z.colors.background.toLowerCase()) {
      warnings.push({ path: `${path}.${zone}.colors`, message: 'цвет текста совпадает с фоном — текст не виден' })
    }
  }
  const size = styles.font?.size
  const rowHeight = styles.layout?.rowHeight
  if (size && rowHeight && rowHeight < size * 1.2) {
    warnings.push({
      path: `${path}.layout.rowHeight`,
      message: `высота строки ${rowHeight}px мала для шрифта ${size}px (нужно ≥ ${Math.ceil(size * 1.2)}px)`
    })
  }
  for (const [field, style] of Object.entries(styles.columns || {})) {
    const fmt = style.format
    if (fmt && typeof fmt === 'object') {
      if (fmt.style === 'date' && fmt.decimals !== undefined) {
        warnings.push({ path: `${path}.columns.${field}.format`, message: 'decimals не влияет на формат даты' })
      }
      if (fmt.style === 'integer' && fmt.decimals !== undefined && fmt.decimals > 0) {
        warnings.push({ path: `${path}.columns.${field}.format`, message: 'decimals не влияет на целочисленный формат' })
      }
    }
  }

  const columns = styles.columns || {}
  for (const [field, style] of Object.entries(columns)) {
    if (knownFields && knownFields.size && !knownFields.has(field)) {
      errors.push({ path: `${path}.columns.${field}`, message: `стиль задан для поля «${field}», которого нет в колонках таблицы` })
    }
    if (style.minWidth && style.maxWidth && style.minWidth > style.maxWidth) {
      errors.push({ path: `${path}.columns.${field}`, message: 'minWidth больше maxWidth' })
    }
  }
  if (Object.keys(columns).length > 15) {
    warnings.push({ path: `${path}.columns`, message: `переопределений колонок ${Object.keys(columns).length}: шаблон сложно поддерживать, подумайте о пресете` })
  }
}

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

  // версия формата (ADR-51): старую поднимет сервер, новую он не поймёт
  if (Number.isInteger(template.version) && template.version < 2) {
    warnings.push({ path: 'version', message: `шаблон версии ${template.version}: при сохранении будет переведён в версию 2` })
  }

  const params = template.params || []

  // Способы получения значений параметров (ADR-22): дата/время, статический набор, справочник из БД.
  const paramsByName = new Map(params.map((p) => [p.name, p]))

  params.forEach((p, index) => {
    const path = `params.${index}`
    const src = p.source
    const kind = src?.kind

    if (!src) {
      // старый стиль: select обязан иметь options, иначе пользователю нечего выбрать
      if (p.type === 'select' && !(p.options && p.options.length)) {
        errors.push({ path, message: 'для select нужен source.options или source.kind=lookup' })
      }
      return
    }

    if (kind === 'lookup') {
      if (!src.sql) {
        errors.push({ path: `${path}.source`, message: 'для source.kind=lookup обязателен sql (SQL списка задаётся в шаблоне)' })
      }
      if (src.queryId) {
        errors.push({ path: `${path}.source`, message: 'queryId больше не поддерживается: перенесите SQL в source.sql' })
      }
      if (src.options) errors.push({ path: `${path}.source`, message: 'lookup и options взаимоисключающие' })
      if (p.type !== 'select') {
        errors.push({ path: `${path}.type`, message: 'параметр со справочником должен иметь type=select' })
      }
    }

    if (kind === 'static') {
      if (!src.options || !src.options.length) {
        errors.push({ path: `${path}.source`, message: 'для source.kind=static нужен непустой options' })
      }
      if (src.queryId || src.sql) errors.push({ path: `${path}.source`, message: 'static не принимает queryId/sql' })
      if (p.type !== 'select') {
        errors.push({ path: `${path}.type`, message: 'параметр со статическим набором должен иметь type=select' })
      }
    }

    if (kind === 'date' || kind === 'datetime') {
      if (src.queryId || src.sql || src.options) {
        errors.push({ path: `${path}.source`, message: 'для даты/времени queryId и options недопустимы' })
      }
      if (p.type !== 'date' && p.type !== 'datetime') {
        errors.push({ path: `${path}.type`, message: `для source.kind=${kind} ожидался type=${kind}` })
      }
    }

    if (p.multiple && kind !== 'lookup' && kind !== 'static') {
      errors.push({ path: `${path}.multiple`, message: 'множественный выбор допустим только для справочника или набора' })
    }

    if (src.dependsOn) {
      const parent = paramsByName.get(src.dependsOn)
      if (!parent) {
        errors.push({ path: `${path}.source.dependsOn`, message: `неизвестный параметр-родитель «${src.dependsOn}»` })
      } else {
        if (params.indexOf(parent) > index) {
          errors.push({ path: `${path}.source.dependsOn`, message: 'параметр-родитель должен быть объявлен выше' })
        }
        if (parent.multiple) {
          errors.push({ path: `${path}.source.dependsOn`, message: 'зависимость от множественного выбора не поддерживается' })
        }
      }
    }
  })

  // автообновление: целое число секунд, 0 выключает
  if (template.refreshSeconds !== undefined) {
    const r = template.refreshSeconds
    if (!Number.isInteger(r)) {
      errors.push({ path: 'refreshSeconds', message: 'refreshSeconds должен быть целым числом' })
    } else if (r < 0 || r > 3600) {
      errors.push({ path: 'refreshSeconds', message: 'refreshSeconds: допустимо 0…3600' })
    } else if (r > 0 && r < 5) {
      warnings.push({ path: 'refreshSeconds', message: 'меньше 5 секунд — лишняя нагрузка на базу' })
    }
  }

  // каталог: папка и теги
  if (template.folder !== undefined && typeof template.folder !== 'string') {
    errors.push({ path: 'folder', message: 'folder должен быть строкой' })
  }
  if (template.tags !== undefined && (!Array.isArray(template.tags) || template.tags.some((t) => typeof t !== 'string'))) {
    errors.push({ path: 'tags', message: 'tags должен быть массивом строк' })
  }

  // Цветовые зоны gauge (ADR-57): N границ дают N+1 зону, значит и N+1 цвет.
function checkGaugeZones(block, base, errors) {
  const thresholds = block.thresholds
  const colors = block.zoneColors
  if (thresholds === undefined && colors === undefined) return
  if (!Array.isArray(thresholds) || thresholds.length < 2) {
    errors.push({ path: `${base}.thresholds`, message: 'нужно не меньше двух границ (например p1…p4)' })
    return
  }
  for (let i = 1; i < thresholds.length; i++) {
    if (!(thresholds[i] > thresholds[i - 1])) {
      errors.push({
        path: `${base}.thresholds.${i}`,
        message: 'границы должны возрастать: каждое значение больше предыдущего'
      })
    }
  }
  if (!Array.isArray(colors)) {
    errors.push({ path: `${base}.zoneColors`, message: `нужно ${thresholds.length + 1} цвет(а/ов) — по одному на зону` })
    return
  }
  if (colors.length !== thresholds.length + 1) {
    errors.push({
      path: `${base}.zoneColors`,
      message: `границ ${thresholds.length} — зон ${thresholds.length + 1}, а цветов ${colors.length}`
    })
  }
  const palette = {} // hex проверяется схемой; токены палитры проверит styles
  for (const [i, color] of colors.entries()) {
    if (typeof color !== 'string' || (!/^#[0-9a-fA-F]{3}$|^#[0-9a-fA-F]{6}$|^transparent$/.test(color) && !/^[a-zA-Z][a-zA-Z0-9_-]*$/.test(color))) {
      errors.push({ path: `${base}.zoneColors.${i}`, message: 'цвет — hex или имя из styles.palette' })
    }
  }
  void palette
}

// drill-through: из подытога таблицы (ADR-29) или из точки диаграммы (ADR-43)
  ;(template.blocks || []).forEach((block, i) => {
    if (!block.drill) return
    const isTable = block.type === 'table'
    const isChart = block.type === 'chart'
    if (!isTable && !isChart) {
      errors.push({ path: `blocks.${i}.drill`, message: 'drill допустим только в табличном блоке или диаграмме' })
      return
    }
    if (!block.drill.report) {
      errors.push({ path: `blocks.${i}.drill.report`, message: 'drill.report обязателен' })
    }
    // у gauge нет ни категории, ни серии: подставлять нечего (ADR-56)
    const gauge = isChart && block.chartType === 'gauge'
    const allowed = isChart && !gauge ? ['x', 'series'] : []
    for (const [name, ref] of Object.entries(block.drill.params || {})) {
      if (typeof ref !== 'string' || !ref) {
        errors.push({ path: `blocks.${i}.drill.params.${name}`, message: 'значение должно быть строкой' })
        continue
      }
      if (ref.startsWith('$')) {
        errors.push({ path: `blocks.${i}.drill.params.${name}`, message: 'неизвестная ссылка: используйте ' + (isChart ? 'x, series, ' : '') + 'param.<name> или строку' })
        continue
      }
      const known =
        ref.startsWith('param.') || allowed.includes(ref) || (isTable && ref.startsWith('group.'))
      if (gauge && (ref === 'x' || ref === 'series')) {
        errors.push({
          path: `blocks.${i}.drill.params.${name}`,
          message: 'у диаграммы gauge нет x и series: подставляйте param.<name> или литерал'
        })
        continue
      }
      if (!known && /^(group\.|x$|series$)/.test(ref)) {
        errors.push({ path: `blocks.${i}.drill.params.${name}`, message: 'в диаграмме допустимы только x, series, param.<name> или литерал' })
      }
    }
  })

  for (const [key, ds] of Object.entries(template.datasets || {})) {
    if (ds?.queryId) {
      errors.push({ path: `datasets.${key}`, message: 'queryId больше не поддерживается: SQL задаётся в datasets.<key>.sql' })
    }
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
      columnsByDataKey[block.dataKey] = columnsByDataKey[block.dataKey] || []

      // заголовок-группа (children) — только визуальная группировка колонок
      const walkColumn = (col, cpath) => {
        if (Array.isArray(col.children)) {
          if (col.field || col.expression || col.aggregate || col.group || col.subtotal) {
            errors.push({
              path: cpath,
              message: 'у колонки-группы с children не может быть field/expression/aggregate/group'
            })
          }
          col.children.forEach((child, ci) => walkColumn(child, `${cpath}.children.${ci}`))
          return
        }
        columnsByDataKey[block.dataKey].push({ col, path: cpath })
      }

      const tableNames = new Set()
      for (const c of block.columns) {
        if (c.field) tableNames.add(c.field)
        if (c.expression && c.title) tableNames.add(c.title)
        if (Array.isArray(c.children)) {
          for (const child of c.children) {
            if (child.field) tableNames.add(child.field)
            if (child.expression && child.title) tableNames.add(child.title)
          }
        }
      }
      checkRules(block.rules, `${base}.rules`, tableNames, errors, warnings)
      checkStyles(block.styles, `${base}.styles`, new Set(block.columns.filter((c) => c.field).map((c) => c.field)), errors, warnings, template?.styles?.palette || {})

      const firstNew = columnsByDataKey[block.dataKey].length
      block.columns.forEach((col, ci) => walkColumn(col, `${base}.columns.${ci}`))

      // выражения разбираются статически: грамматика — ошибка, неизвестное поле — предупреждение
      const knownFields = new Set(
        columnsByDataKey[block.dataKey].slice(firstNew).map(({ col }) => col.field).filter(Boolean)
      )
      for (const { col, path } of columnsByDataKey[block.dataKey].slice(firstNew)) {
        if (typeof col.expression !== 'string' || !col.expression) continue
        try {
          const { fields } = analyzeExpression(col.expression)
          for (const name of fields) {
            if (knownFields.size && !knownFields.has(name)) {
              warnings.push({
                path,
                message: `в выражении поле «${name}» не встречается в колонках таблицы этого набора — проверьте имя`
              })
            }
          }
        } catch (e) {
          errors.push({ path, message: `ошибка выражения: ${e.message}` })
        }
      }

      let seenLeaf = false
      block.columns.forEach((col, ci) => {
        const cpath = `${base}.columns.${ci}`
        const hasField = typeof col.field === 'string' && col.field.length > 0
        const hasExpr = typeof col.expression === 'string' && col.expression.length > 0

        if (Array.isArray(col.children)) {
          if (col.group) {
            errors.push({ path: cpath, message: 'группировать по колонке-заголовку нельзя: нужна колонка с field' })
          }
          return
        }
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
      // Состав полей зависит от вида диаграммы (ADR-56): линия/столбцы/точки/круг —
      // категория + значение, gauge — одно значение, sankey — пара узлов и вес потока.
      const type = block.chartType
      const need = (field) => {
        if (typeof block[field] !== 'string' || !block[field]) {
          errors.push({
            path: `${base}.${field}`,
            message: `для диаграммы «${type}» обязательно поле ${field}`
          })
        }
      }
      const forbid = (field, why) => {
        if (block[field] !== undefined) {
          errors.push({ path: `${base}.${field}`, message: `для диаграммы «${type}» ${field} не используется: ${why}` })
        }
      }

      if (type === 'gauge') {
        need('yField')
        forbid('xField', 'значение одно, категории нет')
        forbid('seriesField', 'значение одно, серий нет')
        checkGaugeZones(block, base, errors)
        if (typeof block.min === 'number' && typeof block.max === 'number' && block.max <= block.min) {
          errors.push({ path: `${base}.max`, message: 'max должен быть больше min' })
        }
        if (block.aggregate === 'countDistinct') {
          warnings.push({
            path: `${base}.aggregate`,
            message: 'countDistinct на одном показателе даёт 1 — вероятно, нужен count или sum'
          })
        }
      } else if (type === 'sankey') {
        need('sourceField')
        need('targetField')
        need('valueField')
        forbid('xField', 'узлы задаются sourceField и targetField')
        forbid('yField', 'вес потока задаётся valueField')
        const names = [block.sourceField, block.targetField, block.valueField].filter(Boolean)
        if (new Set(names).size !== names.length) {
          errors.push({
            path: `${base}.targetField`,
            message: 'sourceField, targetField и valueField должны быть разными полями'
          })
        }
        if (block.aggregate && !['sum', 'avg', 'min', 'max', 'count'].includes(block.aggregate)) {
          errors.push({ path: `${base}.aggregate`, message: 'для sankey агрегат: sum, avg, min, max, count' })
        }
      } else {
        need('xField')
        need('yField')
        if (block.sourceField !== undefined || block.targetField !== undefined || block.valueField !== undefined) {
          errors.push({
            path: `${base}.chartType`,
            message: 'sourceField/targetField/valueField допустимы только для sankey'
          })
        }
        if (type === 'pie' && block.seriesField) {
          warnings.push({ path: `${base}.seriesField`, message: 'круговая диаграмма показывает одну серию' })
        }
      }

      if (block.min !== undefined || block.max !== undefined || block.unit !== undefined) {
        if (type !== 'gauge') {
          warnings.push({ path: `${base}.chartType`, message: 'min/max/unit учитываются только у gauge' })
        }
      }
      if ((block.thresholds !== undefined || block.zoneColors !== undefined) && type !== 'gauge') {
        warnings.push({ path: `${base}.chartType`, message: 'thresholds/zoneColors учитываются только у gauge' })
      }

      const cols = (columnsByDataKey[block.dataKey] || []).map((c) => c.col)
      const known = new Set(cols.map((c) => c.field).filter(Boolean))
      if (known.size) {
        for (const field of ['xField', 'yField', 'sourceField', 'targetField', 'valueField']) {
          if (!block[field]) continue
          if (!known.has(block[field])) {
            // диаграмма читает сырые строки набора и не обязана совпадать с колонками таблицы,
            // поэтому это предупреждение (опечатка), а не ошибка
            warnings.push({
              path: `${base}.${field}`,
              message:
                `поле «${block[field]}» не встречается в колонках таблицы этого набора — ` +
                `проверьте имя (для диаграммы это нормально, если колонка есть в SQL набора)`
            })
          }
        }
      }
    }
  })

  const allFields = new Set()
  for (const list of Object.values(columnsByDataKey)) {
    for (const { col } of list) if (col.field) allFields.add(col.field)
  }
  checkStyles(template?.styles, 'styles', allFields, errors, warnings, template?.styles?.palette || {})

  return { errors, warnings }
}

export default validateTemplate
