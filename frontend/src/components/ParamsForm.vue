<template>
  <form class="params" @submit.prevent="$emit('submit')">
    <label v-for="p in params" :key="p.name" class="field">
      <span class="label">{{ p.label || p.name }}</span>

      <!-- дата / время -->
      <input
        v-if="kind(p) === 'date' || kind(p) === 'datetime'"
        :type="kind(p) === 'date' ? 'date' : 'datetime-local'"
        :value="modelValue[p.name] ?? ''"
        :required="p.required"
        @change="update(p.name, $event.target.value)"
      />

      <!-- множественный выбор: выпадающий список с чекбоксами -->
      <MultiSelect
        v-else-if="(kind(p) === 'lookup' || kind(p) === 'static') && p.multiple"
        :options="optionsFor(p)"
        :model-value="String(modelValue[p.name] ?? '')"
        :disabled="isBlocked(p)"
        @update:model-value="update(p.name, $event)"
      />

      <!-- одиночный справочник из БД или статический набор -->
      <select
        v-else-if="kind(p) === 'lookup' || kind(p) === 'static'"
        :disabled="isBlocked(p)"
        :required="p.required"
        @change="onSelect(p, $event)"
      >
        <option v-if="!p.multiple" value="" disabled>—</option>
        <option
          v-for="o in optionsFor(p)"
          :key="o.value"
          :value="o.value"
          :selected="!p.multiple && isSelected(p, o.value)"
        >
          {{ o.label }}
        </option>
      </select>

      <input
        v-else-if="p.type === 'bool'"
        type="checkbox"
        :checked="modelValue[p.name] === 'true' || modelValue[p.name] === true"
        @change="update(p.name, $event.target.checked ? 'true' : 'false')"
      />

      <input
        v-else-if="p.type === 'int' || p.type === 'float'"
        type="number"
        :step="p.type === 'float' ? 'any' : '1'"
        :value="modelValue[p.name] ?? ''"
        :required="p.required"
        @change="update(p.name, $event.target.value)"
      />

      <input
        v-else
        type="text"
        :value="modelValue[p.name] ?? ''"
        :required="p.required"
        @change="update(p.name, $event.target.value)"
      />

      <span v-if="errors[p.name]" class="err">{{ errors[p.name] }}</span>
    </label>

    <button type="submit" :disabled="loading">{{ loading ? 'загрузка…' : 'Обновить' }}</button>
  </form>
</template>

<script setup>
// Параметры отчёта (ADR-22): способ получения задаётся в шаблоне.
// lookup → GET /api/lookups/{queryId} с кешем в памяти; dependsOn → каскад от родителя.
// multiple → значение уходит строкой через запятую (в SQL: ANY(string_to_array(:p, ','))).
import { onMounted, reactive, ref, watch } from 'vue'
import { api } from '../api/client.js'
import MultiSelect from './MultiSelect.vue'

const props = defineProps({
  params: { type: Array, required: true },
  modelValue: { type: Object, required: true },
  loading: { type: Boolean, default: false }
})
const emit = defineEmits(['update:modelValue', 'submit', 'labels', 'ready'])

const fetched = reactive({}) // имя параметра → [{value,label}]
const errors = reactive({})
const cache = new Map() // queryId + параметры → список

const kind = (p) =>
  p.source?.kind || (p.type === 'date' ? 'date' : p.type === 'select' ? 'static' : 'text')

function optionsFor(p) {
  if (p.source?.kind === 'static') return p.source.options || p.options || []
  return fetched[p.name] || []
}

// выбранность задаём на каждом option: для <select multiple> это надёжнее value-биндинга,
// который применяется до появления опций после загрузки справочника
function isSelected(p, value) {
  const raw = String(props.modelValue[p.name] ?? '')
  if (!raw) return false
  return raw.split(',').some((v) => v === String(value))
}

function isBlocked(p) {
  const dep = p.source?.dependsOn
  return !!dep && !props.modelValue[dep]
}

