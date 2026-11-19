<template>
  <!-- Легенда-строка (ADR-160): маркеры «цвет + подпись» для расшифровки цветов табло -->
  <div class="legend-block" :class="{ carded: block.card === true }">
    <span v-if="block.title" class="legend-title">{{ block.title }}</span>
    <ul class="legend-items">
      <li
        v-for="(item, i) in items"
        :key="i"
        class="legend-item"
        :class="{ hidden: isHidden(item) }"
        :role="forDataKey ? 'button' : undefined"
        :title="forDataKey ? 'Показать или скрыть на графиках' : undefined"
        @click="onClick(item)"
      >
        <span class="marker" :class="`shape-${item.shape || 'square'}`" :style="{ background: colorOf(item) }"></span>
        <span class="legend-label">{{ item.label }}</span>
      </li>
    </ul>
  </div>
</template>

<script setup>
import { computed } from 'vue'
import { isLegendHidden, toggleLegend } from './legendFilter.js'

const props = defineProps({
  block: { type: Object, required: true }
})

// Клик по маркеру скрывает серию с тем же именем на графиках набора (ADR-161).
const forDataKey = computed(() => props.block.forDataKey || '')
// Ключ фильтра — имя серии (ADR-161): по умолчанию совпадает с подписью маркера,
// а items[].series позволяет подписать маркер иначе, чем называется серия.
const seriesKey = (item) => (item && item.series) || (item && item.label) || ''
const isHidden = (item) => forDataKey.value && isLegendHidden(forDataKey.value, seriesKey(item))
function onClick(item) {
  if (!forDataKey.value) return
  toggleLegend(forDataKey.value, seriesKey(item))
}

const FALLBACK = ['var(--rep-ok)', 'var(--rep-accent)', 'var(--rep-danger)', 'var(--rep-warn)']
const items = computed(() => (Array.isArray(props.block.items) ? props.block.items : []))
const colorOf = (item, index) => item.color || FALLBACK[index % FALLBACK.length]
</script>

<style scoped>
.legend-block {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 8px 16px;
  padding: 6px 0;
}
.legend-block.carded {
  padding: 10px 12px;
  background: var(--rep-surface);
  border: 1px solid var(--rep-border);
  border-radius: 8px;
}
.legend-title {
  font-size: 11px;
  letter-spacing: 0.04em;
  text-transform: uppercase;
  color: var(--rep-text-dim);
  font-weight: 600;
}
.legend-items {
  display: flex;
  flex-wrap: wrap;
  gap: 6px 14px;
  margin: 0;
  padding: 0;
  list-style: none;
}
.legend-item {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  font-size: 12px;
  color: var(--rep-text-muted);
  user-select: none;
}
.legend-item[role='button'] {
  cursor: pointer;
}
.legend-item[role='button']:hover {
  color: var(--rep-text);
}
.legend-item.hidden {
  opacity: 0.4;
  text-decoration: line-through;
}
.marker {
  display: inline-block;
  width: 10px;
  height: 10px;
  border-radius: 2px;
}
.marker.shape-circle {
  border-radius: 50%;
}
.marker.shape-line {
  width: 16px;
  height: 2px;
  border-radius: 1px;
}
@media print {
  .legend-block.carded {
    background: #ffffff;
  }
}
</style>
