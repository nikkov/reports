# Справочник шаблона отчёта

Шаблон — JSON-объект, который хранится в таблице `report_templates` (ADR-21) и валидируется
схемой `frontend/src/template/schema.js` + семантическим валидатором `validate.js`;
те же правила повторяет сервер (`backend/src/validate.cc`).
Канон схемы — `frontend/src/template/schema.js`, файл `docs/template.schema.json` генерируется из него
(`npm run schema:json`).

## Общая структура

```json
{
  "version": 2,
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
| `version` | integer | да | версия формата шаблона, сейчас `2` (миграции ниже) |
| `id` | string | да | идентификатор отчёта; он же ключ в `report_templates` и в URL `?report=<id>` |
| `title` | string | да | заголовок страницы и карточки в списке |
| `description` | string | нет | пояснение на карточке списка отчётов |
| `params` | array | нет | параметры, которые пользователь задаёт перед построением |
| `datasets` | object | да | именованные наборы данных: `имя → { sql, params }` (ADR-27, ADR-36) |
| `folder` | string | нет | папка в каталоге отчётов (ADR-30) |
| `refreshSeconds` | integer | нет | автообновление данных на экране, 0…3600 с; пусто — выключено (ADR-39) |
| `live` | boolean | нет | обновлять по событию: сервер сам сообщает о публикации шаблона (ADR-42); не смешивается с `refreshSeconds` |
| `tags` | string[] | нет | теги для фильтра в каталоге |
| `blocks` | array | да | блоки вывода по порядку: текст, таблица, диаграмма |

### Автообновление и обновление по событию

Два независимых способа держать экран свежим:

- `refreshSeconds` — страница сама перезапрашивает данные каждые N секунд (ADR-39). Просто, но при периоде
  меньше TTL кэша (`REP_CACHE_TTL_SECONDS`, по умолчанию 60) данные приходят из кэша;
- `live: true` — страница держит одно SSE-соединение `GET /api/report/events?report=<id>`; сервер пишет
  в поток сразу после публикации или отката шаблона, страница перечитывает шаблон и данные (ADR-44).
  Раз в 15 с сервер шлёт комментарий-пульс, клиент переподключается сам. Запросов в простое нет,
  задержка — доли секунды; цена — открытое соединение на каждого зрителя.

```json
{ "live": true }
```

## Версия формата и миграции

`version` — версия формата шаблона, сейчас **2**. Поддерживаются 1 и 2:

| Версия | Отличие | Что делает миграция |
|---|---|---|
| 1 | `chartType` у диаграмм был необязательным | при сохранении подставляется `bar` |
| 2 | `chartType` обязателен | — |

- шаблон без `version` считается версией 0 и получает актуальную;
- шаблон версии больше текущей отклоняется: «шаблон версии N, сервер поддерживает до 2 — обновите сервер»;
- сервер мигрирует при сохранении, публикации и загрузке в память (в БД хранится нормализованное тело);
- админка мигрирует при открытии и сообщает об этом в статусе;
- валидатор предупреждает о старой версии, но не блокирует сохранение.

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

### Агрегация на сервере (ADR-37)

```json
"block": {
  "type": "table",
  "dataKey": "sales",
  "aggregate": {
    "groupBy": ["category"],
    "measures": [{ "field": "amount", "agg": "sum" }, { "field": "profit", "agg": "sum" }]
  }
}
```

`groupBy` — колонки набора, которые остаются в результате (минимум одна); `measures` — колонки
с агрегатом: `sum`, `avg`, `min`, `max`, `count`. Сервер выполняет `GROUP BY` в PostgreSQL и отдаёт
уже сгруппированные строки — клиент не получает детализацию.

Что проверяется на сохранении: `groupBy` — непустой массив, `measures` — непустой массив объектов
`{field, agg}`, агрегат только из списка (иначе `blocks.<n>.aggregate.measures.<i>.agg`).
Существование полей в SQL на этапе валидации не проверяется — колонки запроса известны только базе:
неверное имя вернёт 422 при построении отчёта (и xlsx/CSV-экспорте). Колонки таблицы при этом
перечисляют те же поля, что возвращает `GROUP BY`.

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

### Drill-through с диаграммы (ADR-43)

```json
{
  "type": "chart",
  "dataKey": "sales",
  "chartType": "bar",
  "xField": "category",
  "seriesField": "region",
  "yField": "amount",
  "drill": {
    "report": "sales-drill",
    "label": "Детали по региону и категории",
    "params": { "category": "x", "region": "series", "from": "param.from", "to": "param.to" }
  }
}
```

| Значение | Смысл |
|---|---|
| `x` | категория точки, по которой кликнули (значение `xField`) |
| `series` | имя серии точки (значение `seriesField`; у круговой диаграммы его нет) |
| `param.<name>` | текущее значение параметра отчёта |
| любая другая строка | литерал |

У диаграммы курсор становится указателем, клик по столбцу/сектору открывает целевой отчёт с параметрами
в URL. `group.<field>` в диаграмме не поддержан — там нет групп таблицы; валидатор такую ссылку отклонит.

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
| `dataKey` | имя набора из `datasets` (обязательно) |
| `title` | подпись блока; в экспорте становится именем листа Excel |
| `aggregate` | агрегация на сервере: `{ groupBy: [...], measures: [{ field, agg }] }` (ADR-37, ниже) |
| `collapsible` | разрешить сворачивание групп (шеврон в ячейке групповой колонки) |
| `grandTotal` | печатать строку общего итога |
| `nullText` | текст для пустых значений (по умолчанию `(пусто)`) |
| `width` | `full` / `half` |
| `columns` | колонки таблицы (обязательно, минимум одна) |
| `styles` | оформление блока (см. «styles») |
| `rules` | правила условного форматирования (см. «rules») |
| `drill` | переход к детальному отчёту (ADR-29) |

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
| `chartType` | `line`, `bar`, `pie`, `area`, `scatter`, `gauge`, `sankey` (см. таблицу ниже) |
| `xField` / `yField` | категория и значение; проверяются на наличие в наборе данных |
| `seriesField` | необязательное разбиение на серии (см. ниже) |
| `sourceField` / `targetField` / `valueField` | узлы и вес потока — только для `sankey` |
| `min` / `max` / `unit` | шкала и суффикс подписи — только для `gauge` |
| `thresholds` / `zoneColors` | границы цветовых зон и их цвета — только для `gauge` (см. «Цветовые зоны») |
| `aggregate` | агрегат: у `line`/`bar`/`pie`/`area`/`scatter` — по `yField`, у `sankey` — по `valueField`, у `gauge` — по единственному значению (`sum` по умолчанию) |
| `title` | заголовок блока (у `gauge` он же подпись под шкалой) |
| `width` | `full` / `half` |

Обязательные поля зависят от вида: `line`, `bar`, `pie`, `area`, `scatter` требуют `xField` и
`yField`; `gauge` — только `yField` (и не принимает `xField`/`seriesField`); `sankey` —
`sourceField`, `targetField`, `valueField` (и не принимает `xField`/`yField`), причём это три разных поля;
`band` — `xField`, `yField`, `lowField`, `highField` (значение и границы — три разных поля).

### Какие виды диаграмм поддерживаются

| `chartType` | Что рисует | Нужны поля |
|---|---|---|
| `line` | линия по категориям, `seriesField` даёт несколько линий | `xField`, `yField` |
| `bar` | столбцы, `seriesField` — группы столбцов | `xField`, `yField` |
| `pie` | круговая по категориям (одна серия) | `xField`, `yField` |
| `area` | линия с заливкой до оси | `xField`, `yField` |
| `scatter` | точки без соединения | `xField`, `yField` |
| `gauge` | спидометр: одно число на шкале | `yField`; необязательно `min`, `max`, `unit` |
| `sankey` | потоки между узлами (категория → регион и т. п.) | `sourceField`, `targetField`, `valueField` |
| `band` | линия значения и закрашенный коридор между границами | `xField`, `yField`, `lowField`, `highField`; необязательно `bandColor` |

`gauge` без `min`/`max` сам подбирает верх шкалы выше значения (низ — 0). Отрицательные и нулевые
потоки `sankey` не рисуются и отбрасываются; строки с пустым источником или приёмником
пропускаются, поэтому диаграмма не падает на «грязных» данных.

### Пример: gauge — одно число

```json
{
  "type": "chart",
  "dataKey": "margin",
  "chartType": "gauge",
  "yField": "margin_pct",
  "aggregate": "max",
  "min": 0,
  "max": 100,
  "unit": "%",
  "title": "Маржа за период"
}
```

Набор `margin` возвращает одну строку с посчитанной в SQL маржой; `aggregate: "max"` берёт это
значение как есть. Для выборки из многих строк годятся `sum`, `avg`, `min`, `max`, `count`.

### Цветовые зоны gauge

Число границ N даёт N+1 зону: каждая граница — это «стык» двух цветов, поэтому цветов нужно ровно N+1.
Порядок цветов слева направо совпадает с порядком зон на шкале.

```json
{
  "chartType": "gauge",
  "yField": "margin_pct",
  "aggregate": "max",
  "min": 0,
  "max": 40,
  "unit": "%",
  "thresholds": [10, 15, 25, 30],
  "zoneColors": ["#c62828", "#f9a825", "#2e7d32", "#f9a825", "#c62828"]
}
```

| Что | Значение |
|---|---|
| `thresholds` | 2…8 чисел, строго возрастают (`p1 < p2 < …`) |
| `zoneColors` | `thresholds.length + 1` цветов: hex (`#rgb`, `#rrggbb`), `transparent` или имя из `styles.palette` |
| зона 1 | `min … p1` — первый цвет (в примере красный) |
| зона 2 | `p1 … p2` — второй (жёлтый) |
| зона 3 | `p2 … p3` — третий (зелёный) |
| зона 4 | `p3 … p4` — четвёртый (жёлтый) |
| зона 5 | `p4 … max` — пятый (красный) |

