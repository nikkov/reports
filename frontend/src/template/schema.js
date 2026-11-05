// Каноническая JSON Schema шаблона отчёта (v1).
// docs/template.schema.json генерируется из этого файла: npm run schema:json
export default {
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://rep.local/template.schema.json",
  "title": "Report template v1",
  "description": "Декларативное описание отчёта: параметры, наборы данных, блоки (текст/таблица/диаграмма).",
  "type": "object",
  "additionalProperties": false,
  "required": [
    "version",
    "id",
    "title",
    "blocks"
  ],
  "properties": {
    "version": {
      "const": 1
    },
    "id": {
      "type": "string",
      "pattern": "^[a-z0-9][a-z0-9-]*$"
    },
    "title": {
      "type": "string",
      "minLength": 1
    },
    "description": {
      "type": "string"
    },
    "params": {
      "type": "array",
      "items": {
        "$ref": "#/$defs/param"
      }
    },
    "datasets": {
      "type": "object",
      "additionalProperties": {
        "$ref": "#/$defs/dataset"
      }
    },
    "blocks": {
      "type": "array",
      "minItems": 1,
      "items": {
        "$ref": "#/$defs/block"
      }
    },
    "styles": {
      "$ref": "#/$defs/styles"
    }
  },
  "$defs": {
    "param": {
      "type": "object",
      "additionalProperties": false,
      "required": [
        "name",
        "type"
      ],
      "properties": {
        "name": {
          "type": "string",
          "pattern": "^[a-z][a-z0-9_]*$"
        },
        "type": {
          "enum": [
            "date",
            "datetime",
            "int",
            "float",
            "text",
            "bool",
            "select"
          ]
        },
        "label": {
          "type": "string"
        },
        "required": {
          "type": "boolean"
        },
        "default": {
          "type": [
            "string",
            "number",
            "boolean"
          ]
        },
        "options": {
          "type": "array",
          "items": {
            "type": "object",
            "additionalProperties": false,
            "required": [
              "value",
              "label"
            ],
            "properties": {
              "value": {
                "type": [
                  "string",
                  "number"
                ]
              },
              "label": {
                "type": "string"
              }
            }
          }
        },
        "source": {
          "type": "object",
          "additionalProperties": false,
          "required": [
            "kind"
          ],
          "properties": {
            "kind": {
              "enum": [
                "date",
                "datetime",
                "static",
                "lookup"
              ]
            },
            "queryId": {
              "type": "string",
              "minLength": 1
            },
            "options": {
              "type": "array",
              "minItems": 1,
              "items": {
                "type": "object",
                "additionalProperties": false,
                "required": [
                  "value",
                  "label"
                ],
                "properties": {
                  "value": {
                    "type": [
                      "string",
                      "number"
                    ]
                  },
                  "label": {
                    "type": "string"
                  }
                }
              }
            },
            "dependsOn": {
              "type": "string",
              "minLength": 1
            },
            "sql": {
              "type": "string",
              "minLength": 1
            }
          }
        },
        "multiple": {
          "type": "boolean"
        }
      }
    },
    "dataset": {
      "type": "object",
      "additionalProperties": false,
      "required": [
        "queryId"
      ],
      "properties": {
        "queryId": {
          "type": "string",
          "minLength": 1
        },
        "params": {
          "type": "object",
          "additionalProperties": {
            "type": "string"
          }
        }
      }
    },
    "column": {
      "type": "object",
      "additionalProperties": false,
      "required": [
        "title"
      ],
      "properties": {
        "field": {
          "type": "string",
          "minLength": 1
        },
        "title": {
          "type": "string",
          "minLength": 1
        },
        "group": {
          "type": "boolean"
        },
        "subtotal": {
          "type": "boolean"
        },
        "aggregate": {
          "enum": [
            "sum",
            "avg",
            "min",
            "max",
            "count",
            "countDistinct"
          ]
        },
        "expression": {
          "type": "string",
          "minLength": 1
        },
        "format": {
          "$ref": "#/$defs/formatSpec"
        },
        "type": {
          "enum": [
            "int",
            "float",
            "text",
            "date",
            "bool"
          ]
        },
        "decimals": {
          "type": "integer",
          "minimum": 0,
          "maximum": 6
        },
        "width": {
          "type": "integer",
          "minimum": 40,
          "maximum": 2000
        },
        "align": {
          "enum": [
            "left",
            "right",
            "center"
          ]
        },
        "children": {
          "type": "array",
          "minItems": 1,
          "items": {
            "$ref": "#/$defs/column"
          }
        },
        "style": {
          "$ref": "#/$defs/columnStyle"
        }
      }
    },
    "textBlock": {
      "type": "object",
      "additionalProperties": false,
      "required": [
        "type",
        "text"
      ],
      "properties": {
        "type": {
          "const": "text"
        },
        "text": {
          "type": "string",
          "minLength": 1
        },
        "width": {
          "enum": [
            "full",
            "half"
          ]
        }
      }
    },
    "tableBlock": {
      "type": "object",
      "additionalProperties": false,
      "required": [
        "type",
        "dataKey",
        "columns"
      ],
      "properties": {
        "type": {
          "const": "table"
        },
        "dataKey": {
          "type": "string",
          "minLength": 1
        },
        "collapsible": {
          "type": "boolean"
        },
        "grandTotal": {
          "type": "boolean"
        },
        "nullText": {
          "type": "string"
        },
        "width": {
          "enum": [
            "full",
            "half"
          ]
        },
        "columns": {
          "type": "array",
          "minItems": 1,
          "items": {
            "$ref": "#/$defs/column"
          }
        },
        "styles": {
          "$ref": "#/$defs/styles"
        }
      }
    },
    "chartBlock": {
      "type": "object",
      "additionalProperties": false,
      "required": [
        "type",
        "dataKey",
        "chartType",
        "xField",
        "yField"
      ],
      "properties": {
        "type": {
          "const": "chart"
        },
        "dataKey": {
          "type": "string",
          "minLength": 1
        },
        "chartType": {
          "enum": [
            "line",
            "bar",
            "pie"
          ]
        },
        "xField": {
          "type": "string",
          "minLength": 1
        },
        "yField": {
          "type": "string",
          "minLength": 1
        },
        "seriesField": {
          "type": "string"
        },
        "aggregate": {
          "enum": [
            "sum",
            "avg",
            "min",
            "max",
            "count",
            "countDistinct"
          ]
        },
        "title": {
          "type": "string"
        },
        "width": {
          "enum": [
            "full",
            "half"
          ]
        }
      }
    },
    "block": {
      "oneOf": [
        {
          "$ref": "#/$defs/textBlock"
        },
        {
          "$ref": "#/$defs/tableBlock"
        },
        {
          "$ref": "#/$defs/chartBlock"
        }
      ]
    },
    "color": {
      "type": "string",
      "pattern": "^#[0-9a-fA-F]{3}$|^#[0-9a-fA-F]{6}$|^transparent$"
    },
    "padding": {
      "type": "string",
      "pattern": "^\\d{1,2}px( \\d{1,2}px){0,3}$"
    },
    "styleObject": {
      "type": "object",
      "additionalProperties": false,
      "properties": {
        "font": {
          "type": "object",
          "additionalProperties": false,
          "properties": {
            "family": {
              "enum": [
                "system-ui",
                "Inter",
                "Roboto",
                "Arial",
                "Georgia",
                "Times New Roman",
                "Courier New",
                "monospace"
              ]
            },
            "size": {
              "type": "integer",
              "minimum": 8,
              "maximum": 32
            },
            "weight": {
              "enum": [
                300,
                400,
                500,
                600,
                700,
                800
              ]
            },
            "style": {
              "enum": [
                "normal",
                "italic"
              ]
            },
            "lineHeight": {
              "type": "number",
              "minimum": 1,
              "maximum": 2
            }
          }
        },
        "colors": {
          "type": "object",
          "additionalProperties": false,
          "properties": {
            "text": {
              "$ref": "#/$defs/color"
            },
            "background": {
              "$ref": "#/$defs/color"
            },
            "border": {
              "$ref": "#/$defs/color"
            }
          }
        },
        "align": {
          "enum": [
            "left",
            "right",
            "center"
          ]
        },
        "verticalAlign": {
          "enum": [
            "top",
            "middle",
            "bottom"
          ]
        },
        "padding": {
          "$ref": "#/$defs/padding"
        }
      }
    },
    "layout": {
      "type": "object",
      "additionalProperties": false,
      "properties": {
        "rowHeight": {
          "type": "integer",
          "minimum": 16,
          "maximum": 120
        },
        "headerHeight": {
          "type": "integer",
          "minimum": 16,
          "maximum": 120
        },
        "subtotalHeight": {
          "type": "integer",
          "minimum": 16,
          "maximum": 120
        },
        "grandtotalHeight": {
          "type": "integer",
          "minimum": 16,
          "maximum": 120
        },
        "cellPadding": {
          "$ref": "#/$defs/padding"
        },
        "borders": {
          "enum": [
            "none",
            "horizontal",
            "grid"
          ]
        },
        "zebra": {
          "type": "boolean"
        },
        "stripeColor": {
          "$ref": "#/$defs/color"
        },
        "tableLayout": {
          "enum": [
            "auto",
            "fixed"
          ]
        },
        "numericAlign": {
          "enum": [
            "right",
            "left",
            "center",
            "inherit"
          ]
        }
      }
    },
    "styles": {
      "type": "object",
      "additionalProperties": false,
      "properties": {
        "preset": {
          "enum": [
            "default",
            "compact",
            "print",
            "dark"
          ]
        },
        "font": {
          "$ref": "#/$defs/styleObject/properties/font"
        },
        "colors": {
          "$ref": "#/$defs/styleObject/properties/colors"
        },
        "layout": {
          "$ref": "#/$defs/layout"
        },
        "header": {
          "$ref": "#/$defs/styleObject"
        },
        "body": {
          "$ref": "#/$defs/styleObject"
        },
        "subtotal": {
          "$ref": "#/$defs/styleObject"
        },
        "grandtotal": {
          "$ref": "#/$defs/styleObject"
        },
        "columns": {
          "type": "object",
          "additionalProperties": {
            "$ref": "#/$defs/columnStyle"
          }
        }
      }
    },
    "columnStyle": {
      "type": "object",
      "additionalProperties": false,
      "properties": {
        "width": {
          "oneOf": [
            {
              "type": "integer",
              "minimum": 40,
              "maximum": 2000
            },
            {
              "type": "string",
              "pattern": "^([1-9]|[1-9][0-9]|100)%$"
            }
          ]
        },
        "minWidth": {
          "type": "integer",
          "minimum": 20,
          "maximum": 2000
        },
        "maxWidth": {
          "type": "integer",
          "minimum": 20,
          "maximum": 4000
        },
        "align": {
          "enum": [
            "left",
            "right",
            "center"
          ]
        },
        "font": {
          "$ref": "#/$defs/styleObject/properties/font"
        },
        "colors": {
          "$ref": "#/$defs/styleObject/properties/colors"
        },
        "padding": {
          "$ref": "#/$defs/padding"
        },
        "format": {
          "$ref": "#/$defs/formatSpec"
        },
        "headerAlign": {
          "enum": [
            "left",
            "right",
            "center"
          ]
        }
      }
    },
    "formatObject": {
      "type": "object",
      "additionalProperties": false,
      "properties": {
        "style": {
          "enum": [
            "number",
            "integer",
            "percent",
            "currency",
            "date"
          ]
        },
        "decimals": {
          "type": "integer",
          "minimum": 0,
          "maximum": 6
        },
        "group": {
          "type": "string",
          "maxLength": 3
        },
        "decimal": {
          "type": "string",
          "maxLength": 1
        },
        "prefix": {
          "type": "string",
          "maxLength": 4
        },
        "suffix": {
          "type": "string",
          "maxLength": 4
        },
        "negative": {
          "enum": [
            "minus",
            "parentheses"
          ]
        }
      }
    },
    "formatSpec": {
      "oneOf": [
        {
          "enum": [
            "number",
            "integer",
            "percent",
            "currency",
            "date"
          ]
        },
        {
          "$ref": "#/$defs/formatObject"
        }
      ]
    }
  }
}
