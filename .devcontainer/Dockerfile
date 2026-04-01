FROM debian:bookworm-slim

SHELL ["/bin/bash", "-c"]
ENV DEBIAN_FRONTEND=noninteractive

# System deps
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        curl \
        git \
        build-essential \
        pkg-config \
        default-jdk-headless \
    && rm -rf /var/lib/apt/lists/*

# Install mise (pinned version, multi-arch)
ARG MISE_VERSION=2026.3.18
RUN ARCH=$(dpkg --print-architecture) \
    && curl -fsSL "https://github.com/jdx/mise/releases/download/v${MISE_VERSION}/mise-v${MISE_VERSION}-linux-${ARCH}" \
       -o /usr/local/bin/mise \
    && chmod +x /usr/local/bin/mise

# Mise shims on PATH (POSIX-safe, works with dash/sh and bash)
RUN echo 'export PATH="$HOME/.local/share/mise/shims:$HOME/.local/bin:$PATH"' > /etc/profile.d/00-mise.sh

# Mise activation for interactive bash sessions only
RUN echo 'eval "$(mise activate bash)"' >> /etc/skel/.bashrc

# Pre-create volume mount points as uid 1000 (vscode user from common-utils).
# Docker named volumes inherit this ownership on first mount — no sudo needed.
RUN mkdir -p \
      /home/vscode/.local/share/mise \
      /home/vscode/.local/state/mise \
      /home/vscode/.cache/uv \
      /home/vscode/.local/share/pnpm/store \
      /home/vscode/.cache/pre-commit \
    && chown -R 1000:1000 /home/vscode
