# История версий

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
