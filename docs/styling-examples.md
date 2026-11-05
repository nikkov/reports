# Примеры стилей таблиц

Стили задаются в шаблоне (ADR-23). Порядок применения: умолчания версии 1.0 → `styles.preset` →
`styles` → `styles.<зона>` → `blocks[].styles` → `styles.columns.<поле>`.
Полная таблица ключей — `docs/template-reference.md`, проектное описание — `docs/styling-design.md`.

Если секции `styles` нет, отчёт выглядит ровно как в версии 1.0.

## Минимальный случай: только ширины и высота строк

```json
"styles": {
  "layout": { "rowHeight": 24, "headerHeight": 30 },
  "columns": {
    "account": { "width": "30%" },
    "fact_amount": { "width": 150, "align": "right" }
  }
}
```

## Пресет `compact` — плотная таблица

```json
"styles": { "preset": "compact" }
```

Даёт шрифт 12 px, строки 22 px, шапку 24 px, отступы 1 px 6 px. Удобно для отчётов на 50 000 строк.

## Пресет `print` — печатная форма

```json
"styles": {
  "preset": "print",
  "layout": { "headerHeight": 34, "grandtotalHeight": 30, "tableLayout": "fixed" },
  "columns": { "period": { "width": 110 }, "account": { "width": "30%" } }
}
```

Шрифт с засечками (Times New Roman), сетка границ, белая шапка с жирным чёрным текстом.
Готовый пример — шаблон `finance-print`.

## Пресет `dark` — тёмная тема с зеброй

```json
"styles": {
  "preset": "dark",
  "font": { "family": "Inter", "size": 12 },
  "layout": { "zebra": true, "stripeColor": "#1b222c", "borders": "horizontal" },
  "header": { "font": { "weight": 700 }, "colors": { "text": "#9fc4ff", "background": "#232c38" } },
  "columns": { "failures": { "colors": { "text": "#ff9a9a" }, "align": "right" } }
}
```

Готовый пример — шаблон `ops-dark`. Диаграммы этого блока берут цвет текста и шрифт из тех же стилей
(оси, легенда, подписи).

## Свои цвета и шрифт без пресета

```json
"styles": {
  "font": { "family": "Georgia", "size": 14, "lineHeight": 1.5 },
  "colors": { "text": "#2b2b2b", "background": "#fffdf8", "border": "#d8cfc0" },
  "layout": { "rowHeight": 28, "headerHeight": 32, "cellPadding": "6px 10px", "borders": "horizontal" },
  "header": { "colors": { "text": "#ffffff", "background": "#6b4a17" } },
  "subtotal": { "font": { "weight": 600 }, "colors": { "background": "#f6f0e6" } },
  "grandtotal": { "font": { "weight": 800 }, "colors": { "background": "#efe3d2", "text": "#3a2a12" } }
}
```

## Что доступно

| Что | Ключи |
|---|---|
| Шрифт | `font.family` (allowlist), `font.size` 8…32, `font.weight` 300…800, `font.style`, `font.lineHeight` |
| Цвета | `colors.text`, `colors.background`, `colors.border` — `#rgb`, `#rrggbb`, `transparent` |
| Раскладка | `layout.rowHeight`, `headerHeight`, `subtotalHeight`, `grandtotalHeight` (16…120), `cellPadding`, `borders` (`none`/`horizontal`/`grid`), `zebra`, `stripeColor`, `tableLayout` |
| Зоны | `header`, `body`, `subtotal`, `grandtotal` — каждая со своими шрифтом, цветами, `align`, `verticalAlign` |
| Колонки | `width` (px или `"20%"`), `minWidth`, `maxWidth`, `align`, `font`, `colors`, `padding` |

Шрифты ограничены списком: `system-ui`, `Inter`, `Roboto`, `Arial`, `Georgia`, `Times New Roman`,
`Courier New`, `monospace`. Причина — стабильные скриншоты и защита от инъекции CSS.

## Выравнивание и форматы чисел

```json
"styles": {
  "layout": { "numericAlign": "right" },
  "header": { "align": "center" },
  "columns": {
    "order_date": { "width": 110, "align": "center", "format": "date" },
    "amount": {
      "width": 160, "align": "right", "headerAlign": "right",
      "format": { "style": "number", "decimals": 0, "group": " ", "suffix": " ₽" }
    },
    "profit": {
      "align": "right", "headerAlign": "right",
      "format": { "style": "number", "decimals": 0, "negative": "parentheses", "suffix": " ₽" }
    }
  }
}
```

- `layout.numericAlign` — выравнивание числовых колонок по умолчанию (`right` / `left` / `center` / `inherit`);
- `header.align` / `columns.<field>.headerAlign` — заголовок можно выровнять иначе, чем данные;
- `columns.<field>.align` перекрывает всё остальное для своей колонки.

Форматы: `number`, `integer`, `percent`, `currency`, `date` — строкой или объектом с `decimals`,
`group`, `decimal`, `prefix`, `suffix`, `negative` (`minus` / `parentheses`).
Готовый пример — шаблон `sales-formatted`:

```
05.03.2026 | Восток | Аксессуары | 58 300 ₽ | 9 675 ₽ | 16,60 %
```

## Ограничения версии 1.1

- высоты фиксированные по зонам: перенос длинного текста и переменная высота строки не поддерживаются
  (потребуют измерения DOM и пересчёта смещений виртуализации);
- условное форматирование («отрицательное — красным») не входит: планируется отдельным слоем правил;
- диаграммы берут только цвет текста и шрифт, стилизация осей по отдельным ключам не делалась.
