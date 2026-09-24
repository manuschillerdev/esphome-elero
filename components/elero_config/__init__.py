"""Shared JSON configuration codec, loaded only by management adapters."""

import esphome.config_validation as cv

DEPENDENCIES = ["elero"]
AUTO_LOAD = ["json"]
CONFIG_SCHEMA = cv.Schema({})