function update(name, value) {
  emit('update:modelValue', { ...props.modelValue, [name]: value })
}

function onSelect(p, event) {
  update(p.name, event.target.value)
}

async function loadOptions(p) {
  if (p.source?.kind !== 'lookup') return
  const dep = p.source.dependsOn
  if (dep && !props.modelValue[dep]) {
    fetched[p.name] = []
    return
  }

  const query = dep ? { [dep]: props.modelValue[dep] } : {}
  const key = p.source.queryId + '|' + JSON.stringify(query)

  if (cache.has(key)) {
    fetched[p.name] = cache.get(key)
  } else {
    try {
      const list = await api.lookups(p.source.queryId, query)
      cache.set(key, list)
      fetched[p.name] = list
    } catch (e) {
      errors[p.name] = e.message
      fetched[p.name] = []
      return
    }
  }
  delete errors[p.name]

  // автовыбор: default или первое значение, чтобы отчёт строился сразу
  const list = fetched[p.name]
  const current = props.modelValue[p.name]

  if (p.multiple) {
    // после каскадной перезагрузки оставляем только существующие значения; если ничего не осталось — все
    const kept = String(current || '')
      .split(',')
      .filter((v) => v && list.some((o) => String(o.value) === v))
    if (!kept.length && list.length) {
      update(p.name, list.map((o) => String(o.value)).join(','))
    } else if (kept.join(',') !== String(current || '')) {
      update(p.name, kept.join(','))
    }
    return
  }

  const valid = list.some((o) => o.value === current)

  if (!valid && list.length) {
    update(p.name, p.default !== undefined ? String(p.default) : String(list[0].value))
  }
}

async function initAll() {
  for (const p of props.params) {
    if (p.source?.kind === 'static' && !props.modelValue[p.name]) {
      const first = (p.source.options || p.options || [])[0]
      if (first) update(p.name, String(first.value))
    }
  }
  for (const p of props.params) await loadOptions(p)
  // все справочники загружены и значения выставлены — можно строить отчёт
  emit('ready')
}

// Подписи выбранных значений: в тексте отчёта пользователь должен видеть NAME, а не ID.
watch(
  () => [props.modelValue, { ...fetched }],
  () => {
    const labels = {}
    for (const p of props.params) {
      const list = optionsFor(p)
      const raw = String(props.modelValue[p.name] ?? '')
      const picked = raw
        .split(',')
        .filter(Boolean)
        .map((v) => list.find((o) => String(o.value) === v)?.label ?? v)
      labels[p.name] = picked.join(', ')
    }
    emit('labels', labels)
  },
  { deep: true, immediate: true }
)

onMounted(initAll)

// каскад: смена родителя перезагружает зависимые списки
watch(
  () => props.params.map((p) => [p.name, props.modelValue[p.name]]),
  async () => {
    for (const p of props.params) {
      if (p.source?.kind === 'lookup' && p.source.dependsOn) await loadOptions(p)
    }
  },
  { deep: true }
)
</script>

<style scoped>
.params {
  display: flex;
  flex-wrap: wrap;
  gap: 12px;
  align-items: flex-end;
  padding: 10px 12px;
  background: #fff;
  border: 1px solid #dfe3e8;
  border-radius: 8px;
  margin-bottom: 12px;
}
.field {
  display: flex;
  flex-direction: column;
  gap: 4px;
  font-size: 12px;
  color: #5b6472;
}
.field input,
.field select {
  font: inherit;
  padding: 4px 6px;
  border: 1px solid #cfd6de;
  border-radius: 6px;
  min-width: 150px;
}
.field select[multiple] {
  min-height: 66px;
}
.err {
  color: #8c1f1f;
}
.params button {
  font: inherit;
  padding: 6px 14px;
  border: 1px solid #4f8ef7;
  background: #4f8ef7;
  color: #fff;
  border-radius: 6px;
  cursor: pointer;
  height: 30px;
}
.params button:disabled {
  opacity: 0.6;
  cursor: default;
}
</style>
