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
| `datasets` | object | да | именованные наборы данных: `имя → { sql, params }` (ADR-27, ADR-36) |
| `folder` | string | нет | папка в каталоге отчётов (ADR-30) |
| `tags` | string[] | нет | теги для фильтра в каталоге |
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
{
  "kind": "lookup",
  "sql": "SELECT id::text AS value, name AS label FROM regions ORDER BY name"
}
{
  "kind": "lookup",
  "sql": "SELECT DISTINCT category AS value, category AS label FROM sales WHERE region_id = :region::int ORDER BY 1",
  "dependsOn": "region"
}
{ "kind": "static", "options": [{ "value": "all", "label": "Все строки" }] }
{ "kind": "date" }
```

| `kind` | Что даёт | Требования |
|---|---|---|
| `date` | контрол `input[type=date]` | `type` = `date`, без `queryId`/`options` |
| `datetime` | контрол `input[type=datetime-local]` | `type` = `datetime` |
| `static` | список из `source.options` | непустой `options`, `type` = `select`, без `queryId` |
| `lookup` | список из `GET /api/reports/{reportId}/lookups/{param}` | обязателен `sql` (ровно две колонки: `value`, `label`), `type` = `select`, без `options` и `queryId` |

Дополнительно:

- `sql` — текст запроса списка; плейсхолдеры `:name` подставляются из query-строки (binding),
  клиенту SQL не отдаётся: `GET /api/reports/{id}` вырезает его на сервере.
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
    "sql": "SELECT region, category, product, amount, profit, order_date FROM sales WHERE order_date >= :from::date AND order_date <= :to::date",
    "params": { "from": "$from", "to": "$to" }
  }
}
```

| Поле | Смысл |
|---|---|
| `sql` | запрос набора данных; плейсхолдеры `:name` подставляются через binding (ADR-27) |
| `params` | отображение «параметр SQL → `$<имя параметра отчёта>`»; необязательно, если имена совпадают |

`queryId` и реестр `backend/config/queries.json` для наборов данных больше не используются.
Типы колонок сервер определяет по результату запроса. Валидатор проверяет, что `sql` задан,
а каждая ссылка `$name` указывает на объявленный параметр. Клиенту `sql` не отдаётся:
`GET /api/reports/{id}` вырезает его так же, как SQL справочников.

## blocks[] — блоки вывода

### Текст

```json
{ "type": "text", "text": "Регион {{region}}, период {{from}} — {{to}}", "width": "full" }
```

`{{name}}` — значение параметра (для справочников — подпись). Необъявленный параметр — ошибка валидации.
`width`: `full` (по умолчанию) или `half`.

### Drill-through в табличном блоке (ADR-29)

```json
"drill": {
  "report": "sales-drill",
  "label": "Детали по группе",
  "params": { "region": "group.region", "category": "group.category", "from": "param.from", "to": "param.to" }
}
```

| Значение | Смысл |
|---|---|
| `group.<field>` | значение группы этой строки (поле должно быть колонкой-группой) |
| `param.<name>` | текущее значение параметра отчёта |
| любая другая строка | литерал |

Ссылкой становится ячейка группы (детальная строка или подытог), параметры уходят в URL целевого отчёта.
Если целевой отчёт объявил их обязательными, у него должны быть значения по умолчанию — иначе открытие
из каталога даст 422.

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

## styles — оформление таблицы

Необязательная секция (ADR-23). Порядок применения: умолчания версии 1.0 → `styles.preset` →
`styles` → `styles.<зона>` → `blocks[].styles` → `styles.columns.<поле>`.

| Поле | Смысл |
|---|---|
| `preset` | `default`, `compact`, `print`, `dark` — встроенные наборы |
| `font` | `family` (allowlist), `size` 8…32, `weight` 300…800, `style`, `lineHeight` 1…2 |
| `colors` | `text`, `background`, `border` — `#rgb`, `#rrggbb`, `transparent` |
| `layout` | `rowHeight`, `headerHeight`, `subtotalHeight`, `grandtotalHeight` (16…120), `cellPadding`, `borders` (`none`/`horizontal`/`grid`), `zebra`, `stripeColor`, `tableLayout` (`auto`/`fixed`) |
| `header` / `body` / `subtotal` / `grandtotal` | стиль зоны: `font`, `colors`, `align`, `verticalAlign`, `padding` |
| `columns.<field>` | `width` (px или `"20%"`), `minWidth`, `maxWidth`, `align`, `headerAlign`, `font`, `colors`, `padding`, `format` |
| `layout.numericAlign` | выравнивание числовых колонок по умолчанию: `right` (по умолчанию), `left`, `center`, `inherit` |
| `blocks[].styles` | те же ключи, но только для этого блока таблицы |

### Форматы чисел и дат

