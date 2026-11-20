<template>
  <!-- Единая админка (ADR-170): разделы — вкладки в одной оболочке, а не отдельные страницы.
       Адрес держит вкладку в hash: /reports/admin.html#users. Старые страницы users.html,
       subscriptions.html и audit.html перенаправляют сюда, чтобы закладки не ломались. -->
  <div class="shell-root">
    <header class="shell-bar">
      <span class="shell-title">Администрирование</span>
      <nav class="shell-tabs" role="tablist" aria-label="Разделы администрирования">
        <a
          v-for="t in tabs"
          :key="t.key"
          role="tab"
          class="shell-tab"
          :class="{ active: t.key === active }"
          :aria-selected="t.key === active ? 'true' : 'false'"
          :href="`#${t.key}`"
          @click.prevent="go(t.key)"
        >{{ t.label }}</a>
      </nav>
      <ThemeToggle />
    </header>
    <div class="shell-body">
      <KeepAlive>
        <component :is="current" :session="session" />
      </KeepAlive>
    </div>
  </div>
</template>

<script setup>
import { computed, onBeforeUnmount, onMounted, ref, shallowRef } from 'vue'
import ThemeToggle from '../components/ThemeToggle.vue'
import AdminApp from './AdminApp.vue'
import UsersApp from './UsersApp.vue'
import SubscriptionsApp from './SubscriptionsApp.vue'
import AuditApp from './AuditApp.vue'

const props = defineProps({
  session: { type: Object, default: null }
})

const tabs = [
  { key: 'templates', label: 'Шаблоны' },
  { key: 'users', label: 'Пользователи' },
  { key: 'subscriptions', label: 'Рассылка' },
  { key: 'audit', label: 'Журнал' }
]
const keys = tabs.map((t) => t.key)
const active = ref('templates')

// Компоненты держим в shallowRef: Vue не должен делать их «реактивными»
const views = { templates: AdminApp, users: UsersApp, subscriptions: SubscriptionsApp, audit: AuditApp }
const current = shallowRef(AdminApp)

function fromHash() {
  const key = (location.hash || '').replace(/^#/, '')
  return keys.includes(key) ? key : 'templates'
}

function apply() {
  active.value = fromHash()
  current.value = views[active.value]
  document.title = `Администрирование · ${tabs.find((t) => t.key === active.value).label}`
}

function go(key) {
  if (!keys.includes(key)) return
  if (location.hash === `#${key}`) return
  // hash меняем, чтобы вкладка попадала в историю и в закладку
  location.hash = `#${key}`
}

onMounted(() => {
  apply()
  window.addEventListener('hashchange', apply)
})
onBeforeUnmount(() => window.removeEventListener('hashchange', apply))

// на случай, если session понадобится дочерним экранам
const session = computed(() => props.session)
</script>

<style scoped>
.shell-root {
  display: flex;
  flex-direction: column;
  height: 100vh;
  background: var(--rep-bg);
  color: var(--rep-text);
}
.shell-bar {
  display: flex;
  align-items: center;
  gap: 16px;
  padding: 8px 16px;
  border-bottom: 1px solid var(--rep-border);
  background: var(--rep-surface);
  flex: 0 0 auto;
}
.shell-title {
  font-size: 13px;
  font-weight: 600;
  color: var(--rep-text);
  white-space: nowrap;
}
.shell-tabs {
  display: flex;
  gap: 4px;
  flex: 1;
  min-width: 0;
  overflow-x: auto;
}
.shell-tab {
  padding: 5px 12px;
  font-size: 13px;
  color: var(--rep-text-muted);
  text-decoration: none;
  border: 1px solid transparent;
  border-radius: 6px;
  white-space: nowrap;
}
.shell-tab:hover {
  background: var(--rep-surface-hover);
  color: var(--rep-text);
}
.shell-tab.active {
  background: var(--rep-accent-soft, var(--rep-surface-hover));
  border-color: var(--rep-border);
  color: var(--rep-text);
  font-weight: 600;
}
.shell-tab:focus-visible {
  outline: 2px solid var(--rep-focus);
  outline-offset: 1px;
}
.shell-body {
  flex: 1;
  min-height: 0;
  display: flex;
  overflow: hidden;
}
.shell-body :deep(> *) {
  flex: 1;
  min-width: 0;
  min-height: 0;
}
@media print {
  .shell-bar {
    display: none;
  }
}
</style>