В примере выше шкала 0…40, границы 10/15/25/30, значение 20,54 % попадает в зелёную зону —
указатель и число окрашиваются в её цвет. Полоса прогресса при заданных зонах не рисуется
(она закрыла бы цвета). Границы вне `min…max` обрезаются по шкале; если цветов не столько,
сколько зон, валидатор сообщает об ошибке, а страница рисует шкалу без зон вместо падения.

Готовый пример — шаблон `margin-zones` («Маржа: цветовые зоны по четырём границам»).

### Пример: sankey — потоки между узлами

```json
{
  "type": "chart",
  "dataKey": "flows",
  "chartType": "sankey",
  "sourceField": "category",
  "targetField": "region",
  "valueField": "amount",
  "aggregate": "sum",
  "title": "Категория → регион, сумма продаж"
}
```

Сервер отдаёт строки как есть, пары «источник → приёмник» и суммы по ним считает клиент.
Узлы нумеруются в порядке появления, поэтому диаграмма стабильна между обновлениями.

### Пример: band — значение и коридор

Точка графика описывается тремя числами: значение, нижняя и верхняя границы. Диапазон между
границами закрашивается цветом `bandColor` (hex или токен `styles.palette`).

```json
{
  "type": "chart",
  "dataKey": "days",
  "chartType": "band",
  "xField": "day",
  "yField": "value_ms",
  "lowField": "low_ms",
  "highField": "high_ms",
  "bandColor": "#2e7d32",
  "title": "Время ответа, мс: значение и коридор min…max"
}
```

| Поле | Смысл |
|---|---|
| `xField` | категория (день, сутки, интервал) |
| `yField` | само значение — линия с точками |
| `lowField` / `highField` | нижняя и верхняя границы коридора |
| `bandColor` | цвет заливки (по умолчанию `#2e7d32`), непрозрачность 0,22 |

Правила: границы — разные поля, значение не может совпадать с границей, `seriesField` в полосе
не используется. Если у строки нет границ, точка рисуется без полосы; если границы перепутаны
(`low > high`), полоса вырождается в нулевую и не ломает график. Повторяющаяся категория берётся
один раз — линия не идёт назад.

Готовый пример — шаблон `ops-band` («Время ответа: значение и коридор»): значение — среднее время
ответа по сервисам за день, коридор — от минимума до максимума по тем же сервисам.

Drill из `sankey` подставляет источник в `x`, приёмник — в `series`; у `gauge` ни `x`, ни `series` нет —
ссылки на них валидатор считает ошибкой (можно `param.<name>` и литералы).

Диаграммы не попадают ни в Excel, ни в печать — это записано в `docs/dod.md`.

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
