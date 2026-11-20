<template>
  <!-- Рисунок (ADR-169): логотип или схема. Файл лежит у сервера, внешних ссылок нет. -->
  <figure class="img-block" :class="`align-${align}`">
    <a
      v-if="block.link"
      :href="block.link"
      target="_blank"
      rel="noopener noreferrer"
      class="img-link"
    >
      <img :src="url" :alt="block.alt" :style="imgStyle" loading="lazy" decoding="async" />
    </a>
    <img v-else :src="url" :alt="block.alt" :style="imgStyle" loading="lazy" decoding="async" />
    <figcaption v-if="block.title">{{ block.title }}</figcaption>
  </figure>
</template>

<script setup>
import { computed } from 'vue'
import { API_BASE } from '../api/client.js'

const props = defineProps({
  block: { type: Object, required: true }
})

// базовый путь приложения (ADR-81): /api/assets/... превращается в /reports/api/assets/...
const url = computed(() => `${API_BASE}${props.block.src || ''}`)
const align = computed(() => props.block.align || 'left')

const imgStyle = computed(() => ({
  height: `${props.block.height || 64}px`,
  objectFit: props.block.fit || 'contain'
}))
</script>

<style scoped>
.img-block {
  margin: 0;
  display: flex;
  flex-direction: column;
  gap: 6px;
}
.img-block.align-center {
  align-items: center;
  text-align: center;
}
.img-block.align-right {
  align-items: flex-end;
  text-align: right;
}
.img-link {
  display: inline-flex;
}
img {
  max-width: 100%;
  width: auto;
  display: block;
}
figcaption {
  font-size: 12px;
  color: var(--rep-text-muted);
}
@media print {
  img {
    break-inside: avoid;
  }
}
</style>
