import { signal } from '@preact/signals'

export type StatusFilter = 'all' | 'saved' | 'unsaved'
export type DeviceTypeFilter = 'all' | 'covers' | 'lights'
export type ActiveTab = 'devices' | 'packets' | 'hub'

interface UiState {
  activeTab: ActiveTab
  filters: {
    status: StatusFilter
    deviceType: DeviceTypeFilter
    rf: string
  }
}

export function createUiModel() {
  const state = signal<UiState>({
    activeTab: 'devices',
    filters: { status: 'all', deviceType: 'all', rf: '' },
  })

  return {
    state,

    setActiveTab(tab: ActiveTab) {
      state.value = { ...state.value, activeTab: tab }
    },

    setStatusFilter(status: StatusFilter) {
      state.value = { ...state.value, filters: { ...state.value.filters, status } }
    },

    setDeviceTypeFilter(deviceType: DeviceTypeFilter) {
      state.value = { ...state.value, filters: { ...state.value.filters, deviceType } }
    },

    setRfFilter(rf: string) {
      state.value = { ...state.value, filters: { ...state.value.filters, rf } }
    },
  }
}

export type UiModel = ReturnType<typeof createUiModel>
