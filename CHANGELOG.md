# История версий

## v1.1.0 — 2026-11-05

Принята постановщиком. Оформление и форматы задаются шаблоном; SQL справочников параметров переехал
в шаблон. Полный чек-лист — `docs/dod.md`, раздел «Приёмка версии 1.1».

### Стили таблиц из шаблона (ADR-23)

- секция `styles` (верхний уровень) + `blocks[].styles` + `columns[].style`; порядок применения:
  умолчания → `preset` → `styles` → `styles.<зона>` → блок → колонка;
- зоны отдельно: `header`, `body`, `subtotal`, `grandtotal` — шрифт, цвета текста и фона, выравнивание,
  вертикальное выравнивание, отступы;
- раскладка: `rowHeight`, `headerHeight`, `subtotalHeight`, `grandtotalHeight`, `cellPadding`,
  `borders` (`none`/`horizontal`/`grid`), `zebra`, `stripeColor`, `tableLayout`;
- колонки: `width` в px или `%`, `minWidth`, `maxWidth`, `align`, `headerAlign`, `format`;
- пресеты: `default` (вид 1.0), `compact`, `print`, `dark`;
- реализация — CSS-переменные на корне таблицы и классы зон, без inline-стилей на ячейках;
  виртуализация переведена на префиксные суммы высот (строки, подытоги и итог могут быть разной высоты).

### Выравнивание и форматы чисел

- `layout.numericAlign` — политика для числовых колонок (`right` по умолчанию, `left`, `center`, `inherit`);
- `headerAlign` у колонки — заголовок можно выровнять иначе, чем данные;
- форматы: `number`, `integer`, `percent`, `currency`, `date` — строкой или объектом
  (`style`, `decimals`, `group`, `decimal`, `prefix`, `suffix`, `negative`: `minus`/`parentheses`);
- резолвер `report/format.js`, 12 юнит-тестов; формат из `styles.columns` сильнее формата колонки.

### Справочники параметров: SQL в шаблоне (ADR-24, заменяет часть ADR-22)

- `params[].source.sql` — обязателен для `kind: "lookup"`; `queryId` удалён и запрещён валидатором;
- API: `GET /api/reports/{reportId}/lookups/{param}?<родитель>=…`, `GET /api/lookups/{id}` удалён;
- `GET /api/reports/{id}` вырезает `sql` из ответа — клиент SQL не видит;
- серверный кэш и `REP_LOOKUP_TTL_SECONDS` удалены, кэш только на клиенте;
- из `queries.json` удалены четыре `lookup_*`; 6 шаблонов переведены на `source.sql`.

### Новые шаблоны

`finance-print` (печатная форма), `ops-dark` (тёмная тема, зебра), `sales-formatted`
(форматы и выравнивание), `sales-typography` (размеры шрифта по зонам, все виды выравнивания).
Всего в базе 11 шаблонов.

### Проверки

`REP_E2E=1 ./scripts/check.sh` → `ALL CHECKS PASSED`: Conan v1, запрет enterprise-компонентов,
лицензии, сборка и `ctest` (13 кейсов), покрытие `engine/` 97,6 % (порог 90 %), сборка фронтенда,
валидация 11 шаблонов, vitest 77, golden 15/15, e2e 25 (включая проверку консоли браузера).

### Известные ограничения

- переменная высота строк и перенос длинного текста не поддерживаются (фиксированные высоты по зонам);
- условного форматирования по значениям нет;
- в диаграммах `yField` — только поле, без выражений;
- hot-reload шаблонов не поддерживается: после `templates-push` нужен рестарт сервиса;
- read-only роль БД для справочников (задача 5.9) отложена на финал — соединение идёт под `postgres`;
- рендер 50 000 категорий — 1,63–1,71 с, принято как норма (бюджет 2 с).

## v1.0.0 — 2026-11-05

Первая версия, принята постановщиком. Состав и известные ограничения — `docs/dod.md`.

### Бэкенд (C++ / Drogon 1.9.8, Conan v1)

- `GET /api/health`, `GET /api/reports`, `GET /api/reports/{id}`, `POST /api/report/data`,
  `GET /api/lookups/{id}`;
- SQL только на сервере: реестр `backend/config/queries.json`, параметры через binding;
- лимит 50 000 строк (`REP_MAX_ROWS`), `statement_timeout` 2 с, коды 404/422/504;
- справочники с кешем в памяти и TTL (`REP_LOOKUP_TTL_SECONDS`);
- шаблоны читаются из PostgreSQL на старте (ADR-21), файлы — источник в репозитории;
- 13 тестов Catch2 (33 assertions) в `ctest`.

### Фронтенд (Vue 3 + Vite + ECharts)

- собственный рендерер `ReportTable.vue` (ADR-20): 50 000 строк — 229 мс, 38 строк в DOM;
- группировка любой глубины, подытоги, общий итог, объединение ячеек, сворачивание групп
  («свернуть/развернуть всё»);
- вычисляемые колонки: `+ - * /`, скобки, `sum/avg/min/max/count/countDistinct`, области
  «строка / группа / итог», статическая проверка выражений в валидаторе и баннер при ошибке;
- диаграммы `line`/`bar`/`pie`, серии по `seriesField`, макет `full`/`half`;
- параметры из БД: `source.kind` = `date`/`datetime`/`static`/`lookup`, мультивыбор в дропдауне
  с чекбоксами, каскад `dependsOn`, в запрос уходит ID, пользователь видит NAME (ADR-22);
- экран списка отчётов с поиском и составом параметров; песочница рендерера `/spike.html`.

### Шаблоны и данные

- 7 шаблонов: `sales-summary`, `sales-by-region`, `sales-matrix`, `sales-detail`,
  `finance-plan-fact`, `ops-sla`, `params-demo`;
- синтетика: `sales` 50 000, `regions` 5, `finance` 576, `ops` 540; `db/init.sh` идемпотентен.

### Проверки

- `REP_E2E=1 ./scripts/check.sh` → `ALL CHECKS PASSED`: Conan v1, запрет enterprise-компонентов,
  лицензии, сборка и `ctest`, покрытие `engine/` ≥ 90 %, сборка фронтенда, валидация шаблонов,
  vitest 48, golden 15/15, e2e 19 (включая проверку консоли браузера).

### Документация

`docs/template-reference.md`, `docs/expressions.md`, `docs/how-to-add-report.md`, `docs/dod.md`,
`docs/decisions.md`, `THIRD_PARTY.md`, `README.md`.

### Известные ограничения

- чистая сборка на пустом Conan-кэше не проверялась;
- hot-reload шаблонов не поддерживается (нужен рестарт сервиса);
- в диаграммах `yField` — только поле, без выражений;
- экспорт, drill-through, рассылка, аутентификация — вне области работ (ТЗ §6).
