# Справочник шаблона отчёта

Шаблон — JSON-объект, который хранится в таблице `report_templates` (ADR-21) и валидируется
схемой `frontend/src/template/schema.json` + семантическим валидатором `validate.js`.
Канон схемы — `frontend/src/template/schema.js`, файл `docs/template.schema.json` генерируется из него
(`npm run schema:json`).

## Общая структура

```json
{
  "version": 1,
  "id": "sales-by-region",
  "title": "Продажи по региону",
  "description": "Необязательное описание для экрана списка отчётов",
  "params": [],
  "datasets": {},
  "blocks": []
}
```

| Поле | Тип | Обяз. | Смысл |
|---|---|---|---|
| `version` | integer | да | версия формата шаблона, сейчас `1` |
| `id` | string | да | идентификатор отчёта; он же ключ в `report_templates` и в URL `?report=<id>` |
| `title` | string | да | заголовок страницы и карточки в списке |
| `description` | string | нет | пояснение на карточке списка отчётов |
| `params` | array | нет | параметры, которые пользователь задаёт перед построением |
| `datasets` | object | да | именованные наборы данных: `имя → { queryId, params }` |
| `blocks` | array | да | блоки вывода по порядку: текст, таблица, диаграмма |

## params[] — параметры отчёта

Значения в SQL всегда уходят через binding (`$1…$N`), в шаблон — по ссылке `$name` в `datasets`.
Пользователь видит `label`, а в запрос уходит `value`.

| Поле | Тип | Смысл |
|---|---|---|
| `name` | string | имя параметра, `^[a-z][a-z0-9_]*$`; на него ссылаются `datasets.*.params` и `{{name}}` в тексте |
| `type` | enum | `date`, `datetime`, `int`, `float`, `text`, `bool`, `select` |
| `label` | string | подпись в форме |
| `required` | boolean | обязателен ли параметр (иначе бэкенд вернёт 422) |
| `default` | string/number/bool | значение по умолчанию |
| `multiple` | boolean | множественный выбор; допустим только при `source.kind` = `lookup` или `static` |
| `options` | array | устаревший способ задать статический список; используйте `source.options` |
| `source` | object | откуда берётся значение (см. ниже) |

### source — способ получения значений

```json
{ "kind": "lookup", "queryId": "lookup_regions" }
{ "kind": "lookup", "queryId": "lookup_categories_by_region", "dependsOn": "region" }
{ "kind": "static", "options": [{ "value": "all", "label": "Все строки" }] }
{ "kind": "date" }
```

| `kind` | Что даёт | Требования |
|---|---|---|
| `date` | контрол `input[type=date]` | `type` = `date`, без `queryId`/`options` |
| `datetime` | контрол `input[type=datetime-local]` | `type` = `datetime` |
| `static` | список из `source.options` | непустой `options`, `type` = `select`, без `queryId` |
| `lookup` | список из `GET /api/lookups/{queryId}` | обязателен `queryId`, `type` = `select`, без `options` |

Дополнительно:

- `dependsOn: "<имя>"` — каскад: значение родителя передаётся в запрос справочника как его параметр.
  Родитель обязан быть объявлен **выше** (это же исключает циклы) и не может быть множественным.
- `multiple: true` — выпадающий список с чекбоксами (`MultiSelect.vue`), наружу значение уходит строкой
  через запятую; в SQL используйте `= ANY(string_to_array(:param, ','))`.
- Поведение по умолчанию: подставляется `default`; если его нет — первое значение списка
  (для `multiple` — все значения). Отчёт строится сразу после загрузки справочника.
- В текстовом блоке `{{name}}` печатается **подпись** выбранного значения (NAME), в SQL уходит ID.

## datasets — наборы данных

```json
"datasets": {
  "sales": {
    "queryId": "sales_by_region_id",
    "params": { "region": "$region", "from": "$from", "to": "$to" }
  }
}
```

| Поле | Смысл |
|---|---|
| `queryId` | ключ из `backend/config/queries.json` |
| `params` | отображение «параметр запроса → ссылка `$<имя параметра отчёта>`» |

Валидатор проверяет, что `queryId` зарегистрирован, а каждая ссылка указывает на объявленный параметр.

## blocks[] — блоки вывода

### Текст

```json
{ "type": "text", "text": "Регион {{region}}, период {{from}} — {{to}}", "width": "full" }
```

