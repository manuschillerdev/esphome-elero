import logging
from pathlib import Path

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components.elero import CONF_ELERO_ID, elero, elero_ns
from esphome.components.logger import request_log_listener
from esphome.const import CONF_ID, CONF_PORT

_LOGGER = logging.getLogger(__name__)

DEPENDENCIES = ["elero", "network", "logger"]
CODEOWNERS = ["@manuschillerdev"]

# Exported so the switch sub-platform can reference the web server class
CONF_ELERO_WEB_ID = "elero_web_id"
EleroWebServer = elero_ns.class_("EleroWebServer", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(EleroWebServer),
        cv.GenerateID(CONF_ELERO_ID): cv.use_id(elero),
        cv.Optional(CONF_PORT, default=80): cv.port,
    }
).extend(cv.COMPONENT_SCHEMA)

# Path to the pre-built single-file frontend
_FRONTEND_HTML = Path(__file__).parent / "frontend" / "app" / "dist" / "index.html"


def _generate_ui_header() -> None:
    """Read the built frontend HTML and generate elero_web_ui.h."""
    header_path = Path(__file__).parent / "elero_web_ui.h"
    html = _FRONTEND_HTML.read_text(encoding="utf-8")
    header_path.write_text(
        '#pragma once\n'
        '\n'
        '// Auto-generated during ESPHome codegen from frontend/app/dist/index.html\n'
        '// Do not edit manually - rebuild frontend with: cd frontend/app && pnpm build\n'
        '\n'
        'namespace esphome {\n'
        'namespace elero {\n'
        '\n'
        f'const char ELERO_WEB_UI_HTML[] PROGMEM = R"rawliteral({html})rawliteral";\n'
        '\n'
        '}  // namespace elero\n'
        '}  // namespace esphome\n',
        encoding="utf-8",
    )
    _LOGGER.info("Generated elero_web_ui.h (%.1f KB)", len(html) / 1024)


async def to_code(config):
    # Generate the UI header from the pre-built frontend HTML
    if _FRONTEND_HTML.exists():
        _generate_ui_header()
    else:
        _LOGGER.warning(
            "Frontend not built: %s not found. "
            "Run 'cd components/elero_web/frontend/app && pnpm build' first.",
            _FRONTEND_HTML,
        )

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    parent = await cg.get_variable(config[CONF_ELERO_ID])
    cg.add(var.set_elero_parent(parent))
    cg.add(var.set_port(config[CONF_PORT]))

    # Request log listener support for forwarding logs to WebSocket
    request_log_listener()
