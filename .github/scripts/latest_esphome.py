"""Resolve the newest published, non-yanked stable ESPHome release from PyPI."""

import json
import os
from pathlib import Path
from urllib.request import urlopen

from packaging.version import InvalidVersion, Version


def latest_stable_esphome() -> str:
    with urlopen("https://pypi.org/pypi/esphome/json", timeout=30) as response:
        metadata = json.load(response)

    stable = []
    for value, files in metadata["releases"].items():
        try:
            version = Version(value)
        except InvalidVersion:
            continue
        if version.is_prerelease or version.is_devrelease:
            continue
        if any(not distribution.get("yanked", False) for distribution in files):
            stable.append(version)
    if not stable:
        raise RuntimeError("PyPI returned no published stable ESPHome releases")
    return str(max(stable))


if __name__ == "__main__":
    latest = latest_stable_esphome()
    print(f"Latest stable ESPHome: {latest}")
    if output_path := os.environ.get("GITHUB_OUTPUT"):
        with Path(output_path).open("a") as output:
            output.write(f"version={latest}\n")
    if summary_path := os.environ.get("GITHUB_STEP_SUMMARY"):
        with Path(summary_path).open("a") as summary:
            summary.write(f"Testing latest stable ESPHome **{latest}** with the existing CI checks.\n")
