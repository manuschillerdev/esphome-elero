import { readFileSync } from 'node:fs'
import Ajv, { type ValidateFunction } from 'ajv'
import { parse } from 'yaml'
import type {
  CmdPayload, RawPayload, UpsertDevicePayload, RemoveDevicePayload,
  UpsertGroupPayload, RemoveGroupPayload, GroupCmdPayload, RestartPayload,
  SetHubConfigPayload, ExportConfigPayload, ImportConfigPayload, LearnInStartPayload,
  LearnInConfirmUpPayload, LearnInConfirmDownPayload, LearnInCancelPayload,
  ConfigData, RfData, DeviceUpsertedData, CrudEventData, GroupConfig,
  GroupRemovedData, GroupStateData, StateChangedData, ErrorData, HubConfigEventData,
  ConfigSnapshot, ImportResult, LearnInStateData, LogData,
} from './src/generated/index'

export type ClientMessage = CmdPayload | RawPayload | UpsertDevicePayload | RemoveDevicePayload
  | UpsertGroupPayload | RemoveGroupPayload | GroupCmdPayload | RestartPayload
  | SetHubConfigPayload | ExportConfigPayload | ImportConfigPayload | LearnInStartPayload
  | LearnInConfirmUpPayload | LearnInConfirmDownPayload | LearnInCancelPayload

export interface Events {
  config: ConfigData
  rf: RfData
  device_upserted: DeviceUpsertedData
  device_removed: CrudEventData
  group_upserted: GroupConfig
  group_removed: GroupRemovedData
  group_state_changed: GroupStateData
  state_changed: StateChangedData
  error: ErrorData
  hub_config: HubConfigEventData
  config_snapshot: ConfigSnapshot
  import_result: ImportResult
  learn_in_state: LearnInStateData
  log: LogData
}

interface Schema {
  properties: { event?: { const: string }; type?: { const: string } }
}
interface Contract {
  components: { schemas: Record<string, Schema>; messages: Record<string, { payload: { $ref: string } }> }
}

// Use the same contract as generated frontend types; additions cannot silently go unvalidated.
const contract = parse(readFileSync(new URL('./asyncapi.yaml', import.meta.url), 'utf8')) as Contract
const ajv = new Ajv({ strict: false, allErrors: true, validateFormats: false })
ajv.addSchema({ ...contract, $id: 'elero-contract' }, 'elero-contract')
const clients = new Map<string, ValidateFunction>()
const events = new Map<string, ValidateFunction>()
for (const message of Object.values(contract.components.messages)) {
  const ref = message.payload.$ref
  const schema = contract.components.schemas[ref.split('/').at(-1)!]
  const validator = ajv.getSchema(`elero-contract${ref}`)!
  if (schema.properties.type) clients.set(schema.properties.type.const, validator)
  if (schema.properties.event) events.set(schema.properties.event.const, validator)
}

export function readCommand(text: string): ClientMessage {
  const value: unknown = JSON.parse(text)
  if (!value || typeof value !== 'object' || !('type' in value) || typeof value.type !== 'string') {
    throw new Error('Expected a command object with a type')
  }
  const validate = clients.get(value.type)
  if (!validate) throw new Error(`Unknown command: ${value.type}`)
  if (!validate(value)) throw new Error(ajv.errorsText(validate.errors))
  return value as ClientMessage
}

export function envelope<K extends keyof Events>(event: K, data: Events[K]): string {
  const value = { event, data }
  const validate = events.get(event)
  if (!validate || !validate(value)) {
    throw new Error(`Invalid mock ${event} event: ${ajv.errorsText(validate?.errors)}`)
  }
  return JSON.stringify(value)
}
