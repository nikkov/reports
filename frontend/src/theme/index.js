// Тема приложения (ADR-156): светлая и тёмная.
// Источник истины — атрибут data-theme на <html>; палитра живёт в tokens.css.
// Выбор запоминается в localStorage; если выбора не было — берём системный prefers-color-scheme
// и следим за его изменением, пока пользователь не выберет тему вручную.

export const THEME_KEY = 'rep.theme'
export const THEMES = ['light', 'dark']

export function systemTheme() {
  if (typeof window === 'undefined' || !window.matchMedia) return 'light'
  return window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light'
}

export function storedTheme() {
  try {
    const value = window.localStorage.getItem(THEME_KEY)
    return THEMES.includes(value) ? value : null
  } catch {
    return null
  }
}

export function currentTheme() {
  const value = document.documentElement.dataset.theme
  return THEMES.includes(value) ? value : 'light'
}

export function applyTheme(theme) {
  const next = THEMES.includes(theme) ? theme : 'light'
  document.documentElement.dataset.theme = next
  // событие для графиков: им нужно перечитать цвета из CSS (ADR-156)
  window.dispatchEvent(new CustomEvent('rep:theme', { detail: { theme: next } }))
  return next
}

export function setTheme(theme) {
  const next = applyTheme(theme)
  try {
    window.localStorage.setItem(THEME_KEY, next)
  } catch {
    /* приватный режим: тема просто не запомнится */
  }
  return next
}

export function toggleTheme() {
  return setTheme(currentTheme() === 'dark' ? 'light' : 'dark')
}

export function initTheme() {
  const saved = storedTheme()
  applyTheme(saved || systemTheme())

  // флаг на window, а не в модуле: повторный initTheme не подписывается дважды,
  // и подписка живёт ровно столько, сколько живёт само окно
  if (!window.__repThemeWatching && typeof window !== 'undefined' && window.matchMedia) {
    window.__repThemeWatching = true
    const media = window.matchMedia('(prefers-color-scheme: dark)')
    const onChange = (event) => {
      // ручной выбор главнее системы
      if (!storedTheme()) applyTheme(event.matches ? 'dark' : 'light')
    }
    if (media.addEventListener) media.addEventListener('change', onChange)
    else if (media.addListener) media.addListener(onChange)
  }
  return currentTheme()
}

// Цвета для canvas/графиков: ECharts не умеет var(--rep-*), значения читаем из CSS.
export function themeColors() {
  if (typeof window === 'undefined') return {}
  const css = getComputedStyle(document.documentElement)
  const pick = (name) => css.getPropertyValue(name).trim()
  return {
    text: pick('--rep-text'),
    muted: pick('--rep-text-muted'),
    border: pick('--rep-border'),
    surface: pick('--rep-surface'),
    accent: pick('--rep-accent'),
    danger: pick('--rep-danger'),
    ok: pick('--rep-ok'),
    warn: pick('--rep-warn')
  }
}

// ECharts не понимает CSS-переменные: `var(--rep-table-text)` он рисует чёрным.
// Умолчания стилей таблицы (styles.js) как раз такие, поэтому перед передачей в ECharts
// значение нужно превратить в реальный цвет текущей темы (ADR-156, ADR-184).
export function cssColorValue(value) {
  if (typeof value !== 'string') return value
  const match = /^var\(\s*(--[^,)\s]+)\s*(?:,\s*([^)]+))?\)\s*$/.exec(value.trim())
  if (!match) return value
  if (typeof window === 'undefined') return match[2] ? match[2].trim() : ''
  const resolved = getComputedStyle(document.documentElement).getPropertyValue(match[1]).trim()
  return resolved || (match[2] ? match[2].trim() : '')
}
