<template>
  <!-- KPI-карточка (ADR-159): значение, сравнение с планом/лучшим, отклонение со стрелкой
       и мини-график. Числа берутся из первой строки набора: одна карточка — один показатель. -->
  <div class="kpi" :class="tone" :style="{ height: heightPx }">
    <div class="kpi-head">
      <span class="kpi-title">{{ block.title || valueField }}</span>
      <span v-if="unit" class="kpi-unit">{{ unit }}</span>
    </div>

    <div class="kpi-value">{{ valueText }}</div>

    <div class="kpi-row">
      <span v-if="compareText" class="kpi-compare">
        <span v-if="compareLabel">{{ compareLabel }}:</span> {{ compareText }}
      </span>
      <span v-if="deltaText" class="kpi-delta" :class="deltaTone">
        {{ arrow }} {{ deltaText }}
      </span>
    </div>

    <svg v-if="sparkPoints" class="kpi-spark" :viewBox="`0 0 ${sparkWidth} ${sparkHeight}`" preserveAspectRatio="none">
      <polyline :points="sparkPoints" fill="none" :stroke="sparkColor" stroke-width="1.5" />
      <circle
        v-if="sparkLast"
        :cx="sparkLast.x"
        :cy="sparkLast.y"
        r="2"
        :fill="sparkColor"
      />
    </svg>
  </div>
</template>

<script setup>
import { computed } from 'vue'

const props = defineProps({
  block: { type: Object, required: true },
  rows: { type: Array, default: () => [] },
  // ряд для мини-графика может жить в другом наборе (например, по дням), а значение — в одном наборе
  sparkRows: { type: Array, default: () => [] },
  height: { type: Number, default: 160 }
})

const valueField = computed(() => props.block.valueField || '')
const compareField = computed(() => props.block.compareField || '')
const deltaField = computed(() => props.block.deltaField || '')
const sparkField = computed(() => props.block.sparkField || valueField.value)
const unit = computed(() => props.block.unit || '')
const compareLabel = computed(() => props.block.compareLabel || 'план')
const decimals = computed(() => (Number.isInteger(props.block.decimals) ? props.block.decimals : 2))
const goodWhen = computed(() => (props.block.goodWhen === 'down' ? 'down' : 'up'))

const row = computed(() => (props.rows && props.rows.length ? props.rows[0] : null))
const num = (value) => {
  const n = Number(value)
  return Number.isFinite(n) ? n : null
}

const formatNumber = (value) => {
  const n = num(value)
  if (n === null) return '—'
  return n.toLocaleString('ru-RU', {
    minimumFractionDigits: decimals.value,
    maximumFractionDigits: decimals.value
  })
}

const valueText = computed(() => formatNumber(row.value?.[valueField.value]))
const compareText = computed(() => {
  if (!compareField.value || !row.value) return ''
  const n = num(row.value[compareField.value])
  return n === null ? '' : formatNumber(n)
})

// отклонение: готовое поле в процентах или расчёт от значения сравнения
const delta = computed(() => {
  if (!row.value) return null
  if (deltaField.value) {
    const n = num(row.value[deltaField.value])
    return n === null ? null : n
  }
  if (!compareField.value) return null
  const value = num(row.value[valueField.value])
  const base = num(row.value[compareField.value])
  if (value === null || base === null || base === 0) return null
  return ((value - base) / Math.abs(base)) * 100
})

const deltaText = computed(() => {
  if (delta.value === null) return ''
  return `${Math.abs(delta.value).toFixed(1)}%`
})

const arrow = computed(() => (delta.value === null ? '' : delta.value > 0 ? '▲' : delta.value < 0 ? '▼' : '='))

// цвет отклонения: рост хорош или плох — задаёт шаблон (goodWhen)
const deltaTone = computed(() => {
  if (delta.value === null || delta.value === 0) return 'neutral'
  const better = goodWhen.value === 'up' ? delta.value > 0 : delta.value < 0
  return better ? 'good' : 'bad'
})

// вся карточка подкрашивается по тому же правилу
const tone = computed(() => {
  if (delta.value === null) return ''
  return deltaTone.value === 'bad' ? 'is-bad' : deltaTone.value === 'good' ? 'is-good' : ''
})

const sparkColor = computed(() => {
  if (deltaTone.value === 'bad') return 'var(--rep-danger)'
  if (deltaTone.value === 'good') return 'var(--rep-ok)'
  return 'var(--rep-accent)'
})

const SPARK_W = 100
const SPARK_H = 24
const sparkWidth = SPARK_W
const sparkHeight = SPARK_H

const sparkValues = computed(() => {
  if (!sparkField.value) return []
  const source = props.sparkRows && props.sparkRows.length ? props.sparkRows : props.rows || []
  return source.map((r) => num(r[sparkField.value])).filter((v) => v !== null)
})

const sparkPoints = computed(() => {
  const values = sparkValues.value
  if (values.length < 2) return ''
  const min = Math.min(...values)
  const max = Math.max(...values)
  const span = max - min || 1
  return values
    .map((v, i) => {
      const x = (i / (values.length - 1)) * SPARK_W
      const y = SPARK_H - ((v - min) / span) * (SPARK_H - 2) - 1
      return `${x.toFixed(1)},${y.toFixed(1)}`
    })
    .join(' ')
})

const sparkLast = computed(() => {
  const values = sparkValues.value
  if (values.length < 2) return null
  const min = Math.min(...values)
  const max = Math.max(...values)
  const span = max - min || 1
  const v = values[values.length - 1]
  return { x: SPARK_W, y: SPARK_H - ((v - min) / span) * (SPARK_H - 2) - 1 }
})

const heightPx = computed(() => (Number.isFinite(props.height) ? `${props.height}px` : undefined))
</script>

<style scoped>
.kpi {
  display: flex;
  flex-direction: column;
  gap: 2px;
  box-sizing: border-box;
  min-height: 120px;
  padding: 10px 12px;
  background: var(--rep-surface);
  border: 1px solid var(--rep-border);
  border-left: 3px solid var(--rep-border-strong);
  border-radius: 8px;
  overflow: hidden;
}
.kpi.is-bad {
  border-left-color: var(--rep-danger);
}
.kpi.is-good {
  border-left-color: var(--rep-ok);
}
.kpi-head {
  display: flex;
  align-items: baseline;
  gap: 6px;
  font-size: 11px;
  letter-spacing: 0.04em;
  text-transform: uppercase;
  color: var(--rep-text-dim);
}
.kpi-title {
  font-weight: 600;
}
.kpi-unit {
  margin-left: auto;
  color: var(--rep-text-faint);
  text-transform: none;
}
.kpi-value {
  font-size: 26px;
  font-weight: 700;
  line-height: 1.15;
  color: var(--rep-text);
  font-variant-numeric: tabular-nums;
}
.kpi-row {
  display: flex;
  align-items: baseline;
  gap: 8px;
  font-size: 12px;
  color: var(--rep-text-muted);
}
.kpi-delta {
  font-weight: 700;
  font-variant-numeric: tabular-nums;
}
.kpi-delta.good {
  color: var(--rep-ok);
}
.kpi-delta.bad {
  color: var(--rep-danger);
}
.kpi-delta.neutral {
  color: var(--rep-text-dim);
}
.kpi-spark {
  margin-top: auto;
  width: 100%;
  height: 24px;
}
@media print {
  .kpi {
    background: #ffffff;
  }
}
</style>