`format` задаётся у колонки (`columns[].format`) или в стиле (`styles.columns.<field>.format`);
стиль колонки перекрывает колонку. Допустимы строка-псевдоним и объект:

| Параметр | Значения | По умолчанию |
|---|---|---|
| `style` | `number`, `integer`, `percent`, `currency`, `date` | `number` |
| `decimals` | 0…6 | как у Intl (до 3), для `percent` — 1, для `currency` — 2 |
| `group` | разделитель тысяч (до 3 символов) | неразрывный пробел |
| `decimal` | десятичный разделитель (1 символ) | `,` |
| `prefix`, `suffix` | до 4 символов | пусто |
| `negative` | `minus` или `parentheses` | `minus` |

Примеры: `1356707500 → 1 356 707 500`, `{style:'number', decimals:0, suffix:' ₽'} → 58 300 ₽`,
`{negative:'parentheses'} → (1 500)`, `0.2054 → 20,5 %`, `2026-03-05 → 05.03.2026`,
`2026-03-05T14:30 → 05.03.2026 14:30`.

Примеры оформления и форматов: `docs/styling-examples.md`, шаблон `sales-formatted`, шаблоны `finance-print` (preset print) и `ops-dark` (preset dark).

## rules — условное форматирование

Правила задаются в блоке таблицы (`blocks[].rules`) и меняют только оформление, не данные.

```json
"rules": [
  { "when": { "column": "profit", "op": "<", "value": 0 },
    "apply": { "colors": { "text": "danger" }, "font": { "weight": 600 } } },
  { "when": { "column": "Маржа", "op": "between", "value": [0, 0.2] },
    "apply": { "colors": { "background": "warningBg" } }, "priority": 1 },
  { "when": { "column": "amount", "op": ">=", "value": 50000 },
    "apply": { "colors": { "background": "rowHighlight" } }, "scope": "row" },
  { "when": { "column": "amount", "op": ">", "value": 0 },
    "apply": { "colors": { "background": "totalBg" } }, "scope": "row", "zones": ["grandtotal"] }
]
```

| Поле | Значения | Смысл |
|---|---|---|
| `when` | `{column, op, value}` или `{all: [...]}` / `{any: [...]}` (без вложенности) | условие по сырому значению |
| `op` | `=`, `!=`, `>`, `>=`, `<`, `<=`, `between`, `empty`, `notEmpty`, `contains` | оператор сравнения |
| `apply` | стиль-объект: `font`, `colors`, `align`, `padding` | что меняем |
| `scope` | `cell` (по умолчанию) — колонка-цель, `row` — вся строка | область действия |
| `target` | имя колонки для `scope: cell` (по умолчанию `when.column`) | что красить |
| `zones` | `detail`, `subtotal`, `grandtotal` (по умолчанию `detail` + `subtotal`) | на каких строках |
| `priority` | число | порядок слияния, большее побеждает |

Колонка адресуется полем (`amount`) или заголовком вычисляемой колонки (`Маржа`).
Семантические цвета берутся из `styles.palette`: `"danger": "#b42318"` → `"text": "danger"`.
Порядок применения: зона → `styles.columns` → правила по `priority`.

## Что проверяет валидатор

Схема (типы, обязательность, `additionalProperties: false`) плюс семантические правила:

- `dataKey` объявлен в `datasets`; `xField`/`yField`/`seriesField` существуют в таблице набора;
- в колонке ровно одно из `field` / `expression`; `aggregate` требует `field`;
- групповые колонки идут до обычных; `subtotal` только у групповых; у группы с `children` нет `field`;
- есть хотя бы одна листовая колонка, если включён `grandTotal`;
- `{{name}}` в тексте и `$name` в `datasets` ссылаются на объявленные параметры;
- правила `source`: `lookup` → `sql` (и запрет `queryId`); `static` → непустой `options`; `date`/`datetime` → без `sql`
  и `options`; `multiple` только для `lookup`/`static`; `dependsOn` — на объявленный выше параметр,
  не множественный;
- стили: форматы цветов, диапазоны высот и размеров шрифта, allowlist шрифтов, `minWidth ≤ maxWidth`,
  стиль колонки только для существующего поля; предупреждения — невидимый текст (цвет = фон),
  слишком малая высота строки, больше 15 переопределений колонок;
- выражения разбираются статически: синтаксис (`+ - * /`, скобки, функции `sum/avg/min/max/count/countDistinct`)
  проверяется как **ошибка** (`round()`, `>`, `%` — недопустимы), а имя поля, которого нет в колонках
  таблицы, даёт **предупреждение** (`!`) — оно не блокирует загрузку, но видно в выводе.

Запуск: `cd frontend && npm run validate -- ../backend/templates/*.json`.

## Полный пример

Рабочий шаблон со всеми видами параметров: `backend/templates/params-demo.json`
(справочник, мультивыбор с каскадом, статический набор, две даты, таблица и две диаграммы).
