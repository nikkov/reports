// Валидация шаблона отчёта: структура (JSON Schema) + семантические правила.
// Блоки проверяются по своей ветке (text/table/chart), поэтому одна ошибка = одно сообщение,
// а не лавина от oneOf на все варианты сразу.
import Ajv2020 from 'ajv/dist/2020.js'
import schema from './schema.js'
import { analyzeExpression } from '../engine/expr.js'
import { looksLikeDateExpr, parseDateExpr } from './dates.js'

// Шаблон даты (ADR-88): только известные токены и разделители, и только для формата даты.
const DATE_PATTERN_RE = /YYYY|YY|MM|M|DD|D|HH|H|mm|ss/g
function checkDatePattern(fmt, path, errors) {
  if (!fmt || typeof fmt !== 'object' || fmt.pattern === undefined) return
  if (typeof fmt.pattern !== 'string' || fmt.pattern === '') {
    errors.push({ path: `${path}.pattern`, message: 'pattern: непустая строка с токенами даты' })
    return
  }
  if (fmt.style !== undefined && fmt.style !== 'date') {
    errors.push({ path: `${path}.pattern`, message: 'pattern допустим только для формата даты (style: "date")' })
  }
  const rest = fmt.pattern.replace(DATE_PATTERN_RE, '')
  // как на сервере: после токенов допустимы только цифры и разделители
  if (/[^\d\s.,:/;\-T]/.test(rest)) {
    errors.push({
      path: `${path}.pattern`,
      message: 'pattern: недопустимые буквы; допустимы YYYY, YY, MM, M, DD, D, HH, H, mm, ss'
    })
  }
}


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
      checkDatePattern(fmt, `${path}.columns.${field}.format`, errors)
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

