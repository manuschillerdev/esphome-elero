#include <gtest/gtest.h>
#include "elero_web/web_json.h"
#include "elero_web/web_raw_tx.h"

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

namespace {
struct RawTxSink {
  esphome::elero::packet::TxParams frame{};
  bool accepts{true};
  bool send_raw_command(uint32_t dst, uint32_t src, uint8_t channel, uint8_t command,
                        uint8_t first, uint8_t second, uint8_t type, uint8_t type2, uint8_t hop) {
    frame = {.dst_addr = dst, .src_addr = src, .channel = channel, .type = type, .type2 = type2,
             .hop = hop, .command = command, .payload_1 = first, .payload_2 = second};
    return accepts;
  }
};
}  // namespace

TEST(WebJson, RawRequestSubmitsEverySuppliedFieldToRadioQueue) {
  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, R"({"type":"raw","src_address":"0xABCDEF","dst_address":"0x123456",
      "channel":7,"command":"0x10","msg_type":"0x69","type2":"0x12","hop":"0x33",
      "payload_1":"0x56","payload_2":"0x78"})"));
  RawTxSink sink;
  auto root = doc.as<JsonObject>();
  ASSERT_EQ(esphome::elero::submit_raw_tx(root, sink), nullptr);
  EXPECT_EQ(sink.frame.dst_addr, 0x123456u);
  EXPECT_EQ(sink.frame.src_addr, 0xABCDEFu);
  EXPECT_EQ(sink.frame.channel, 7);
  EXPECT_EQ(sink.frame.command, 0x10);
  EXPECT_EQ(sink.frame.type, 0x69);
  EXPECT_EQ(sink.frame.type2, 0x12);
  EXPECT_EQ(sink.frame.hop, 0x33);
  EXPECT_EQ(sink.frame.payload_1, 0x56);
  EXPECT_EQ(sink.frame.payload_2, 0x78);
  sink.accepts = false;
  EXPECT_NE(esphome::elero::submit_raw_tx(root, sink), nullptr);
}
