<template>
  <n-card class="clients-card" :segmented="{ content: true, footer: false }">
    <template #header>
      <div class="clients-section-heading">
        <span class="clients-section-icon">
          <i class="fas fa-microphone" />
        </span>
        <div class="min-w-0">
          <h2 class="text-lg font-medium">{{ t('mic.title') }}</h2>
          <p class="text-xs opacity-70 max-w-2xl mt-1">{{ t('mic.desc') }}</p>
        </div>
      </div>
    </template>
    <div class="space-y-4">
      <n-form
        class="grid grid-cols-1 gap-4 md:grid-cols-[minmax(8rem,12rem)_minmax(12rem,1fr)_auto] md:items-end"
        @submit.prevent="pair"
      >
        <n-form-item class="flex flex-col" :label="t('mic.pin')" label-placement="top">
          <n-input
            v-model:value="pin"
            maxlength="4"
            :input-props="{ inputmode: 'numeric', autocomplete: 'one-time-code' }"
          />
        </n-form-item>
        <n-form-item class="flex flex-col" :label="t('mic.name')" label-placement="top">
          <n-input v-model:value="name" maxlength="64" :input-props="{ autocomplete: 'off' }" />
        </n-form-item>
        <n-form-item class="flex flex-col md:items-end">
          <n-button
            type="primary"
            strong
            attr-type="submit"
            :disabled="busy || !/^[0-9]{4}$/.test(pin.trim())"
            :loading="busy"
          >
            {{ t('mic.pair') }}
          </n-button>
        </n-form-item>
      </n-form>
      <n-alert v-if="message" :type="messageType">{{ message }}</n-alert>
      <p v-if="!mics.length" class="text-sm opacity-70">{{ t('mic.none') }}</p>
      <ul v-else class="space-y-2">
        <li v-for="mic in mics" :key="mic.uuid" class="flex items-center justify-between gap-3">
          <span class="min-w-0 truncate">
            {{ mic.name }}
            <n-tag v-if="mic.connected" type="success" size="small" class="ml-2">
              {{ t('mic.connected') }}
            </n-tag>
          </span>
          <n-button size="small" type="error" secondary :disabled="busy" @click="remove(mic)">
            {{ t('mic.remove') }}
          </n-button>
        </li>
      </ul>
    </div>
  </n-card>
</template>

<script setup lang="ts">
// Calliope remote microphone pairing and list (Apollo fork).
import { onBeforeUnmount, onMounted, ref } from 'vue';
import { useI18n } from 'vue-i18n';
import { NAlert, NButton, NCard, NForm, NFormItem, NInput, NTag } from 'naive-ui';
import { http } from '@/http';

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
const messageType = ref<'success' | 'error'>('success');
const mics = ref<Mic[]>([]);

async function refresh(): Promise<void> {
  try {
    const r = await http.get('./api/mic/list', { validateStatus: () => true });
    if (r.status === 200 && Array.isArray(r.data?.named_mics)) {
      mics.value = r.data.named_mics;
    }
  } catch {
    // Keep the last list; the next poll retries.
  }
}

async function pair(): Promise<void> {
  busy.value = true;
  message.value = '';
  try {
    const r = await http.post(
      './api/mic/pin',
      { pin: pin.value.trim(), name: name.value.trim() },
      { validateStatus: () => true },
    );
    if (r.status === 200 && r.data?.status === true) {
      messageType.value = 'success';
      message.value = t('mic.pair_success', { name: r.data.name });
      pin.value = '';
      name.value = '';
      await refresh();
    } else {
      messageType.value = 'error';
      message.value = r.status === 200 ? t('mic.pair_failure') : t('mic.request_failed');
    }
  } catch {
    messageType.value = 'error';
    message.value = t('mic.request_failed');
  } finally {
    busy.value = false;
  }
}

async function remove(mic: Mic): Promise<void> {
  if (!window.confirm(t('mic.remove_confirm', { name: mic.name }))) return;
  busy.value = true;
  try {
    await http.post('./api/mic/remove', { uuid: mic.uuid }, { validateStatus: () => true });
    await refresh();
  } finally {
    busy.value = false;
  }
}

// Polls so that the Connected tag follows Calliope.
let timer: ReturnType<typeof setInterval> | undefined;
onMounted(() => {
  void refresh();
  timer = setInterval(refresh, 5000);
});
onBeforeUnmount(() => clearInterval(timer));
</script>
