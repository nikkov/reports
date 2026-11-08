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
import { buildBandModel, buildGaugeModel, buildSankeyModel } from '../engine/charts.js'

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
  const type = props.block.chartType
  const fmt = (v) => (typeof v === 'number' ? nf.format(Math.round(v * 100) / 100) : v)

  // gauge: одно число, шкала и подпись (ADR-56)
  if (type === 'gauge') {
    const model = buildGaugeModel(props.rows, {
      yField: props.block.yField,
      aggregate: props.block.aggregate || 'sum',
      min: props.block.min,
      max: props.block.max,
      unit: props.block.unit || '',
      name: props.block.title || props.block.yField,
      // цветовые зоны (ADR-57): границы и цвета из шаблона, токены — из styles.palette
      thresholds: props.block.thresholds,
      zoneColors: props.block.zoneColors,
      palette: props.styles?.palette
    })
    const accent = props.styles?.palette?.accent || props.styles?.colors?.accent || '#17427f'
    const span = model.max - model.min || 1
    // ось красится полосами: доля правой границы зоны на шкале
    const axisColors = model.zones.length
      ? model.zones.map((z) => [(z.to - model.min) / span, z.color])
      : [[1, '#e6ebf2']]
    const valueColor = model.color || accent
    return {
      animation: false,
      textStyle: textStyle(),
      series: [
        {
          type: 'gauge',
          min: model.min,
          max: model.max,
          startAngle: 210,
          endAngle: -30,
          // при цветовых зонах полоса прогресса закрыла бы зоны — оставляем только полосы
          progress: { show: !model.zones.length, width: 12, itemStyle: { color: accent } },
          axisLine: { lineStyle: { width: 12, color: axisColors } },
          axisTick: { show: false },
          splitLine: { length: 10, lineStyle: { color: '#c3ccd8' } },
          axisLabel: { color: textStyle()?.color, fontSize: 11, distance: 14 },
          pointer: { width: 4, itemStyle: { color: valueColor } },
          title: { show: !!model.name, offsetCenter: [0, '72%'], fontSize: 12, color: textStyle()?.color },
          detail: {
            valueAnimation: false,
            offsetCenter: [0, '42%'],
            fontSize: 26,
            color: valueColor,
            formatter: (v) => `${nf.format(Math.round(v * 100) / 100)}${model.unit}`
          },
          data: [{ value: model.value, name: model.name }]
        }
      ]
    }
  }

  // band: значение плюс верхняя и нижняя границы, диапазон закрашен (ADR-58)
  if (type === 'band') {
    const model = buildBandModel(props.rows, {
      xField: props.block.xField,
      yField: props.block.yField,
      lowField: props.block.lowField,
      highField: props.block.highField
    })
    const raw = props.block.bandColor || '#2e7d32'
    const bandColor = props.styles?.palette?.[raw] || raw
    return {
      animation: false,
      textStyle: textStyle(),
      grid: { left: 70, right: 24, top: 32, bottom: 56, containLabel: false },
      legend: { data: ['Значение'], top: 0, right: 8, textStyle: textStyle() },
      tooltip: {
        trigger: 'axis',
        formatter: (items) => {
          const i = items?.[0]?.dataIndex ?? 0
          const fmt1 = (v) => (v === null || v === undefined ? '—' : fmt(v))
          return [
            `<b>${model.categories[i]}</b>`,
            `значение: ${fmt1(model.values[i])}`,
            `нижняя: ${fmt1(model.lows[i])}`,
            `верхняя: ${fmt1(model.highs[i])}`
          ].join('<br/>')
        }
      },
      xAxis: {
        type: 'category',
        data: model.categories,
        axisLabel: {
          rotate: model.categories.length > 20 ? 45 : model.categories.length > 8 ? 30 : 0,
          // много точек (месяц дней) — подписи через одну-две, иначе они наезжают друг на друга
          interval: Math.max(0, Math.ceil(model.categories.length / 12) - 1),
          color: textStyle()?.color
        }
      },
      yAxis: { type: 'value', axisLabel: { formatter: fmt, color: textStyle()?.color } },
      series: [
        // невидимая нижняя граница — основание полосы
        {
          name: '__band_low',
          type: 'line',
          stack: 'band',
          data: model.lows,
          symbol: 'none',
          lineStyle: { opacity: 0 },
          areaStyle: { opacity: 0 },
          silent: true,
          tooltip: { show: false },
          legendHoverLink: false
        },
        // разница «верх − низ», закрашенная цветом полосы
        {
          name: '__band_fill',
          type: 'line',
          stack: 'band',
          data: model.band,
          symbol: 'none',
          lineStyle: { opacity: 0 },
          areaStyle: { color: bandColor, opacity: 0.22 },
          silent: true,
          tooltip: { show: false },
          legendHoverLink: false
        },
        {
          name: 'Значение',
          type: 'line',
          data: model.values,
          symbol: 'circle',
          symbolSize: 5,
          connectNulls: false,
          lineStyle: { width: 2 }
        }
      ]
    }
  }

  // sankey: потоки между узлами (ADR-56)
  if (type === 'sankey') {
    const model = buildSankeyModel(props.rows, {
      sourceField: props.block.sourceField,
      targetField: props.block.targetField,
      valueField: props.block.valueField,
      aggregate: props.block.aggregate || 'sum'
    })
    return {
      animation: false,
      textStyle: textStyle(),
      tooltip: { trigger: 'item', valueFormatter: fmt },
      series: [
        {
          type: 'sankey',
          data: model.nodes,
          links: model.links,
          emphasis: { focus: 'adjacency' },
          nodeGap: 12,
          nodeWidth: 14,
          lineStyle: { color: 'gradient', curveness: 0.5, opacity: 0.42 },
          label: { color: textStyle()?.color, fontSize: 12 }
        }
      ]
    }
  }

  const { categories, series } = aggregateSeries(props.rows, {
    xField: props.block.xField,
    seriesField: props.block.seriesField,
    yField: props.block.yField,
    aggregate: props.block.aggregate || 'sum'
  })

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
      // ECharts не знает типа «area»: это линия с заливкой до оси
      type: props.block.chartType === 'area' ? 'line' : props.block.chartType,
      areaStyle: props.block.chartType === 'area' ? { opacity: 0.25 } : undefined,
      data: s.data,
      smooth: props.block.smooth === true,
      barMaxWidth: 42,
      symbolSize: props.block.chartType === 'scatter' ? 9 : undefined,
      emphasis: { focus: 'series' }
    }))
  }
}

