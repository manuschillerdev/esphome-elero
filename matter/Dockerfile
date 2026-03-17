FROM espressif/esp-matter:latest

# Install uv
COPY --from=ghcr.io/astral-sh/uv:latest /uv /usr/local/bin/uv

# Use a container-internal venv (not the host-mounted .venv)
ENV UV_PROJECT_ENVIRONMENT=/opt/elero-venv

# Install project Python deps (ESPHome, PlatformIO, etc.)
WORKDIR /project
COPY pyproject.toml uv.lock ./
RUN uv venv --seed /opt/elero-venv \
    && uv sync --frozen --no-install-project

# Entry point: source IDF + Matter exports, then run the command.
# The full project is mounted at /project at runtime.
ENTRYPOINT ["/bin/bash", "-c", ". /opt/espressif/esp-idf/export.sh && . /opt/espressif/esp-matter/export.sh && exec \"$@\"", "--"]
CMD ["uv", "run", "esphome", "compile", "tests/test.esp32-matter.yaml"]
