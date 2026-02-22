/**
 * generate_header.mjs
 * Reads dist/index.html produced by Vite and writes ../elero_web_ui.h
 * which embeds the UI as a C raw-string literal for the ESP32 firmware.
 */
import fs from 'fs'
import path from 'path'
import { fileURLToPath } from 'url'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const distDir   = path.resolve(__dirname, '..', 'dist')
const outFile   = path.resolve(__dirname, '..', '..', 'elero_web_ui.h')
const htmlFile  = path.join(distDir, 'index.html')

if (!fs.existsSync(htmlFile)) {
  console.error('ERROR: dist/index.html not found — run vite build first')
  process.exit(1)
}

const html = fs.readFileSync(htmlFile, 'utf8')

// We use R"rawliteral(...)rawliteral" so no escaping is needed inside the HTML.
// Just verify there is no rawliteral delimiter in the content (extremely unlikely).
if (html.includes(')rawliteral"')) {
  console.error('ERROR: HTML contains the C raw-string end delimiter — cannot embed safely')
  process.exit(1)
}

const header = `#pragma once

#ifdef __AVR__
#include <pgmspace.h>
#elif !defined(PROGMEM)
#define PROGMEM
#endif

// AUTO-GENERATED FILE — do not edit by hand.
// To rebuild: cd components/elero_web/frontend && npm run build

namespace esphome {
namespace elero {

const char ELERO_WEB_UI_HTML[] PROGMEM = R"rawliteral(${html})rawliteral";

}  // namespace elero
}  // namespace esphome
`

fs.writeFileSync(outFile, header, 'utf8')
console.log(`Written ${Buffer.byteLength(html)} bytes of HTML → ${outFile}`)
