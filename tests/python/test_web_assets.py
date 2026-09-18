"""A missing/partial release asset must fail before C++ compilation."""

import importlib
import sys
import urllib.error
import urllib.request
from pathlib import Path

import elero
import pytest

from esphome.config_validation import Invalid


@pytest.fixture
def web_assets(monkeypatch, tmp_path):
    # Match ESPHome's external-component import namespace.
    monkeypatch.setitem(sys.modules, "esphome.components.elero", elero)
    web = importlib.import_module("elero_web")
    monkeypatch.setattr(web, "_COMPONENT_DIR", tmp_path)
    monkeypatch.setattr(web, "_UI_HEADER", tmp_path / "elero_web_ui.h")
    return web


def test_existing_local_build_does_not_download(web_assets, monkeypatch):
    web_assets._UI_HEADER.write_text("local build")

    def unexpected_download(*args):
        pytest.fail("Local frontend should not require a release asset")

    monkeypatch.setattr(urllib.request, "urlretrieve", unexpected_download)
    web_assets._ensure_ui_header()
    assert web_assets._UI_HEADER.read_text() == "local build"


def test_failed_download_is_not_cached_and_can_be_retried(web_assets, monkeypatch):
    def interrupted_download(url, target):
        Path(target).write_text("partial")
        raise urllib.error.URLError("connection interrupted")

    monkeypatch.setattr(urllib.request, "urlretrieve", interrupted_download)
    with pytest.raises(Invalid, match="pnpm install --frozen-lockfile && pnpm build"):
        web_assets._ensure_ui_header()
    assert not web_assets._UI_HEADER.exists()
    assert list(web_assets._COMPONENT_DIR.iterdir()) == []

    def successful_download(url, target):
        assert f"/v{elero.ELERO_VERSION}/elero_web_ui.h" in url
        Path(target).write_text("complete header")

    monkeypatch.setattr(urllib.request, "urlretrieve", successful_download)
    web_assets._ensure_ui_header()
    assert web_assets._UI_HEADER.read_text() == "complete header"


def test_empty_download_fails(web_assets, monkeypatch):
    monkeypatch.setattr(urllib.request, "urlretrieve", lambda url, target: Path(target).touch())
    with pytest.raises(Invalid, match="empty"):
        web_assets._ensure_ui_header()
    assert not web_assets._UI_HEADER.exists()
