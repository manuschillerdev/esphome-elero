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
writeFileSync(new URL('index.ts', output), models.map(name => {
  const type = name.slice(0, -3)
  return `export type { ${type} } from './${type}'\n`
}).join(''))
