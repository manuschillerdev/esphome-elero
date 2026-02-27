#!/usr/bin/env python3
"""Generate elero_web_ui.h from index.html.

Run from the frontend directory:
    python generate_header.py

Or from anywhere:
    python components/elero_web/frontend/generate_header.py
"""
from pathlib import Path

here = Path(__file__).parent
html_path = here / "index.html"
header_path = here.parent / "elero_web_ui.h"

html = html_path.read_text(encoding="utf-8")

# Use raw string literal to avoid escaping issues
header = f'''#pragma once

// Auto-generated from frontend/index.html
// Do not edit manually - run: python frontend/generate_header.py

namespace esphome {{
namespace elero {{

const char ELERO_WEB_UI_HTML[] PROGMEM = R"rawliteral({html})rawliteral";

}}  // namespace elero
}}  // namespace esphome
'''

header_path.write_text(header, encoding="utf-8")
print(f"Generated {header_path.name} ({len(html)} bytes HTML)")
