#!/usr/bin/env bash
set -euo pipefail

# Normal CI uses the locked environment. Compatibility runs layer exactly the
# resolved ESPHome release over it, keeping pyproject.toml and uv.lock unchanged.
uv_args=(--locked)
if [[ -n "${ESPHOME_VERSION:-}" ]]; then
  uv_args+=(--with "esphome==${ESPHOME_VERSION}")
fi
exec uv run "${uv_args[@]}" "$@"
