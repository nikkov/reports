<template>
  <!-- KPI (ADR-159, ADR-164): одна карточка или несколько в одном блоке.
       Числа берутся из первой строки набора; items[] задаёт показатели одной карточки-группы. -->
  <div class="kpi-wrap" :class="{ multi: cards.length > 1 }">
    <div v-for="(c, i) in cards" :key="i" class="kpi" :class="c.tone" :style="{ height: heightPx }">
      <div class="kpi-head">
        <span class="kpi-title">{{ c.title }}</span>
        <span v-if="c.unit" class="kpi-unit">{{ c.unit }}</span>
      </div>

      <div class="kpi-value">{{ c.valueText }}</div>

      <div class="kpi-row">
        <span v-if="c.compareText" class="kpi-compare">
          <span v-if="c.compareLabel">{{ c.compareLabel }}:</span> {{ c.compareText }}
        </span>
        <span v-if="c.deltaText" class="kpi-delta" :class="c.deltaTone">
          {{ c.arrow }} {{ c.deltaText }}
        </span>
      </div>

      <svg
        v-if="c.sparkPoints"
        class="kpi-spark"
        :viewBox="`0 0 ${SPARK_W} ${SPARK_H}`"
        preserveAspectRatio="none"
      >
        <polyline :points="c.sparkPoints" fill="none" :stroke="c.sparkColor" stroke-width="1.5" />
        <circle v-if="c.sparkLast" :cx="c.sparkLast.x" :cy="c.sparkLast.y" r="2" :fill="c.sparkColor" />
      </svg>
    </div>
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

const SPARK_W = 100
const SPARK_H = 24

const num = (value) => {
  const n = Number(value)
  return Number.isFinite(n) ? n : null
}

const formatNumber = (value, decimals) => {
  const n = num(value)
  if (n === null) return '—'
  return n.toLocaleString('ru-RU', {
    minimumFractionDigits: decimals,
    maximumFractionDigits: decimals
  })
}

// Спецификация одного показателя: блок сам или элемент items[] (ADR-164).
function buildCard(spec) {
  const row = props.rows && props.rows.length ? props.rows[0] : null
  const valueField = spec.valueField || ''
  const compareField = spec.compareField || ''
  const deltaField = spec.deltaField || ''
  const sparkField = spec.sparkField || valueField
  const unit = spec.unit || ''
  const compareLabel = spec.compareLabel || 'план'
  const decimals = Number.isInteger(spec.decimals) ? spec.decimals : 2
  const goodWhen = spec.goodWhen === 'down' ? 'down' : 'up'

  const valueText = formatNumber(row?.[valueField], decimals)
  const compareText = compareField && row ? formatNumber(row[compareField], decimals) : ''

  let delta = null
  if (row) {
    if (deltaField) {
      delta = num(row[deltaField])
    } else if (compareField) {
      const value = num(row[valueField])
      const base = num(row[compareField])
      if (value !== null && base !== null && base !== 0) delta = ((value - base) / Math.abs(base)) * 100
    }
  }

  const deltaTone = delta === null || delta === 0 ? 'neutral' : (goodWhen === 'up' ? delta > 0 : delta < 0) ? 'good' : 'bad'
  const tone = delta === null ? '' : deltaTone === 'bad' ? 'is-bad' : deltaTone === 'good' ? 'is-good' : ''
  const sparkColor = deltaTone === 'bad' ? 'var(--rep-danger)' : deltaTone === 'good' ? 'var(--rep-ok)' : 'var(--rep-accent)'

  const source = props.sparkRows && props.sparkRows.length ? props.sparkRows : props.rows || []
  const values = sparkField ? source.map((r) => num(r[sparkField])).filter((v) => v !== null) : []
  let sparkPoints = ''
  let sparkLast = null
  if (values.length >= 2) {
    const min = Math.min(...values)
    const max = Math.max(...values)
    const span = max - min || 1
    sparkPoints = values
      .map((v, i) => {
        const x = (i / (values.length - 1)) * SPARK_W
        const y = SPARK_H - ((v - min) / span) * (SPARK_H - 2) - 1
        return `${x.toFixed(1)},${y.toFixed(1)}`
      })
      .join(' ')
    const last = values[values.length - 1]
    sparkLast = { x: SPARK_W, y: SPARK_H - ((last - min) / span) * (SPARK_H - 2) - 1 }
  }

  return {
    title: spec.title || props.block.title || valueField,
    unit,
    valueText,
    compareText,
    compareLabel,
    deltaText: delta === null ? '' : `${Math.abs(delta).toFixed(1)}%`,
    arrow: delta === null ? '' : delta > 0 ? '▲' : delta < 0 ? '▼' : '=',
    deltaTone,
    tone,
    sparkPoints,
    sparkLast,
    sparkColor
  }
}

// items[] — несколько показателей в одном блоке; без него блок описывает одну карточку.
const cards = computed(() => {
  const items = Array.isArray(props.block.items) ? props.block.items : null
  if (items && items.length) return items.map((item) => buildCard(item))
  return [buildCard(props.block)]
})

const heightPx = computed(() => (Number.isFinite(props.height) ? `${props.height}px` : undefined))
</script>

<style scoped>
.kpi-wrap {
  display: grid;
  gap: 12px;
  grid-template-columns: minmax(0, 1fr);
}
/* несколько показателей — в ряд, на узком экране по одному */
.kpi-wrap.multi {
  grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
}
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
