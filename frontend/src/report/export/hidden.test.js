import { describe, expect, it } from 'vitest'
import { leafColumns } from './xlsx.js'

// Скрытые колонки (ADR-158): в книгу и на экран не попадают, но остаются в данных.
describe('leafColumns и скрытые колонки', () => {
  const block = {
    columns: [
      { field: 'region', title: 'Регион', group: true },
      { field: 'amount', title: 'Сумма', aggregate: 'sum' },
      { field: 'profit', title: 'Прибыль', hidden: true, aggregate: 'sum' },
      { title: 'Финансы', children: [
        { field: 'qty', title: 'Кол-во' },
        { field: 'customer', title: 'Клиент', hidden: true }
      ] }
    ]
  }

  it('скрытые колонки не попадают в листья', () => {
    const leaves = leafColumns(block).map((l) => l.col.field).filter(Boolean)
    expect(leaves).toEqual(['region', 'amount', 'qty'])
  })

  it('скрытая колонка внутри группы не ломает заголовок группы', () => {
    const leaves = leafColumns(block)
    const qty = leaves.find((l) => l.col.field === 'qty')
    expect(qty.headerPath).toEqual(['Финансы'])
  })

  it('если скрыта вся группа — листьев из неё нет', () => {
    const onlyHidden = { columns: [{ title: 'Скрытое', children: [{ field: 'x', title: 'X', hidden: true }] },
                                   { field: 'y', title: 'Y' }] }
    expect(leafColumns(onlyHidden).map((l) => l.col.field)).toEqual(['y'])
  })
})
