#!/usr/bin/env node
// Перегенерация docs/template.schema.json из канонического JS-модуля.
import { writeFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { dirname, resolve } from 'node:path'
import schema from '../src/template/schema.js'

const here = dirname(fileURLToPath(import.meta.url))
const out = resolve(here, '../../docs/template.schema.json')
writeFileSync(out, JSON.stringify(schema, null, 2) + '\n')
console.log('записан', out)
