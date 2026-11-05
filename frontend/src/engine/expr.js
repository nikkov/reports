// Минимальный вычислитель выражений для отчёта (Ф1).
// Грамматика закрытая: числа, ссылки на поля, + - * /, унарный минус, скобки,
// агрегатные функции sum/avg/min/max/count. Область действия передаётся снаружи
// колбэками field() и agg(), поэтому одна и та же функция работает для строки,
// группы и общего итога (полная семантика областей — задача 2a).

const FUNCS = new Set(['sum', 'avg', 'min', 'max', 'count'])

function tokenize(input) {
  const tokens = []
  let i = 0
  while (i < input.length) {
    const ch = input[i]
    if (/\s/.test(ch)) {
      i++
      continue
    }
    if (/[0-9.]/.test(ch)) {
      let j = i
      while (j < input.length && /[0-9.eE]/.test(input[j])) j++
      tokens.push({ type: 'num', value: Number(input.slice(i, j)) })
      i = j
      continue
    }
    if (/[a-zA-Z_]/.test(ch)) {
      let j = i
      while (j < input.length && /[a-zA-Z0-9_]/.test(input[j])) j++
      const name = input.slice(i, j)
      i = j
      if (FUNCS.has(name)) {
        tokens.push({ type: 'func', value: name })
      } else {
        // идентификатор перед «(» — это попытка вызвать несуществующую функцию
        let k = j
        while (k < input.length && /\s/.test(input[k])) k++
        if (input[k] === '(') throw new Error(`неизвестная функция «${name}»`)
        tokens.push({ type: 'ident', value: name })
      }
      continue
    }
    if ('+-*/()'.includes(ch)) {
      tokens.push({ type: ch === '(' || ch === ')' ? ch : 'op', value: ch })
      i++
      continue
    }
    throw new Error(`недопустимый символ «${ch}» в выражении`)
  }
  return tokens
}

/**
 * Проверка выражения без вычисления: возвращает используемые поля и агрегаты
 * либо бросает ошибку грамматики. Значения подменяются единицей, чтобы деление
 * и null-ветки не влияли на разбор.
 * @param {string} expression
 * @returns {{fields: string[], aggs: Array<{fn: string, field: string}>}}
 */
export function analyzeExpression(expression) {
  const fields = new Set()
  const aggs = []
  evaluate(expression, {
    field: (name) => {
      fields.add(name)
      return 1
    },
    agg: (fn, name) => {
      aggs.push({ fn, field: name })
      fields.add(name)
      return 1
    }
  })
  return { fields: [...fields], aggs }
}

/**
 * @param {string} expression
 * @param {{field: (name:string)=>number|null|undefined, agg: (fn:string, name:string)=>number|null|undefined}} scope
 * @returns {number|null}
 */
export function evaluate(expression, scope) {
  if (!expression) return null
  const tokens = tokenize(expression)
  let pos = 0

  const peek = () => tokens[pos]
  const next = () => tokens[pos++]

  function primary() {
    const t = next()
    if (!t) throw new Error('неожиданный конец выражения')

    if (t.type === 'num') return t.value

    if (t.type === 'ident') {
      const v = scope.field(t.value)
      return v === undefined || v === null ? null : Number(v)
    }

    if (t.type === 'func') {
      const open = next()
      if (!open || open.type !== '(') throw new Error(`после ${t.value} ожидалась «(»`)
      const arg = next()
      if (!arg || arg.type !== 'ident') throw new Error(`аргумент ${t.value}() должен быть именем поля`)
      const close = next()
      if (!close || close.type !== ')') throw new Error('не закрыта скобка в функции')
      const v = scope.agg(t.value, arg.value)
      return v === undefined || v === null ? null : Number(v)
    }

    if (t.type === '(') {
      const v = add()
      const close = next()
      if (!close || close.type !== ')') throw new Error('не закрыта скобка')
      return v
    }

    if (t.type === 'op' && t.value === '-') {
      const v = primary()
      return v === null ? null : -v
    }

    throw new Error(`неожиданный токен «${t.value}»`)
  }

  function mul() {
    let v = unary()
    for (;;) {
      const t = peek()
      if (t && t.type === 'op' && (t.value === '*' || t.value === '/')) {
        next()
        const r = unary()
        if (v === null || r === null) v = null
        else if (t.value === '*') v = v * r
        else v = r === 0 ? null : v / r
      } else return v
    }
  }

  function unary() {
    const t = peek()
    if (t && t.type === 'op' && t.value === '-') {
      next()
      const v = unary()
      return v === null ? null : -v
    }
    return primary()
  }

  function add() {
    let v = mul()
    for (;;) {
      const t = peek()
      if (t && t.type === 'op' && (t.value === '+' || t.value === '-')) {
        next()
        const r = mul()
        if (v === null || r === null) v = null
        else v = t.value === '+' ? v + r : v - r
      } else return v
    }
  }

  const result = add()
  if (pos < tokens.length) throw new Error('лишние символы в выражении')
  if (result === null || !Number.isFinite(result)) return null
  return result
}

export default evaluate
