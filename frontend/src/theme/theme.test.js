import { beforeEach, describe, expect, it, vi } from 'vitest'
import { THEME_KEY, applyTheme, currentTheme, initTheme, setTheme, storedTheme, systemTheme, toggleTheme } from './index.js'

// Тема приложения (ADR-156): источник истины — data-theme на <html>, выбор — в localStorage.
// vitest здесь без jsdom, поэтому минимальные заглушки window/document — так тест не тянет зависимость.

function fakeStorage() {
  const data = new Map()
  return {
    getItem: (k) => (data.has(k) ? data.get(k) : null),
    setItem: (k, v) => data.set(k, String(v)),
    removeItem: (k) => data.delete(k),
    clear: () => data.clear()
  }
}

function setupDom({ dark = false } = {}) {
  const listeners = {}
  const html = { dataset: {} }
  const events = []
  globalThis.document = { documentElement: html }
  globalThis.window = {
    localStorage: fakeStorage(),
    matchMedia: (query) => ({
      matches: query.includes('dark') ? dark : !dark,
      media: query,
      addEventListener: (name, cb) => (listeners[name] = cb),
      removeEventListener: () => {},
      addListener: (cb) => (listeners.change = cb)
    }),
    addEventListener: (name, cb) => (listeners[name] = cb),
    removeEventListener: () => {},
    dispatchEvent: (event) => {
      events.push(event)
      listeners[event.type]?.(event)
      return true
    }
  }
  return { html, events, listeners }
}

describe('тема приложения', () => {
  beforeEach(() => setupDom())

  it('без сохранения берёт системную тему', () => {
    setupDom({ dark: true })
    expect(systemTheme()).toBe('dark')
    expect(storedTheme()).toBe(null)
    expect(initTheme()).toBe('dark')
    expect(document.documentElement.dataset.theme).toBe('dark')
  })

  it('сохранённый выбор важнее системной темы', () => {
    setupDom({ dark: true })
    window.localStorage.setItem(THEME_KEY, 'light')
    expect(initTheme()).toBe('light')
  })

  it('setTheme запоминает выбор и ставит атрибут', () => {
    expect(setTheme('dark')).toBe('dark')
    expect(document.documentElement.dataset.theme).toBe('dark')
    expect(window.localStorage.getItem(THEME_KEY)).toBe('dark')
    expect(currentTheme()).toBe('dark')
  })

  it('toggleTheme переключает в обе стороны', () => {
    applyTheme('light')
    expect(toggleTheme()).toBe('dark')
    expect(toggleTheme()).toBe('light')
  })

  it('смена темы публикует событие rep:theme', () => {
    const { events } = setupDom()
    setTheme('dark')
    setTheme('light')
    expect(events.map((e) => e.detail.theme)).toEqual(['dark', 'light'])
    expect(events.map((e) => e.type)).toEqual(['rep:theme', 'rep:theme'])
  })

  it('системная тема меняется, пока её не выбрали вручную', () => {
    const { listeners } = setupDom()
    initTheme()
    listeners.change({ matches: true })
    expect(document.documentElement.dataset.theme).toBe('dark')
    // после ручного выбора система больше не влияет
    setTheme('light')
    listeners.change({ matches: true })
    expect(document.documentElement.dataset.theme).toBe('light')
  })

  it('мусор в localStorage не ломает выбор', () => {
    window.localStorage.setItem(THEME_KEY, 'sepia')
    expect(storedTheme()).toBe(null)
    expect(initTheme()).toBe('light')
  })
})
