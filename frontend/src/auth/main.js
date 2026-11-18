import '../theme/tokens.css'
import { initTheme as initAppTheme } from '../theme/index.js'
import { createApp } from 'vue'
import LoginPage from './LoginPage.vue'

initAppTheme()

createApp(LoginPage).mount('#app')
