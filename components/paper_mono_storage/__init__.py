"""Optional PaperMono microSD hardware and asynchronous file access."""

import esphome.config_validation as cv
from esphome.components import esp32
from esphome.core import CoroPriority, coroutine_with_priority

DEPENDENCIES = ["paper_mono", "esp32"]
CONFIG_SCHEMA = cv.Schema({})


@coroutine_with_priority(CoroPriority.BUS)
async def to_code(config):
    esp32.require_fatfs()
    esp32.require_vfs_dir()
    esp32.include_builtin_idf_component("fatfs")
    esp32.include_builtin_idf_component("wear_levelling")
    esp32.add_idf_sdkconfig_option("CONFIG_FATFS_LFN_HEAP", True)
