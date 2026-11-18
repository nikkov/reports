<template>
  <button
    class="theme-toggle"
    type="button"
    :title="isDark ? 'Светлая тема' : 'Тёмная тема'"
    :aria-label="isDark ? 'Включить светлую тему' : 'Включить тёмную тему'"
    :aria-pressed="isDark ? 'true' : 'false'"
    @click="toggle"
  >
    <span aria-hidden="true">{{ isDark ? '☀' : '☾' }}</span>
    <span class="label">{{ isDark ? 'Светлая' : 'Тёмная' }}</span>
  </button>
</template>

<script setup>
import { onBeforeUnmount, onMounted, ref } from 'vue'
import { currentTheme, toggleTheme } from '../theme/index.js'

const isDark = ref(currentTheme() === 'dark')

function sync() {
  isDark.value = currentTheme() === 'dark'
}

function toggle() {
  toggleTheme()
  sync()
}

onMounted(() => {
  sync()
  window.addEventListener('rep:theme', sync)
})

onBeforeUnmount(() => window.removeEventListener('rep:theme', sync))
</script>

<style scoped>
.theme-toggle {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 4px 10px;
  font: inherit;
  font-size: 12px;
  color: var(--rep-text-muted);
  background: var(--rep-surface);
  border: 1px solid var(--rep-border);
  border-radius: 6px;
  cursor: pointer;
}
.theme-toggle:hover {
  background: var(--rep-surface-hover);
  color: var(--rep-text);
}
.theme-toggle:focus-visible {
  outline: 2px solid var(--rep-focus);
  outline-offset: 1px;
}
@media print {
  .theme-toggle {
    display: none;
  }
}
</style>