// ECharts не отдаёт «какой это ряд и категория» в событии: имя серии есть у bar/line,
// у круговой — только подпись сектора.
function onPointClick(event) {
  if (!props.drill?.report) return
  // sankey: в событии приходит сам поток {source, target, value}
  if (props.block.chartType === 'sankey' && event.data && event.data.source) {
    emit('drill', { spec: props.drill, x: event.data.source ?? null, series: event.data.target ?? null })
    return
  }
  // gauge: одна точка без категории — в параметры ничего не подставляется
  if (props.block.chartType === 'gauge') {
    emit('drill', { spec: props.drill, x: null, series: null })
    return
  }
  emit('drill', {
    spec: props.drill,
    x: event.name ?? null,
    series: event.seriesName ?? null
  })
}

// Реестр инстансов для e2e: window.__chart — последняя диаграмма (обратная совместимость),
// window.__charts — все отрисованные на странице (gauge, sankey и остальные).
function registerChart() {
  window.__chart = { type: props.block.chartType, series: chart.getOption().series?.length ?? 0, instance: chart }
  window.__charts = (window.__charts || []).filter((c) => c.instance !== chart)
  window.__charts.push({ type: props.block.chartType, block: props.block, instance: chart })
}

function render() {
  if (!chart) return
  chart.setOption(buildOption(), true)
  registerChart()
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
  window.__charts = (window.__charts || []).filter((c) => c.instance !== chart)
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
