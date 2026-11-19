// Общая фильтрация серий по клику на легенду (ADR-161).
// Легенда и графики — разные блоки, поэтому состояние живёт в модуле: блок легенды
// переключает имя серии, а графики того же набора её прячут.
import { reactive } from 'vue'

const state = reactive({ hidden: {} })

const keyOf = (dataKey, label) => `${dataKey || ''}|${label || ''}`

export function toggleLegend(dataKey, label) {
  const key = keyOf(dataKey, label)
  if (state.hidden[key]) delete state.hidden[key]
  else state.hidden[key] = true
}

export function isLegendHidden(dataKey, label) {
  return !!state.hidden[keyOf(dataKey, label)]
}

export function hiddenLabels(dataKey) {
  const prefix = `${dataKey || ''}|`
  return Object.keys(state.hidden)
    .filter((key) => key.startsWith(prefix))
    .map((key) => key.slice(prefix.length))
}

export function resetLegend() {
  state.hidden = {}
}
