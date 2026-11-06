<script setup lang="ts">
// Calliope remote microphone pairing and list (Apollo fork).
import { onBeforeUnmount, onMounted, ref } from 'vue';
import { useI18n } from 'vue-i18n';

import { ApiError, apiGet, apiPost } from '@/api/client';
import { AppButton, InlineAlert, StatusBadge } from '@/components/ui';

interface Mic {
  name: string;
  uuid: string;
  connected: boolean;
}

const { t } = useI18n();
const pin = ref('');
const name = ref('');
const busy = ref(false);
const message = ref('');
const tone = ref<'success' | 'danger'>('success');
const mics = ref<Mic[]>([]);

async function refresh(): Promise<void> {
  try {
    const response = await apiGet<{ named_mics?: Mic[] }>('/api/mic/list');
    if (Array.isArray(response.named_mics)) mics.value = response.named_mics;
  } catch {
    // Keep the last list; the next poll retries.
  }
}

async function pair(): Promise<void> {
  busy.value = true;
  message.value = '';
  try {
    const response = await apiPost<{ status?: boolean; name?: string }>('/api/mic/pin', {
      pin: pin.value.trim(),
      name: name.value.trim(),
    });
    if (response.status === true) {
      tone.value = 'success';
      message.value = t('mic.pair_success', { name: response.name });
      pin.value = '';
      name.value = '';
      await refresh();
    } else {
      tone.value = 'danger';
      message.value = t('mic.pair_failure');
    }
  } catch (cause) {
    tone.value = 'danger';
    message.value = cause instanceof ApiError ? t('mic.request_failed') : t('mic.pair_failure');
  } finally {
    busy.value = false;
  }
}

async function remove(mic: Mic): Promise<void> {
  if (!window.confirm(t('mic.remove_confirm', { name: mic.name }))) return;
  busy.value = true;
  try {
    await apiPost('/api/mic/remove', { uuid: mic.uuid });
  } catch {
    // The refreshed list shows whether the removal happened.
  } finally {
    await refresh();
    busy.value = false;
  }
}

// Polls so that the Connected badge follows Calliope.
let timer: ReturnType<typeof setInterval> | undefined;
onMounted(() => {
  void refresh();
  timer = setInterval(refresh, 5000);
});
onBeforeUnmount(() => clearInterval(timer));
</script>

<template>
  <section class="mic-panel vs-surface" aria-labelledby="mic-panel-title">
    <h2 id="mic-panel-title">{{ t('mic.title') }}</h2>
    <p>{{ t('mic.desc') }}</p>
    <form class="mic-panel__form" @submit.prevent="pair">
      <label>
        <span>{{ t('mic.pin') }}</span>
        <input v-model="pin" class="vs-input" inputmode="numeric" maxlength="4" autocomplete="one-time-code" />
      </label>
      <label>
        <span>{{ t('mic.name') }}</span>
        <input v-model="name" class="vs-input" maxlength="64" autocomplete="off" />
      </label>
      <AppButton
        type="submit"
        variant="primary"
        :label="t('mic.pair')"
        :busy="busy"
        :disabled="busy || !/^[0-9]{4}$/.test(pin.trim())"
      />
    </form>
    <InlineAlert v-if="message" :tone="tone" announce="polite">{{ message }}</InlineAlert>
    <p v-if="!mics.length" class="mic-panel__empty">{{ t('mic.none') }}</p>
    <ul v-else class="mic-panel__list">
      <li v-for="mic in mics" :key="mic.uuid">
        <span class="mic-panel__name">{{ mic.name }}</span>
        <StatusBadge v-if="mic.connected" :label="t('mic.connected')" tone="success" compact />
        <AppButton variant="danger" size="compact" :label="t('mic.remove')" :disabled="busy" @click="remove(mic)" />
      </li>
    </ul>
  </section>
</template>

<style scoped>
.mic-panel {
  display: grid;
  gap: 0.75rem;
  padding: 1rem;
}

.mic-panel__form {
  display: flex;
  flex-wrap: wrap;
  gap: 0.75rem;
  align-items: flex-end;
}

.mic-panel__form label {
  display: grid;
  gap: 0.25rem;
}

.mic-panel__list {
  display: grid;
  gap: 0.5rem;
  margin: 0;
  padding: 0;
  list-style: none;
}

.mic-panel__list li {
  display: flex;
  align-items: center;
  gap: 0.75rem;
}

.mic-panel__name {
  flex: 1;
  min-width: 0;
  overflow: hidden;
  text-overflow: ellipsis;
}

.mic-panel__empty {
  opacity: 0.7;
}
</style>
