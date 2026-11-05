<template>
  <form class="params" @submit.prevent="$emit('submit')">
    <label v-for="p in params" :key="p.name" class="field">
      <span class="label">{{ p.label || p.name }}</span>

      <select
        v-if="p.type === 'select'"
        :value="modelValue[p.name] ?? ''"
        @change="update(p.name, $event.target.value)"
      >
        <option value="" disabled>—</option>
        <option v-for="o in p.options || []" :key="o.value" :value="o.value">{{ o.label }}</option>
      </select>

      <input
        v-else-if="p.type === 'date'"
        type="date"
        :value="modelValue[p.name] ?? ''"
        :required="p.required"
        @change="update(p.name, $event.target.value)"
      />

      <input
        v-else-if="p.type === 'int' || p.type === 'float'"
        type="number"
        :step="p.type === 'float' ? 'any' : '1'"
        :value="modelValue[p.name] ?? ''"
        :required="p.required"
        @change="update(p.name, $event.target.value)"
      />

      <input
        v-else-if="p.type === 'bool'"
        type="checkbox"
        :checked="modelValue[p.name] === 'true' || modelValue[p.name] === true"
        @change="update(p.name, $event.target.checked ? 'true' : 'false')"
      />

      <input
        v-else
        type="text"
        :value="modelValue[p.name] ?? ''"
        :required="p.required"
        @change="update(p.name, $event.target.value)"
      />
    </label>

    <button type="submit" :disabled="loading">{{ loading ? 'загрузка…' : 'Обновить' }}</button>
  </form>
</template>

<script setup>
const props = defineProps({
  params: { type: Array, required: true },
  modelValue: { type: Object, required: true },
  loading: { type: Boolean, default: false }
})
const emit = defineEmits(['update:modelValue', 'submit'])

function update(name, value) {
  emit('update:modelValue', { ...props.modelValue, [name]: value })
}
</script>

<style scoped>
.params {
  display: flex;
  flex-wrap: wrap;
  gap: 12px;
  align-items: flex-end;
  padding: 10px 12px;
  background: #fff;
  border: 1px solid #dfe3e8;
  border-radius: 8px;
  margin-bottom: 12px;
}
.field {
  display: flex;
  flex-direction: column;
  gap: 4px;
  font-size: 12px;
  color: #5b6472;
}
.field input,
.field select {
  font: inherit;
  padding: 4px 6px;
  border: 1px solid #cfd6de;
  border-radius: 6px;
  min-width: 150px;
}
.params button {
  font: inherit;
  padding: 6px 14px;
  border: 1px solid #4f8ef7;
  background: #4f8ef7;
  color: #fff;
  border-radius: 6px;
  cursor: pointer;
  height: 30px;
}
.params button:disabled {
  opacity: 0.6;
  cursor: default;
}
</style>
