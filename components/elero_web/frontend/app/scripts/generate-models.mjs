import { parse } from 'yaml'
import { spawnSync } from 'node:child_process'
import { readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs'

const output = new URL('../src/generated/', import.meta.url)
rmSync(output, { recursive: true, force: true })
const result = spawnSync('pnpm', [
  'exec', 'asyncapi', 'generate', 'models', 'typescript', 'asyncapi.yaml',
  '-o', 'src/generated', '--tsModelType', 'interface', '--tsEnumType', 'union',
  '--tsExportType', 'named', '--tsIncludeComments', '--tsRawPropertyNames',
], { stdio: 'inherit' })
if (result.error) throw result.error
if (result.status !== 0) process.exit(result.status ?? 1)

const models = readdirSync(output).filter(name => name.endsWith('.ts') && name !== 'index.ts').sort()
for (const name of models) {
  const path = new URL(name, output)
  // AsyncAPI's interface/union generator emits value exports for type declarations.
  // TypeScript's isolatedModules requires these to be explicitly type-only.
  writeFileSync(path, readFileSync(path, 'utf8').replace(/^export \{/gm, 'export type {'))
}
// Keep the byte-only compatibility path and mock gateway on the WebSocket contract.
const contract = parse(readFileSync(new URL('../asyncapi.yaml', import.meta.url), 'utf8'))
const names = contract.components.schemas.RfStateName.enum
writeFileSync(new URL('rf-status-names.ts', output),
  `import type { RfStateName } from './RfStateName'

export const rfStatusNames: readonly RfStateName[] = ${JSON.stringify(names)}
`)
const firmwareHeader = new URL('../../../../elero/elero_status_names.h', import.meta.url)
const checks = names.map((name, byte) =>
  `static_assert(packet::state::${name.toUpperCase()} == 0x${byte.toString(16).padStart(2, '0')});`)
writeFileSync(firmwareHeader, `// Generated from RfStateName in asyncapi.yaml by pnpm generate:types.
#pragma once

#include <array>
#include "elero_packet.h"

namespace esphome::elero {
inline constexpr std::array ELERO_STATUS_NAMES{
${names.map(name => `    "${name}",`).join('\n')}
};

${checks.join('\n')}
}  // namespace esphome::elero
`)
writeFileSync(new URL('index.ts', output), models.map(name => {
  const type = name.slice(0, -3)
  return `export type { ${type} } from './${type}'\n`
}).join(''))