`{{name}}` — значение параметра (для справочников — подпись). Необъявленный параметр — ошибка валидации.
`width`: `full` (по умолчанию) или `half`.

### Таблица

```json
{
  "type": "table",
  "dataKey": "sales",
  "collapsible": true,
  "grandTotal": true,
  "nullText": "—",
  "columns": [
    { "field": "category", "title": "Категория", "group": true, "subtotal": true },
    { "field": "product", "title": "Продукт" },
    { "field": "amount", "title": "Сумма", "type": "int", "aggregate": "sum", "format": "number" },
    { "title": "Рентабельность", "expression": "sum(profit) / sum(amount)", "format": "percent" }
  ]
}
```

| Поле | Смысл |
|---|---|
| `dataKey` | имя набора из `datasets` |
| `collapsible` | разрешить сворачивание групп (шеврон в ячейке групповой колонки) |
| `grandTotal` | печатать строку общего итога |
| `nullText` | текст для пустых значений (по умолчанию `(пусто)`) |
| `width` | `full` / `half` |

### column — колонка таблицы

| Поле | Тип | Смысл |
|---|---|---|
| `title` | string | заголовок (обязателен) |
| `field` | string | поле из набора данных |
| `expression` | string | вычисляемое значение (см. `expressions.md`); с `field` несовместимо |
| `group` | boolean | колонка группировки; групповые колонки обязаны идти **первыми** |
| `subtotal` | boolean | печатать подытог по группе; только у `group`-колонки |
| `aggregate` | enum | `sum`, `avg`, `min`, `max`, `count`, `countDistinct`; требует `field` |
| `format` | enum | `number`, `percent`, `currency`, `date`, `integer` |
| `type` | enum | `int`, `float`, `text`, `date`, `bool` (подсказка для формата) |
| `decimals` | integer 0…6 | знаков после запятой (для `percent` по умолчанию 1) |
| `width` | integer ≥ 40 | ширина в px |
| `align` | enum | `left`, `right`, `center` |
| `children` | array | вложенные колонки для двухуровневого заголовка (без `field`/`expression`/`aggregate`/`group`) |

### Форматирование

- `number` / `integer` — `Intl.NumberFormat('ru-RU')`: `1 356 707 500`;
- `percent` — значение × 100, `decimals` (по умолчанию 1) знаков, запятая: `0,205 → 20,5 %`;
- `currency` и `date` пока не реализованы рендерером — выводятся как строка;
- колонка с `aggregate` форматируется как число, даже без `format`;
- пустое значение в агрегатной/вычисляемой колонке печатается как `—` (или `nullText`).

### Диаграмма

```json
{
  "type": "chart",
  "dataKey": "sales",
  "chartType": "bar",
  "title": "Сумма по категориям",
  "xField": "category",
  "yField": "amount",
  "aggregate": "sum",
  "width": "half"
}
```

| Поле | Смысл |
|---|---|
| `chartType` | `line`, `bar`, `pie` |
| `xField` / `yField` | категория и значение; проверяются на наличие в наборе данных |
| `seriesField` | необязательное разбиение на серии (см. ниже) |
| `aggregate` | агрегат по `yField` перед построением (`sum` по умолчанию) |
| `title` | заголовок блока |
| `width` | `full` / `half` |

## Что проверяет валидатор

Схема (типы, обязательность, `additionalProperties: false`) плюс семантические правила:

- `dataKey` объявлен в `datasets`; `xField`/`yField`/`seriesField` существуют в таблице набора;
- в колонке ровно одно из `field` / `expression`; `aggregate` требует `field`;
- групповые колонки идут до обычных; `subtotal` только у групповых; у группы с `children` нет `field`;
- есть хотя бы одна листовая колонка, если включён `grandTotal`;
- `{{name}}` в тексте и `$name` в `datasets` ссылаются на объявленные параметры;
- правила `source`: `lookup` → `queryId`; `static` → непустой `options`; `date`/`datetime` → без `queryId`
  и `options`; `multiple` только для `lookup`/`static`; `dependsOn` — на объявленный выше параметр,
  не множественный.

Запуск: `cd frontend && npm run validate -- ../backend/templates/*.json`.

## Полный пример

Рабочий шаблон со всеми видами параметров: `backend/templates/params-demo.json`
(справочник, мультивыбор с каскадом, статический набор, две даты, таблица и две диаграммы).
