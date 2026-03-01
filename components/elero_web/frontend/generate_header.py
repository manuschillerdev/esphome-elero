#!/usr/bin/env python3
"""Generate elero_web_ui.h from built frontend.

Run from the frontend directory:
    pnpm build && python generate_header.py

Or from anywhere:
    cd components/elero_web/frontend/app && pnpm build && cd .. && python generate_header.py
"""
from pathlib import Path

here = Path(__file__).parent
html_path = here / "app" / "dist" / "index.html"
header_path = here.parent / "elero_web_ui.h"

if not html_path.exists():
    print(f"Error: {html_path} not found. Run 'pnpm build' in app/ first.")
    exit(1)

html = html_path.read_text(encoding="utf-8")

# Use raw string literal to avoid escaping issues
header = f'''#pragma once

// Auto-generated from frontend/app/dist/index.html
// Do not edit manually - run: cd frontend/app && pnpm build && cd .. && python generate_header.py

namespace esphome {{
namespace elero {{

const char ELERO_WEB_UI_HTML[] PROGMEM = R"rawliteral({html})rawliteral";

}}  // namespace elero
}}  // namespace esphome
'''

header_path.write_text(header, encoding="utf-8")
print(f"Generated {header_path.name} ({len(html)} bytes HTML)")
