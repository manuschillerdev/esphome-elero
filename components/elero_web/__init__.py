"""
Elero Web Server library component.

This is a library component - it has no CONFIG_SCHEMA and is not directly
configurable by users. It provides the C++ EleroWebServer implementation.

Configuration is done via the `elero:` component's `web:` sub-object:

    elero:
      cs_pin: GPIO5
      gdo0_pin: GPIO26
      web:
        port: 80

The main elero component AUTO_LOADs this when web: is configured.

The switch sub-platform (switch.elero_web) can be used to enable/disable
the web UI at runtime.
"""

import esphome.codegen as cg

CODEOWNERS = ["@manuschillerdev"]

# No CONFIG_SCHEMA - this is a library, not a user-configurable component

# Re-export for sub-platforms (switch)
elero_ns = cg.esphome_ns.namespace("elero")
EleroWebServer = elero_ns.class_("EleroWebServer", cg.Component)

# Config key for switch sub-platform to reference the web server
CONF_ELERO_WEB_ID = "elero_web_id"
