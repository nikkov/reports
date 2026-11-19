<template>
  <div class="chart-box">
    <h2 v-if="block.title">{{ block.title }}</h2>
    <div ref="el" class="chart" :class="{ drillable: !!drill }" :style="{ height: height + 'px' }"></div>
    <!-- несколько коридоров: цвет на графике нужно расшифровать (ADR-69) -->
    <div v-if="bandLegend.length" class="chart-band-legend">
      <span v-for="item in bandLegend" :key="item.name" class="band-key">
        <i class="band-swatch" :style="{ background: item.color }"></i>{{ item.name }}
      </span>
    </div>
  </div>
</template>

<script setup>
// Блок диаграммы (Ф3): данные берутся из того же движка агрегации, что и таблица.
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import { themeColors } from '../theme/index.js'
import { hiddenLabels } from './legendFilter.js'
import * as echarts from 'echarts'
import RU from 'echarts/i18n/langRU'
import { aggregateSeries } from '../engine/group.js'
import { buildBandModel, buildGaugeModel, buildSankeyModel } from '../engine/charts.js'
import { drillAllowed } from '../engine/drill.js'

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

// Коридоры полосы (ADR-69): либо bands[], либо прежняя пара lowField/highField.
// Цвет и подпись разрешаются здесь один раз — их читают и график, и подпись под ним.
// ECharts не понимает var(--rep-*): цвета читаем из CSS при построении опций (ADR-156)
const cssColors = ref(themeColors())
// серии, скрытые кликом по легенде (ADR-161): имя серии = подпись маркера
const hiddenNames = computed(() => hiddenLabels(props.block.dataKey))
const BAND_DEFAULT_COLORS = computed(() => [
  cssColors.value.ok,
  cssColors.value.accent,
  cssColors.value.warn,
  cssColors.value.muted
])
const bandSpecs = computed(() => {
  if (props.block?.chartType !== 'band') return []
  const list = Array.isArray(props.block.bands) && props.block.bands.length
    ? props.block.bands
    : [{ lowField: props.block.lowField, highField: props.block.highField, color: props.block.bandColor }]
  const palette = props.styles?.palette || {}
  return list.map((spec, i) => {
    const raw = spec?.color || props.block.bandColor || BAND_DEFAULT_COLORS.value[i % BAND_DEFAULT_COLORS.value.length]
    return {
      lowField: spec?.lowField,
      highField: spec?.highField,
      color: palette[raw] || raw,
      name: spec?.name || `коридор ${i + 1}`
    }
  })
})
// Подпись под графиком нужна, только когда коридоров больше одного: один цвет ясен и без неё
const bandLegend = computed(() => (bandSpecs.value.length > 1 ? bandSpecs.value : []))

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
    const accent = props.styles?.palette?.accent || props.styles?.colors?.accent || cssColors.value.accent
    const span = model.max - model.min || 1
    // ось красится полосами: доля правой границы зоны на шкале
    const axisColors = model.zones.length
      ? model.zones.map((z) => [(z.to - model.min) / span, z.color])
      : [[1, cssColors.value.accent]]
    const valueColor = model.color || accent
    // Полукруг (ADR-160): 180° вместо 210…-30, подписи границ под шкалой.
    const semi = props.block.semi === true
    const showLimits = props.block.showLimits === true
    return {
      animation: false,
      textStyle: textStyle(),
      series: [
        {
          type: 'gauge',
          min: model.min,
          max: model.max,
          startAngle: semi ? 180 : 210,
          endAngle: semi ? 0 : -30,
          center: semi ? ['50%', '72%'] : ['50%', '55%'],
          // при цветовых зонах полоса прогресса закрыла бы зоны — оставляем только полосы
          progress: { show: !model.zones.length, width: 12, itemStyle: { color: accent } },
          axisLine: { lineStyle: { width: 12, color: axisColors } },
          axisTick: { show: false },
          splitLine: { length: 10, lineStyle: { color: cssColors.value.border } },
          axisLabel: {
            show: !showLimits,
            color: textStyle()?.color,
            fontSize: 11,
            distance: 14
          },
          pointer: { width: 4, itemStyle: { color: valueColor } },
          title: { show: !!model.name, offsetCenter: [0, '72%'], fontSize: 12, color: textStyle()?.color },
          detail: {
            valueAnimation: false,
            offsetCenter: [0, '42%'],
            fontSize: 26,
            color: valueColor,
            formatter: (v) => `${nf.format(Math.round(v * 100) / 100)}${model.unit}`
          },
          data: [{ value: model.value, name: model.name }],
          // подписи границ: нижняя и верхняя точка шкалы под полукругом
          markPoint: showLimits
            ? undefined
            : undefined
        }
      ],
      graphic: showLimits
        ? [
            { type: 'text', left: '8%', bottom: 4, style: { text: fmt(model.min), fontSize: 11, fill: 'var(--rep-text-muted)' } },
            { type: 'text', right: '8%', bottom: 4, style: { text: fmt(model.max), fontSize: 11, fill: 'var(--rep-text-muted)' } }
          ]
        : undefined
    }
  }

  // band: значение плюс верхняя и нижняя границы, диапазон закрашен (ADR-58).
  // Коридоров может быть несколько (ADR-69): bands: [{lowField, highField, color?, name?}].
  if (type === 'band') {
    const model = buildBandModel(props.rows, {
      xField: props.block.xField,
      yField: props.block.yField,
      bands: bandSpecs.value
    })
    const corridors = bandSpecs.value.map((spec, i) => ({ ...model.corridors[i], ...spec }))
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
          const lines = [`<b>${model.categories[i]}</b>`, `значение: ${fmt1(model.values[i])}`]
          for (const c of corridors) {
            lines.push(`${c.name}: ${fmt1(c.lows[i])} … ${fmt1(c.highs[i])}`)
          }
          return lines.join('<br/>')
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
        // на каждый коридор — невидимое основание и закрашенная разница; стек свой,
        // иначе второй коридор начнётся от верха первого
        ...corridors.flatMap((c, i) => [
          {
            name: `__band_low_${i}`,
            type: 'line',
            stack: `band-${i}`,
            data: c.lows,
            symbol: 'none',
            lineStyle: { opacity: 0 },
            areaStyle: { opacity: 0 },
            silent: true,
            tooltip: { show: false },
            legendHoverLink: false
          },
          {
            name: `__band_fill_${i}`,
            type: 'line',
            stack: `band-${i}`,
            data: c.band,
            symbol: 'none',
            lineStyle: { opacity: 0 },
            areaStyle: { color: c.color, opacity: 0.22 },
            silent: true,
            tooltip: { show: false },
            legendHoverLink: false
          }
        ]),
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
      levels: props.block.levels,
      aggregate: props.block.aggregate || 'sum'
    })
    // Подпись узла берём из модели: у цепочки id содержит префикс уровня, иначе
    // одинаковые названия на разных уровнях склеились бы в один узел (ADR-68).
    const labelOf = (name) => model.label(name)
    return {
      animation: false,
      textStyle: textStyle(),
      tooltip: {
        trigger: 'item',
        formatter: (p) => {
          if (p.dataType === 'edge') {
            return `${labelOf(p.data.source)} → ${labelOf(p.data.target)}<br/><b>${fmt(p.data.value)}</b>`
          }
          return `${labelOf(p.name)}<br/><b>${fmt(p.value)}</b>`
        }
      },
      series: [
        {
          type: 'sankey',
          data: model.nodes,
          links: model.links,
          // узлы прижимаются к краям, иначе при десятках узлов на последнем уровне
          // первый уровень сжимается в нечитаемое пятно
          nodeAlign: 'justify',
          emphasis: { focus: 'adjacency' },
          nodeGap: 10,
          nodeWidth: 14,
          lineStyle: { color: 'gradient', curveness: 0.5, opacity: 0.42 },
          label: { color: textStyle()?.color, fontSize: 12, formatter: (p) => labelOf(p.name) }
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
    const donut = props.block.donut === true
    const total = first.data.reduce((acc, v) => acc + (Number(v) || 0), 0)
    // Число в центре кольца (ADR-160): сумма секторов и доля от цели, если цель задана.
    const centerValue = props.block.centerField && props.rows?.length
      ? Number(props.rows[0][props.block.centerField])
      : total
    // цель может быть константой (target) или полем набора (targetField): у каждого региона свой план
    const targetFromField = props.block.goalField
      ? (props.rows || []).reduce((acc, r) => acc + (Number(r[props.block.goalField]) || 0), 0)
      : null
    const target = targetFromField !== null ? targetFromField : Number(props.block.target)
    const centerText = Number.isFinite(centerValue) ? fmt(centerValue) : ''
    const centerSub = Number.isFinite(target) && target > 0 && Number.isFinite(centerValue)
      ? `${(centerValue / target * 100).toFixed(1)}% от цели`
      : ''
    return {
      animation: false,
      tooltip: { trigger: 'item', valueFormatter: fmt },
      legend: { type: 'scroll', bottom: 0, textStyle: textStyle() },
      textStyle: textStyle(),
      // подпись в центре кольца: число и доля от цели рисуются graphic-слоем —
      // у title при пустом сегменте текст иногда не появляется
      graphic: donut
        ? [
            {
              type: 'text',
              left: 'center',
              top: '40%',
              style: {
                text: centerText,
                fontSize: 20,
                fontWeight: 700,
                fill: textStyle()?.color,
                textAlign: 'center'
              }
            },
            {
              type: 'text',
              left: 'center',
              top: '48%',
              style: {
                text: centerSub,
                fontSize: 12,
                fill: 'var(--rep-text-muted)',
                textAlign: 'center'
              }
            }
          ]
        : undefined,
      series: [
        {
          type: 'pie',
          radius: donut ? ['52%', '74%'] : ['35%', '65%'],
          data: categories
            .map((name, i) => ({ name, value: first.data[i] }))
            .filter((d) => !hiddenNames.value.includes(d.name)),
          // у кольца подписи секторов убираем: место в центре занято числом и долей от цели,
          // расшифровка цветов — в легенде под графиком (ADR-160)
          label: donut
            ? { show: false }
            : { formatter: '{b}: {d}%', color: textStyle()?.color },
          labelLine: donut ? { show: false } : { lineStyle: { color: textStyle()?.color } }
        }
      ]
    }
  }

  // Спарклайн (ADR-159): мини-график без осей, сетки и легенды — для карточки показателя.
  // Живёт отдельным chartType, чтобы в блоке диаграммы можно было показать тренд вне KPI.
  if (props.block.chartType === 'sparkline') {
    const first = series[0] || { data: [] }
    return {
      animation: false,
      grid: { left: 2, right: 2, top: 4, bottom: 4 },
      tooltip: { trigger: 'axis', valueFormatter: fmt },
      textStyle: textStyle(),
      xAxis: { type: 'category', data: categories, show: false, boundaryGap: false },
      yAxis: { type: 'value', show: false, min: 'dataMin', max: 'dataMax' },
      series: [
        {
          type: 'line',
          data: first.data,
          smooth: props.block.smooth === true,
          symbol: 'none',
          lineStyle: { width: 1.6 },
          areaStyle: props.block.area === true ? { opacity: 0.2 } : undefined,
          emphasis: { focus: 'series' }
        }
      ]
    }
  }

  // Горизонтальные полосы (ADR-159): категории по вертикали, значения по горизонтали —
  // так удобно сравнивать план/факт по нескольким объектам (bullet chart).
  const horizontal = props.block.chartType === 'bar' && props.block.orientation === 'horizontal'
  // Опорная линия: план, лучший результат, норматив — вертикальная (или горизонтальная) черта.
  const reference = props.block.reference
  const refValue = typeof reference === 'number' ? reference : reference?.value
  const refLabel = (typeof reference === 'object' && reference?.label) || ''
  const refColor = (typeof reference === 'object' && reference?.color) || 'var(--rep-danger)'
  // Свой план у каждой полосы (ADR-161): опорные точки берутся из поля набора по категориям.
  const refField = props.block.referenceField
  const refPoints = refField
    ? categories
        .map((name, i) => {
          const src = (props.rows || []).find((r) => String(r[props.block.xField]) === String(name))
          const value = src ? Number(src[refField]) : NaN
          return Number.isFinite(value) ? { name, value } : null
        })
        .filter(Boolean)
    : []

  const axisCategory = {
    type: 'category',
    data: categories,
    axisLabel: { rotate: horizontal ? 0 : categories.length > 8 ? 30 : 0, color: textStyle()?.color },
    nameTextStyle: textStyle()
  }
  const axisValue = { type: 'value', axisLabel: { formatter: fmt, color: textStyle()?.color } }

  return {
    animation: false,
    grid: horizontal
      ? { left: 110, right: 32, top: 16, bottom: 24, containLabel: false }
      : { left: 90, right: 24, top: 24, bottom: 48, containLabel: false },
    tooltip: { trigger: 'axis', valueFormatter: fmt },
    legend: series.length > 1 ? { type: 'scroll', bottom: 0, textStyle: textStyle() } : undefined,
    xAxis: horizontal ? axisValue : axisCategory,
    yAxis: horizontal ? axisCategory : axisValue,
    textStyle: textStyle(),
    series: series.filter((s) => !hiddenNames.value.includes(s.name)).map((s) => ({
      name: s.name,
      // ECharts не знает типа «area»: это линия с заливкой до оси
      type: props.block.chartType === 'area' ? 'line' : props.block.chartType,
      areaStyle: props.block.chartType === 'area' ? { opacity: 0.25 } : undefined,
      // скрытая легендой категория превращается в пропуск: так можно гасить и полосы,
      // когда серия одна, а маркеры легенды соответствуют категориям (ADR-161)
      data: s.data.map((v, i) =>
        hiddenNames.value.includes(String(categories[i])) ? null : v
      ),
      smooth: props.block.smooth === true,
      barMaxWidth: horizontal ? 22 : 42,
      symbolSize: props.block.chartType === 'scatter' ? 9 : undefined,
      emphasis: { focus: 'series' },
      // подписи значений у полос: без них «bullet» нечитаем
      label:
        horizontal && props.block.showValues === true
          ? { show: true, position: 'right', formatter: (p) => fmt(p.value), color: textStyle()?.color }
          : undefined,
      markLine: refPoints.length
        ? {
            // у каждой категории своя цель: для горизонтальных полос — отметка по оси значений
            symbol: 'none',
            silent: true,
            lineStyle: { color: refColor, type: 'dashed', width: 1.5 },
            label: { formatter: (p) => fmt(p.value), color: refColor, position: 'end' },
            data: refPoints
              .filter((p) => !hiddenNames.value.includes(String(p.name)))
              .map((p) => (horizontal ? { yAxis: p.name, xAxis: p.value } : { xAxis: p.name, yAxis: p.value }))
          }
        : Number.isFinite(Number(refValue)) && refValue !== null && refValue !== undefined
          ? {
              symbol: 'none',
              silent: true,
              lineStyle: { color: refColor, type: 'dashed', width: 1.5 },
              label: { formatter: refLabel, color: refColor, position: 'end' },
              data: [horizontal ? { xAxis: Number(refValue) } : { yAxis: Number(refValue) }]
            }
          : undefined
    }))
  }
}

// ECharts не отдаёт «какой это ряд и категория» в событии: имя серии есть у bar/line,
// у круговой — только подпись сектора.
function onPointClick(event) {
  if (!props.drill?.report) return
  // sankey: в событии приходит поток {source, target, value} или узел. У цепочки уровней
  // (ADR-68) id узла содержит префикс уровня — в параметры уходит исходное значение,
  // а рядом — карта значений уровней для подстановок level.<поле> (ADR-72).
  if (props.block.chartType === 'sankey' && event.data) {
    const data = event.data
    const clean = (id) => (typeof id === 'string' ? id.replace(/^\d+\u0001/, '') : id ?? null)
    const levels = data.levels || {}
    // переход возможен, только если в кликнутом элементе есть все нужные уровни:
    // иначе фильтр получился бы шире, чем клик
    if (!drillAllowed(props.drill, levels)) return
    if (data.source) {
      emit('drill', { spec: props.drill, x: clean(data.source), series: clean(data.target), levels })
      return
    }
    emit('drill', { spec: props.drill, x: clean(data.name), series: null, levels, node: clean(data.name) })
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

// тема сменилась: ECharts держит цвета внутри опций — перечитываем и перерисовываем
function onThemeChange() {
  cssColors.value = themeColors()
  render()
}

onMounted(async () => {
  chart = echarts.init(el.value, null, { locale: 'RU' })
  chart.on('click', onPointClick)
  window.addEventListener('rep:theme', onThemeChange)
  render()
  await nextTick()
  chart.resize()
})

watch(() => [props.rows, props.block], render, { deep: false })
// клик по легенде меняет набор видимых серий — перерисовываем
watch(hiddenNames, render)

onBeforeUnmount(() => {
  window.removeEventListener('rep:theme', onThemeChange)
  window.__charts = (window.__charts || []).filter((c) => c.instance !== chart)
  chart?.dispose()
  chart = null
})
</script>

<style scoped>
.chart-box {
  background: var(--rep-surface);
  border: 1px solid var(--rep-border);
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
.chart-band-legend {
  display: flex;
  flex-wrap: wrap;
  gap: 16px;
  margin: 6px 0 0 8px;
  font-size: 12px;
  color: var(--rep-muted, var(--rep-text-muted));
}

.band-key {
  display: inline-flex;
  align-items: center;
  gap: 6px;
}

.band-swatch {
  width: 12px;
  height: 12px;
  border-radius: 3px;
  opacity: 0.55;
}
</style>
