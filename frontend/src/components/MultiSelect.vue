<template>
  <div class="ms" :class="{ disabled }" ref="root">
    <button
      type="button"
      class="ms-trigger"
      :disabled="disabled"
      :aria-expanded="open"
      data-testid="ms-trigger"
      @click="toggleOpen"
      @keydown.down.prevent="open = true"
      @keydown.esc="open = false"
    >
      <span class="ms-summary" :class="{ muted: !selected.length }">{{ summary }}</span>
      <span class="ms-caret">{{ open ? '▴' : '▾' }}</span>
    </button>

    <div v-if="open" class="ms-panel" data-testid="ms-panel" @click.stop>
      <div class="ms-actions">
        <button type="button" @click="selectAll">Все</button>
        <button type="button" @click="clearAll">Очистить</button>
      </div>
      <label v-for="o in options" :key="o.value" class="ms-item">
        <input
          type="checkbox"
          :value="o.value"
          :checked="selected.includes(String(o.value))"
          @change="toggle(String(o.value))"
        />
        <span>{{ o.label }}</span>
      </label>
      <div v-if="!options.length" class="ms-empty">нет доступных значений</div>
    </div>
  </div>
</template>

<script setup>
// Мультивыбор в виде выпадающего списка с чекбоксами (ADR-22).
// Значение наружу — строка через запятую (в SQL: ANY(string_to_array(:p, ','))).
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'

const props = defineProps({
  options: { type: Array, default: () => [] },
  modelValue: { type: String, default: '' },
  disabled: { type: Boolean, default: false },
  placeholder: { type: String, default: 'не выбрано' }
})
const emit = defineEmits(['update:modelValue'])

const open = ref(false)
const root = ref(null)

const selected = computed(() =>
  String(props.modelValue || '')
    .split(',')
    .filter(Boolean)
)

const labels = computed(() =>
  selected.value.map((v) => props.options.find((o) => String(o.value) === v)?.label ?? v)
)

const summary = computed(() => {
  if (!selected.value.length) return props.placeholder
  if (props.options.length && selected.value.length >= props.options.length) {
    return `Все (${selected.value.length})`
  }
  if (labels.value.length <= 2) return labels.value.join(', ')
  return `${labels.value.slice(0, 2).join(', ')} +${labels.value.length - 2}`
})

function emitSelected(list) {
  emit('update:modelValue', list.join(','))
}

function toggle(value) {
  const next = selected.value.includes(value)
    ? selected.value.filter((v) => v !== value)
    : [...selected.value, value]
  emitSelected(next)
}

function selectAll() {
  emitSelected(props.options.map((o) => String(o.value)))
}

function clearAll() {
  emitSelected([])
}

function toggleOpen() {
  if (!props.disabled) open.value = !open.value
}

function onDocClick(e) {
  if (open.value && root.value && !root.value.contains(e.target)) open.value = false
}

onMounted(() => document.addEventListener('mousedown', onDocClick))
onBeforeUnmount(() => document.removeEventListener('mousedown', onDocClick))
</script>

<style scoped>
.ms {
  position: relative;
  min-width: 220px;
}
.ms-trigger {
  width: 100%;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
  font: inherit;
  padding: 4px 8px;
  border: 1px solid #cfd6de;
  border-radius: 6px;
  background: #fff;
  cursor: pointer;
  min-height: 28px;
  text-align: left;
}
.ms.disabled .ms-trigger {
  background: #f5f6f8;
  cursor: default;
  opacity: 0.7;
}
.ms-summary {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.ms-summary.muted {
  color: #9aa3ae;
}
.ms-caret {
  color: #7b8492;
}
.ms-panel {
  position: absolute;
  z-index: 20;
  top: calc(100% + 4px);
  left: 0;
  min-width: 100%;
  max-height: 260px;
  overflow: auto;
  background: #fff;
  border: 1px solid #cfd6de;
  border-radius: 8px;
  box-shadow: 0 8px 24px rgba(20, 30, 45, 0.12);
  padding: 6px;
}
.ms-actions {
  display: flex;
  gap: 6px;
  padding: 2px 4px 6px;
  border-bottom: 1px solid #eef1f5;
  margin-bottom: 4px;
}
.ms-actions button {
  font: inherit;
  font-size: 12px;
  padding: 2px 8px;
  border: 1px solid #cfd6de;
  border-radius: 6px;
  background: #fff;
  cursor: pointer;
}
.ms-item {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 4px 6px;
  border-radius: 6px;
  cursor: pointer;
  font-size: 13px;
  color: #1c1f23;
}
.ms-item:hover {
  background: #f2f6fd;
}
.ms-empty {
  padding: 6px;
  color: #9aa3ae;
  font-size: 12px;
}
</style>
