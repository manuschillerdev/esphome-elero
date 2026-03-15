import { createHubModel } from './hub'
import { createUiModel } from './ui'
import { createDeviceModel } from './devices'

export function createAppStore() {
  const hub = createHubModel()
  const ui = createUiModel()
  const devices = createDeviceModel(ui, hub)

  return { hub, ui, devices }
}

export type AppStore = ReturnType<typeof createAppStore>