// Высота блока (ADR-85): пиксели числом или процент от высоты окна строкой.
// У многоуровневого sankey узлов десятки — на 320 px подписи не читаются, но это
// забота шаблона, а не отдельного правила: ограничения общие для таблицы и диаграммы.
function checkBlockHeight(block, base, errors) {
  const height = block.height
  if (height === undefined) return
  if (typeof height === 'number') {
    if (!Number.isInteger(height) || height < 160 || height > 2000) {
      errors.push({ path: `${base}.height`, message: 'height: пиксели — целое 160…2000 или процент от окна («60%»)' })
    }
    return
  }
  if (typeof height === 'string') {
    const m = /^(\d{1,3})%$/.exec(height)
    if (!m) {
      errors.push({ path: `${base}.height`, message: 'height: пиксели — целое 160…2000 или процент от окна («60%»)' })
      return
    }
    const pct = Number(m[1])
    if (pct < 10 || pct > 100) {
      errors.push({ path: `${base}.height`, message: 'height: процент от высоты окна — от 10 до 100 («60%»)' })
    }
    return
  }
  errors.push({ path: `${base}.height`, message: 'height: пиксели — целое 160…2000 или процент от окна («60%»)' })
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
  const datasetNames = Object.keys(template.datasets || {})
  const datasetKeys = new Set(datasetNames)

  // Источники данных (ADR-76): блок connections описывает, куда идти за данными —
  // адрес, базу, пользователя, схему, режим SSL и имя переменной окружения с паролем.
  // Пароль в шаблоне не хранится (решение PO), поэтому поле password — ошибка.
  // Наборы и справочники ссылаются на источник по ключу; `default` — сервер из
  // окружения, его можно не объявлять.
  const connections = template.connections && typeof template.connections === 'object' ? template.connections : {}
  const connKeys = new Set(Object.keys(connections))
  const connKnown = (key) => !key || key === 'default' || connKeys.has(key)
  const idPattern = /^[a-z0-9][a-z0-9_-]*$/
  const envPattern = /^[A-Za-z_][A-Za-z0-9_]*$/
  const sslModes = ['disable', 'allow', 'prefer', 'require', 'verify-ca', 'verify-full']
  // Значение поля источника может быть целиком подстановкой ${ИМЯ} (ADR-77):
  // адрес и база берутся из окружения, а в шаблоне остаётся видно, откуда они.
  const envRefPattern = /^\$\{[A-Za-z_][A-Za-z0-9_]*\}$/
  const isRef = (value) => typeof value === 'string' && envRefPattern.test(value)
  const refValid = (value) => value === undefined || !String(value).includes('${') || isRef(String(value))

  for (const [key, conn] of Object.entries(connections)) {
    const base = `connections.${key}`
    if (!idPattern.test(key)) {
      errors.push({ path: base, message: 'ключ источника должен соответствовать ^[a-z0-9][a-z0-9_-]*$' })
    }
    if (!conn || typeof conn !== 'object' || Array.isArray(conn)) {
      errors.push({ path: base, message: 'источник должен быть объектом' })
      continue
    }
    if (Object.prototype.hasOwnProperty.call(conn, 'password')) {
      errors.push({
        path: `${base}.password`,
        message: 'пароль в шаблоне не хранится: укажите passwordEnv с именем переменной окружения'
      })
    }
    const kind = conn.kind || 'postgres'
    if (!['postgres', 'rest'].includes(kind)) {
      errors.push({ path: `${base}.kind`, message: `неизвестный kind «${kind}»: поддержаны postgres и rest` })
      continue
    }
    if (kind === 'postgres') {
      if (!conn.host) errors.push({ path: `${base}.host`, message: 'для postgres нужен host' })
      if (!conn.database) errors.push({ path: `${base}.database`, message: 'для postgres нужна database' })
      if (!conn.user) errors.push({ path: `${base}.user`, message: 'для postgres нужен user' })
      if (!conn.passwordEnv) {
        errors.push({
          path: `${base}.passwordEnv`,
          message: 'для postgres нужен passwordEnv — имя переменной окружения с паролем'
        })
      } else if (!envPattern.test(conn.passwordEnv)) {
        errors.push({ path: `${base}.passwordEnv`, message: 'passwordEnv должен быть именем переменной окружения' })
      }
      if (conn.schema && !envPattern.test(conn.schema) && !isRef(conn.schema)) {
        errors.push({ path: `${base}.schema`, message: 'schema должен быть идентификатором или ${ИМЯ_ПЕРЕМЕННОЙ}' })
      }
      const ssl = conn.sslmode || conn.sslMode
      if (ssl && !sslModes.includes(ssl)) {
        errors.push({
          path: `${base}.sslmode`,
          message: 'sslmode: disable, allow, prefer, require, verify-ca или verify-full'
        })
      }
      if (conn.connectTimeout !== undefined &&
          !(Number.isInteger(conn.connectTimeout) && conn.connectTimeout >= 1 && conn.connectTimeout <= 120)) {
        errors.push({ path: `${base}.connectTimeout`, message: 'connectTimeout — целое 1…120 секунд' })
      }
      for (const field of ['host', 'port', 'database', 'user', 'schema']) {
        if (!refValid(conn[field])) {
          errors.push({
            path: `${base}.${field}`,
            message: 'подстановка окружения задаётся целиком: ${ИМЯ_ПЕРЕМЕННОЙ}'
          })
        }
      }
      if (conn.statementTimeoutMs !== undefined &&
          !(Number.isInteger(conn.statementTimeoutMs) &&
            conn.statementTimeoutMs >= 0 && conn.statementTimeoutMs <= 600000)) {
        errors.push({
          path: `${base}.statementTimeoutMs`,
          message: 'statementTimeoutMs — целое 0…600000 (0 — без ограничения)'
        })
      }
    } else {
      // rest зарезервирован: структура проверяется, исполнение появится позже
      if (!conn.baseUrl) errors.push({ path: `${base}.baseUrl`, message: 'для rest нужен baseUrl' })
    }
  }
  if (connections.default && (connections.default.kind || 'postgres') !== 'postgres') {
    errors.push({ path: 'connections.default', message: 'источник default должен быть postgres' })
  }

  // Способы получения значений параметров (ADR-22): дата/время, статический набор, справочник из БД.
  const paramsByName = new Map(params.map((p) => [p.name, p]))

  // Текстовый блок может брать строку из набора данных (ADR-101): набор обязан быть объявлен,
  // а подстановки {{data.<колонка>}} без dataKey не на что опереть.
  ;(template.blocks || []).forEach((b, index) => {
    if (b?.type !== 'text') return
    if (b.dataKey && !datasetKeys.has(b.dataKey)) {
      errors.push({
        path: `blocks.${index}.dataKey`,
        message: `набор «${b.dataKey}» не объявлен в datasets`
      })
      return
    }
    if (!b.dataKey && /\{\{[^}]*\bdata\./.test(String(b.text || ''))) {
      errors.push({
        path: `blocks.${index}.text`,
        message: 'подстановка {{data.<колонка>}} требует dataKey — набор, откуда брать строку'
      })
    }
  })

  params.forEach((p, index) => {
    const path = `params.${index}`
    const src = p.source
    const kind = src?.kind

    // Арифметика дат в default (ADR-92): выражения допустимы у date и datetime,
    // опечатка в шаблоне — ошибка, а не пустое значение.
    if ((p.type === 'date' || p.type === 'datetime') && p.default !== undefined && looksLikeDateExpr(p.default)) {
      const parsed = parseDateExpr(p.default)
      if (!parsed.ok) {
        errors.push({ path: `${path}.default`, message: `default: ${parsed.error}` })
      }
    }

    // Пустой элемент списка задаётся шаблоном явно (ADR-84): интерфейс больше не
    // подставляет «—» сам. Значения value/label проверяет схема, здесь — область действия.
    // Проверяем до раннего выхода: она не зависит от source.
    if (p.emptyOption !== undefined) {
      if (kind !== 'lookup') {
        errors.push({ path: `${path}.emptyOption`, message: 'emptyOption допустим только у справочника (source.kind=lookup)' })
      }
      if (p.multiple) {
        errors.push({ path: `${path}.emptyOption`, message: 'emptyOption не сочетается с multiple: пустой элемент — для одиночного выбора' })
      }
    }

    if (!src) {
      // старый стиль: select обязан иметь options, иначе пользователю нечего выбрать
      if (p.type === 'select' && !(p.options && p.options.length)) {
        errors.push({ path, message: 'для select нужен source.options или source.kind=lookup' })
      }
      return
    }

    if (kind === 'lookup') {
      // Справочник — это набор данных (ADR-78): SQL и соединение живут в datasets,
      // параметр ссылается на набор. Старый source.sql отклоняется с подсказкой.
      if (src.sql) {
        errors.push({
          path: `${path}.source.sql`,
          message: 'SQL справочника задаётся набором datasets.<ключ>: source.sql больше не поддерживается'
        })
      }
      if (!src.dataset) {
        errors.push({ path: `${path}.source`, message: 'для source.kind=lookup обязателен dataset — набор из datasets' })
      } else if (!datasetKeys.has(src.dataset)) {
        errors.push({ path: `${path}.source.dataset`, message: `набор «${src.dataset}» не объявлен в datasets` })
      }
      if (src.queryId) {
        errors.push({ path: `${path}.source`, message: 'queryId больше не поддерживается' })
      }
      if (src.options) errors.push({ path: `${path}.source`, message: 'lookup и options взаимоисключающие' })
      if (src.connection) {
        errors.push({ path: `${path}.source.connection`, message: 'соединение задаётся в datasets.<ключ>.connection' })
      }
      if (p.type !== 'select') {
        errors.push({ path: `${path}.type`, message: 'параметр со справочником должен иметь type=select' })
      }
    } else if (src.dataset) {
      errors.push({ path: `${path}.source.dataset`, message: 'dataset допустим только у source.kind=lookup' })
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
    // цепочка уровней sankey отдаёт значения уровней в переход (ADR-72)
    const chain = isChart && block.chartType === 'sankey' && Array.isArray(block.levels)
    const allowed = isChart && !gauge ? ['x', 'series'] : []
    for (const [name, ref] of Object.entries(block.drill.params || {})) {
      if (typeof ref !== 'string' || !ref) {
        errors.push({ path: `blocks.${i}.drill.params.${name}`, message: 'значение должно быть строкой' })
        continue
      }
      if (ref === 'node') {
        if (!chain) {
          errors.push({ path: `blocks.${i}.drill.params.${name}`, message: 'node допустим только в sankey с levels' })
        }
        continue
      }
      if (ref.startsWith('level.')) {
        const field = ref.slice(6)
        if (!chain) {
          errors.push({
            path: `blocks.${i}.drill.params.${name}`,
            message: 'level.<поле> допустим только в sankey с levels'
          })
        } else if (!block.levels.includes(field)) {
          errors.push({ path: `blocks.${i}.drill.params.${name}`, message: `уровня «${field}» нет в levels` })
        }
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
    if (ds?.connection && !connKnown(ds.connection)) {
      errors.push({
        path: `datasets.${key}.connection`,
        message: `источник «${ds.connection}» не объявлен в connections`
      })
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

    if (!datasetNames.includes(block.dataKey)) {
      errors.push({
        path: `${base}.dataKey`,
        message: `набор данных «${block.dataKey}» не объявлен в datasets (есть: ${datasetNames.join(', ') || '—'})`
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
        checkDatePattern(col.format, `${cpath}.format`, errors)
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

      // Порядок деталей группы (ADR-71): имеет смысл только там, где группа раскрывается,
      // то есть у блока с серверной агрегацией
      if (block.detailOrder !== undefined) {
        const list = block.detailOrder
        const aggregated = block.aggregate && typeof block.aggregate === 'object'
        if (!Array.isArray(list) || list.length < 1 || list.length > 4) {
          errors.push({ path: `${base}.detailOrder`, message: 'detailOrder: от 1 до 4 полей' })
        } else if (!aggregated) {
          errors.push({ path: `${base}.detailOrder`, message: 'detailOrder имеет смысл только с aggregate' })
        } else {
          list.forEach((item, i) => {
            const path = `${base}.detailOrder.${i}`
            if (!item || typeof item !== 'object' || Array.isArray(item)) {
              errors.push({ path, message: 'элемент — объект {field, dir}' })
              return
            }
            if (typeof item.field !== 'string' || !/^[A-Za-z_][A-Za-z0-9_]*$/.test(item.field)) {
              errors.push({ path: `${path}.field`, message: 'нужно имя поля' })
            }
            if (item.dir !== undefined && item.dir !== 'asc' && item.dir !== 'desc') {
              errors.push({ path: `${path}.dir`, message: 'направление: asc или desc' })
            }
          })
        }
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
      } else if (type === 'band') {
        // полоса: значение и одна или несколько пар границ (ADR-58, ADR-69)
        need('xField')
        need('yField')
        forbid('seriesField', 'в полосе одна серия значений')
        if (block.bands !== undefined) {
          if (!Array.isArray(block.bands) || block.bands.length < 1 || block.bands.length > 4) {
            errors.push({ path: `${base}.bands`, message: 'bands: от 1 до 4 коридоров' })
          } else {
            block.bands.forEach((corridor, i) => {
              const path = `${base}.bands.${i}`
              if (!corridor || typeof corridor !== 'object' || Array.isArray(corridor)) {
                errors.push({ path, message: 'коридор — объект { lowField, highField }' })
                return
              }
              if (!corridor.lowField) errors.push({ path: `${path}.lowField`, message: 'нужен lowField' })
              if (!corridor.highField) errors.push({ path: `${path}.highField`, message: 'нужен highField' })
              if (corridor.lowField && corridor.highField && corridor.lowField === corridor.highField) {
                errors.push({ path: `${path}.highField`, message: 'границы коридора — разные поля' })
              }
              const bounds = [corridor.lowField, corridor.highField].filter(Boolean)
              if (block.yField && bounds.includes(block.yField)) {
                errors.push({ path: `${path}.lowField`, message: 'значение должно быть отдельным полем, не границей' })
              }
            })
            if (block.lowField !== undefined || block.highField !== undefined) {
              errors.push({
                path: `${base}.lowField`,
                message: 'вместе с bands поля lowField/highField не задаются'
              })
            }
          }
        } else {
          need('lowField')
          need('highField')
          const names = [block.lowField, block.highField].filter(Boolean)
          if (names.length === 2 && block.lowField === block.highField) {
            errors.push({ path: `${base}.highField`, message: 'lower и upper — разные поля' })
          }
          if (block.lowField && block.highField && block.yField &&
              [block.lowField, block.highField].includes(block.yField)) {
            errors.push({ path: `${base}.yField`, message: 'значение должно быть отдельным полем, не границей' })
          }
        }
        if (block.sourceField !== undefined || block.targetField !== undefined || block.valueField !== undefined) {
          errors.push({ path: `${base}.chartType`, message: 'sourceField/targetField/valueField допустимы только для sankey' })
        }
        if (block.levels !== undefined) {
          errors.push({ path: `${base}.levels`, message: 'levels допустимы только для sankey' })
        }
      } else if (type === 'sankey') {
        // Поток задаётся либо парой source/target, либо цепочкой уровней (ADR-68)
        need('valueField')
        if (block.levels !== undefined) {
          if (!Array.isArray(block.levels) || block.levels.length < 2 || block.levels.length > 4) {
            errors.push({ path: `${base}.levels`, message: 'levels: от 2 до 4 полей' })
          } else {
            if (block.levels.some((f) => typeof f !== 'string' || !f)) {
              errors.push({ path: `${base}.levels`, message: 'в levels только непустые имена полей' })
            } else if (new Set(block.levels).size !== block.levels.length) {
              errors.push({ path: `${base}.levels`, message: 'поля уровней не должны повторяться' })
            }
            if (block.sourceField !== undefined || block.targetField !== undefined) {
              errors.push({
                path: `${base}.sourceField`,
                message: 'у цепочки уровней sourceField/targetField не задаются'
              })
            }
          }
        } else {
          need('sourceField')
          need('targetField')
        }
        forbid('xField', 'узлы задаются sourceField/targetField или levels')
        forbid('yField', 'вес потока задаётся valueField')
        if (block.bands !== undefined) {
          errors.push({ path: `${base}.bands`, message: 'bands допустимы только для band' })
        }
        const names = (Array.isArray(block.levels)
          ? [...block.levels, block.valueField]
          : [block.sourceField, block.targetField, block.valueField]).filter(Boolean)
        if (new Set(names).size !== names.length) {
          errors.push({
            path: Array.isArray(block.levels) ? `${base}.levels` : `${base}.targetField`,
            message: Array.isArray(block.levels)
              ? 'поля уровней и веса должны быть разными'
              : 'sourceField, targetField и valueField должны быть разными полями'
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
        if (block.lowField !== undefined || block.highField !== undefined) {
          errors.push({ path: `${base}.chartType`, message: 'lowField/highField допустимы только для band' })
        }
        if (block.levels !== undefined) {
          errors.push({ path: `${base}.levels`, message: 'levels допустимы только для sankey' })
        }
        if (block.bands !== undefined) {
          errors.push({ path: `${base}.bands`, message: 'bands допустимы только для band' })
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
        for (const field of ['xField', 'yField', 'sourceField', 'targetField', 'valueField', 'lowField', 'highField']) {
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

  // Стили набора (ADR-91): действуют на блоки этого набора, блок может переопределить.
  // Поля для styles.columns берём из блоков набора; палитра — общая, из styles шаблона.
  for (const [key, ds] of Object.entries(template.datasets || {})) {
    if (!ds?.styles) continue
    const fields = new Set(
      (columnsByDataKey[key] || []).map(({ col }) => col.field).filter(Boolean)
    )
    checkStyles(ds.styles, `datasets.${key}.styles`, fields, errors, warnings, template?.styles?.palette || {})
  }

  // Автозагрузка (ADR-89): с выключенной автозагрузкой фоновые обновления не запустятся
  if (template.autoLoad === false) {
    if (Number(template.refreshSeconds) > 0) {
      warnings.push({
        path: 'autoLoad',
        message: 'refreshSeconds не сработает: данные загружаются только по кнопке (autoLoad: false)'
      })
    }
    if (template.live === true) {
      warnings.push({
        path: 'autoLoad',
        message: 'live не сработает: данные загружаются только по кнопке (autoLoad: false)'
      })
    }
  }

  // Высота блоков (ADR-85): у таблицы и диаграммы — пиксели или процент от окна
  ;(template.blocks || []).forEach((block, i) => {
    if (block.type !== 'table' && block.type !== 'chart') {
      if (block.height !== undefined) {
        errors.push({ path: `blocks.${i}.height`, message: 'height допустим только у таблицы и диаграммы' })
      }
      return
    }
    checkBlockHeight(block, `blocks.${i}`, errors)
  })

  return { errors, warnings }
}

export default validateTemplate
