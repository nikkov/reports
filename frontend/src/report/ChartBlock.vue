<template>
  <div class="chart-box">
    <h2 v-if="block.title">{{ block.title }}</h2>
    <div ref="el" class="chart" :class="{ drillable: !!drill }" :style="{ height: height + 'px' }"></div>
  </div>
</template>

<script setup>
// Блок диаграммы (Ф3): данные берутся из того же движка агрегации, что и таблица.
import { nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import * as echarts from 'echarts'
import RU from 'echarts/i18n/langRU'
import { aggregateSeries } from '../engine/group.js'

echarts.registerLocale('RU', RU)

const props = defineProps({
  block: { type: Object, required: true },
  rows: { type: Array, required: true },
  height: { type: Number, default: 320 },
  // drill-through (ADR-43): клик по точке ведёт в другой отчёт с параметрами из категории/серии
  drill: { type: Object, default: null },
  // стили шаблона (ADR-23): из них берём цвет и шрифт подписей, осей и легенды
  styles: { type: Object, default: null }
})

const textStyle = () => {
  const font = props.styles?.font
  const colors = props.styles?.colors
  if (!font || !colors) return undefined
  return { color: colors.text, fontFamily: font.family, fontSize: font.size }
}

const emit = defineEmits(['drill'])
const el = ref(null)
let chart = null
const nf = new Intl.NumberFormat('ru-RU')

function buildOption() {
  const { categories, series } = aggregateSeries(props.rows, {
    xField: props.block.xField,
    seriesField: props.block.seriesField,
    yField: props.block.yField,
    aggregate: props.block.aggregate || 'sum'
  })

  const fmt = (v) => (typeof v === 'number' ? nf.format(Math.round(v * 100) / 100) : v)

  if (props.block.chartType === 'pie') {
    const first = series[0] || { data: [] }
    return {
      animation: false,
      tooltip: { trigger: 'item', valueFormatter: fmt },
      legend: { type: 'scroll', bottom: 0, textStyle: textStyle() },
      textStyle: textStyle(),
      series: [
        {
          type: 'pie',
          radius: ['35%', '65%'],
          data: categories.map((name, i) => ({ name, value: first.data[i] })),
          label: { formatter: '{b}: {d}%' }
        }
      ]
    }
  }

  return {
    animation: false,
    grid: { left: 90, right: 24, top: 24, bottom: 48, containLabel: false },
    tooltip: { trigger: 'axis', valueFormatter: fmt },
    legend: series.length > 1 ? { type: 'scroll', bottom: 0, textStyle: textStyle() } : undefined,
    xAxis: {
      type: 'category',
      data: categories,
      axisLabel: { rotate: categories.length > 8 ? 30 : 0, color: textStyle()?.color },
      nameTextStyle: textStyle()
    },
    yAxis: { type: 'value', axisLabel: { formatter: fmt, color: textStyle()?.color } },
    textStyle: textStyle(),
    series: series.map((s) => ({
      name: s.name,
      type: props.block.chartType,
      data: s.data,
      smooth: props.block.smooth === true,
      barMaxWidth: 42,
      emphasis: { focus: 'series' }
    }))
  }
}

// ECharts не отдаёт «какой это ряд и категория» в событии: имя серии есть у bar/line,
// у круговой — только подпись сектора.
function onPointClick(event) {
  if (!props.drill?.report) return
  emit('drill', {
    spec: props.drill,
    x: event.name ?? null,
    series: event.seriesName ?? null
  })
}

function render() {
  if (!chart) return
  chart.setOption(buildOption(), true)
  window.__chart = {
    type: props.block.chartType,
    series: chart.getOption().series?.length ?? 0,
    instance: chart
  }
}

onMounted(async () => {
  chart = echarts.init(el.value, null, { locale: 'RU' })
  chart.on('click', onPointClick)
  render()
  await nextTick()
  chart.resize()
})

watch(() => [props.rows, props.block], render, { deep: false })

onBeforeUnmount(() => {
  chart?.dispose()
  chart = null
})
</script>

<style scoped>
.chart-box {
  background: #fff;
  border: 1px solid #dfe3e8;
  border-radius: 8px;
  padding: 10px 14px 4px;
}
.chart-box h2 {
  font-size: 13px;
  margin: 0 0 6px;
  font-weight: 600;
}
.chart.drillable {
  cursor: pointer;
}
</style>
