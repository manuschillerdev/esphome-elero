#include <gtest/gtest.h>
#include "elero_web/web_json.h"

using esphome::elero::build_web_json;

TEST(WebJson, FullDeviceListAndEnvelopeExceedEspHomeSerializationCap) {
  const std::string name = "Cover \"with quotes\" \\ ";
  const auto config = build_web_json([&](JsonObject root) {
    auto devices = root["blinds"].to<JsonArray>();
    for (int i = 0; i < 48; ++i) {
      auto dev = devices.add<JsonObject>();
      dev["name"] = name;
      dev["address"] = i + 1;
      dev["command_up"] = "0x21";
      dev["command_down"] = "0x44";
      dev["command_stop"] = "0x10";
      dev["command_check"] = "0x00";
      dev["command_tilt"] = "0x24";
    }
    auto groups = root["groups"].to<JsonArray>();
    auto group = groups.add<JsonObject>();
    group["id"] = "last-group";
    group["device_ids"].to<JsonArray>().add(48);
  });
  ASSERT_GT(config.size(), 5120u);
  const auto message = build_web_json([&](JsonObject root) {
    root["event"] = "config";
    root["data"] = serialized(config);
  });
  JsonDocument parsed;
  ASSERT_FALSE(deserializeJson(parsed, message));
  EXPECT_EQ(parsed["event"].as<std::string>(), "config");
  EXPECT_EQ(parsed["data"]["blinds"].size(), 48u);
  EXPECT_EQ(parsed["data"]["blinds"][47]["name"].as<std::string>(), name);
  EXPECT_EQ(parsed["data"]["blinds"][47]["command_down"].as<std::string>(), "0x44");
  EXPECT_EQ(parsed["data"]["groups"][0]["device_ids"][0].as<int>(), 48);
}

TEST(WebJson, EmptyDocumentRemainsValid) {
  EXPECT_EQ(build_web_json([](JsonObject) {}), "{}");
}
