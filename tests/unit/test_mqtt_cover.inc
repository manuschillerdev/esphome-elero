// Included after the DeviceRegistryTest fixture so these tests exercise the
// real registry, snapshot computation and MQTT adapter together.

class FakeMqttPublisher : public MqttPublisher {
 public:
    std::map<std::string, std::string> retained;
    std::map<std::string, std::vector<std::function<void(const char *, const char *)>>> subscriptions;
    bool connected{true};
    std::vector<std::string> cleared_topics;

    bool publish(const char *topic, const char *payload, bool retain) override {
        if (!connected) return false;
        if (retain) {
            if (payload[0] == '\0') { retained.erase(topic); cleared_topics.emplace_back(topic); }
            else retained[topic] = payload;
        }
        return true;
    }
    bool subscribe(const char *topic, std::function<void(const char *, const char *)> cb) override {
        // ESPHome appends callbacks even when a topic is already subscribed.
        subscriptions[topic].push_back(std::move(cb));
        return true;
    }
    void unsubscribe(const char *topic) override { subscriptions.erase(topic); }
    bool is_connected() const override { return connected; }
    void deliver(const char *topic, const char *payload) {
        for (const auto &cb : subscriptions.at(topic)) cb(topic, payload);
    }
};

class MqttCoverTest : public DeviceRegistryTest {
 protected:
    FakeMqttPublisher broker_;
    MqttAdapter mqtt_{broker_};
    static constexpr const char *STATE_TOPIC = "elero/cover/a831e5/state";
    static constexpr const char *POSITION_TOPIC = "elero/cover/a831e5/position";
    static constexpr const char *TILT_TOPIC = "elero/cover/a831e5/tilt_state";
    static constexpr const char *SET_TOPIC = "elero/cover/a831e5/set";
    static constexpr const char *SET_POSITION_TOPIC = "elero/cover/a831e5/set_position";
    static constexpr const char *SET_TILT_TOPIC = "elero/cover/a831e5/tilt";
    static constexpr const char *LIGHT_SET_TOPIC = "elero/light/c41a2b/set";
    static constexpr const char *DISCOVERY_TOPIC = "homeassistant/cover/test-gateway_cover_a831e5/config";

    void SetUp() override {
        DeviceRegistryTest::SetUp();
        mock_time_.advance(1000);
        registry_.init_preferences();
        registry_.add_adapter(&mqtt_);
        registry_.setup_adapters();
    }

    JsonDocument discovery() {
        JsonDocument doc;
        EXPECT_FALSE(deserializeJson(doc, broker_.retained.at(DISCOVERY_TOPIC)));
        return doc;
    }

    void report_closed() {
        registry_.on_rf_packet(make_status_pkt(0xA831E5, pkt::state::BOTTOM), mock_time_.millis());
    }
};

TEST_F(MqttCoverTest, UncalibratedCoverDoesNotAdvertisePositionOrTilt) {
    auto cfg = make_cover_config(0xA831E5);
    cfg.open_duration_ms = cfg.close_duration_ms = 0;
    registry_.upsert(cfg);
    const auto doc = discovery();
    EXPECT_TRUE(doc["position_topic"].isNull());
    EXPECT_TRUE(doc["set_position_topic"].isNull());
    EXPECT_TRUE(doc["tilt_command_topic"].isNull());
    EXPECT_FALSE(broker_.retained.contains(STATE_TOPIC));
    EXPECT_FALSE(broker_.retained.contains(POSITION_TOPIC));
}

TEST_F(MqttCoverTest, LightDiscoveryAdvertisesCanonicalSwitchingStateNames) {
    add_light();
    JsonDocument doc;
    ASSERT_FALSE(deserializeJson(doc, broker_.retained.at(
        "homeassistant/sensor/test-gateway_light_c41a2b_state/config")));
    std::vector<std::string> options;
    for (const auto value : doc["options"].as<JsonArrayConst>()) {
        options.emplace_back(value.as<const char *>());
    }
    EXPECT_NE(std::find(options.begin(), options.end(), "light_off"), options.end());
    EXPECT_NE(std::find(options.begin(), options.end(), "light_on"), options.end());
    EXPECT_EQ(std::find(options.begin(), options.end(), "off"), options.end());
    EXPECT_EQ(std::find(options.begin(), options.end(), "on"), options.end());
}

class MqttFirstStatusTest : public MqttCoverTest, public ::testing::WithParamInterface<uint8_t> {};

TEST_P(MqttFirstStatusTest, FirstStationaryReportPublishesCompleteState) {
    auto cfg = make_cover_config(0xA831E5);
    cfg.open_duration_ms = cfg.close_duration_ms = 0;
    cfg.supports_tilt = 1;
    registry_.upsert(cfg);
    EXPECT_FALSE(broker_.retained.contains(STATE_TOPIC));

    registry_.on_rf_packet(make_status_pkt(0xA831E5, GetParam()), mock_time_.millis());
    EXPECT_EQ(broker_.retained.at(STATE_TOPIC), "open");
    EXPECT_EQ(broker_.retained.at(TILT_TOPIC), "0");
    EXPECT_EQ(broker_.retained.at("elero/cover/a831e5/problem"), "OFF");
    EXPECT_TRUE(broker_.retained.contains("elero/cover/a831e5/attributes"));
    EXPECT_FALSE(broker_.retained.contains(POSITION_TOPIC));

    // Once the first snapshot is published, identical reports stay deduplicated.
    adapter_.clear();
    mock_time_.advance(100);
    registry_.on_rf_packet(make_status_pkt(0xA831E5, GetParam()), mock_time_.millis());
    EXPECT_TRUE(adapter_.last_changes.empty());
}

INSTANTIATE_TEST_SUITE_P(Stationary, MqttFirstStatusTest,
    ::testing::Values(pkt::state::TOP, pkt::state::STOPPED, pkt::state::INTERMEDIATE));

TEST_F(MqttCoverTest, ConfigurationEditsKeepOneCoverCommandHandler) {
    auto cfg = make_cover_config(0xA831E5);
    cfg.supports_tilt = 1;
    auto *dev = registry_.upsert(cfg);
    for (int i = 0; i < 3; ++i) {
        snprintf(cfg.name, NVS_NAME_MAX, "Renamed Cover %d", i);
        registry_.upsert(cfg);
    }
    for (const auto *topic : {SET_TOPIC, SET_POSITION_TOPIC, SET_TILT_TOPIC}) {
        EXPECT_EQ(broker_.subscriptions.at(topic).size(), 1u) << topic;
    }
    broker_.deliver(SET_TOPIC, "open");
    EXPECT_EQ(dev->sender.queue_size(), 2u);  // One move + one status check.
}

TEST_F(MqttCoverTest, ConfigurationEditsKeepOneLightCommandHandler) {
    auto cfg = make_light_config(0xC41A2B);
    auto *dev = registry_.upsert(cfg);
    for (int i = 0; i < 3; ++i) {
        snprintf(cfg.name, NVS_NAME_MAX, "Renamed Light %d", i);
        registry_.upsert(cfg);
    }
    EXPECT_EQ(broker_.subscriptions.at(LIGHT_SET_TOPIC).size(), 1u);
    broker_.deliver(LIGHT_SET_TOPIC, R"({"state":"ON"})");
    EXPECT_EQ(dev->sender.queue_size(), 1u);
}

TEST_F(MqttCoverTest, ReconnectKeepsOneCommandHandlerPerTopic) {
    auto *cover = add_cover();
    auto *light = add_light();
    for (int i = 0; i < 3; ++i) {
        broker_.connected = false;
        mqtt_.loop();
        broker_.connected = true;
        mqtt_.loop();
        mock_time_.advance(501);
        mqtt_.loop();
    }
    for (const auto *topic : {SET_TOPIC, SET_POSITION_TOPIC, SET_TILT_TOPIC, LIGHT_SET_TOPIC}) {
        EXPECT_EQ(broker_.subscriptions.at(topic).size(), 1u) << topic;
    }
    broker_.deliver(SET_TOPIC, "open");
    broker_.deliver(LIGHT_SET_TOPIC, R"({"state":"ON"})");
    EXPECT_EQ(cover->sender.queue_size(), 2u);
    EXPECT_EQ(light->sender.queue_size(), 1u);
}

TEST_F(MqttCoverTest, LateHomeAssistantSubscriberReceivesReportedState) {
    auto cfg = make_cover_config(0xA831E5);
    cfg.supports_tilt = 1;
    registry_.upsert(cfg);
    report_closed();
    EXPECT_EQ(broker_.retained.at(TILT_TOPIC), "0");
    broker_.retained.erase(TILT_TOPIC);
    registry_.force_republish_all();

    // These retained values are what a newly started HA receives, without any
    // gateway reconnect, additional RF report, or movement of the motor.
    EXPECT_EQ(broker_.retained.at(STATE_TOPIC), "closed");
    EXPECT_EQ(broker_.retained.at(POSITION_TOPIC), "0");
    EXPECT_EQ(broker_.retained.at(TILT_TOPIC), "0");
    const auto doc = discovery();
    EXPECT_EQ(doc["position_topic"].as<std::string>(), POSITION_TOPIC);
    EXPECT_EQ(doc["position_open"].as<int>(), 100);
    EXPECT_EQ(doc["position_closed"].as<int>(), 0);
}

TEST_F(MqttCoverTest, EnablingCapabilitiesReplaysUnchangedZeroValues) {
    auto cfg = make_cover_config(0xA831E5);
    cfg.open_duration_ms = cfg.close_duration_ms = 0;
    auto *dev = registry_.upsert(cfg);
    report_closed();
    adapter_.clear();
    cfg.open_duration_ms = 25000;
    cfg.close_duration_ms = 22000;
    cfg.supports_tilt = 1;
    registry_.upsert(cfg);

    EXPECT_EQ(broker_.retained.at(POSITION_TOPIC), "0");
    EXPECT_EQ(broker_.retained.at(TILT_TOPIC), "0");
    ASSERT_EQ(adapter_.last_changes.size(), 1u);
    EXPECT_EQ(adapter_.last_changes[0], state_change::ALL);
    EXPECT_EQ(dev->sender.queue_size(), 0u);
}

TEST_F(MqttCoverTest, CapabilityChangeWithoutRfDoesNotInventPosition) {
    auto cfg = make_cover_config(0xA831E5);
    registry_.upsert(cfg);
    cfg.supports_tilt = 1;
    registry_.upsert(cfg);
    registry_.force_republish_all();
    EXPECT_FALSE(broker_.retained.contains(STATE_TOPIC));
    EXPECT_FALSE(broker_.retained.contains(POSITION_TOPIC));
    EXPECT_FALSE(broker_.retained.contains(TILT_TOPIC));
}

TEST_F(MqttCoverTest, RemovingCapabilitiesClearsTheirRetainedValues) {
    auto cfg = make_cover_config(0xA831E5);
    cfg.supports_tilt = 1;
    registry_.upsert(cfg);
    report_closed();
    registry_.force_republish_all();
    ASSERT_TRUE(broker_.retained.contains(TILT_TOPIC));

    cfg.open_duration_ms = 0;
    cfg.supports_tilt = 0;
    registry_.upsert(cfg);
    EXPECT_FALSE(broker_.retained.contains(POSITION_TOPIC));
    EXPECT_FALSE(broker_.retained.contains(TILT_TOPIC));
    EXPECT_EQ(broker_.retained.at(STATE_TOPIC), "closed");
}

TEST_F(MqttCoverTest, DisablingAndReenablingRestoresStateAndCommandSubscription) {
    auto *dev = add_cover();
    report_closed();
    auto cfg = dev->config;
    cfg.set_enabled(false);
    registry_.upsert(cfg);
    EXPECT_FALSE(broker_.retained.contains(STATE_TOPIC));
    EXPECT_FALSE(broker_.retained.contains(POSITION_TOPIC));
    EXPECT_FALSE(broker_.subscriptions.contains(SET_TOPIC));

    cfg.set_enabled(true);
    registry_.upsert(cfg);
    EXPECT_EQ(broker_.retained.at(STATE_TOPIC), "closed");
    ASSERT_TRUE(broker_.subscriptions.contains(SET_TOPIC));
    EXPECT_EQ(broker_.subscriptions.at(SET_TOPIC).size(), 1u);
    broker_.deliver(SET_TOPIC, "open");
    EXPECT_EQ(dev->sender.queue_size(), 2u);
}

TEST_F(MqttCoverTest, DisablingAndReenablingLightRestoresOneCommandHandler) {
    auto *dev = add_light();
    auto cfg = dev->config;
    for (int i = 0; i < 3; ++i) {
        cfg.set_enabled(false);
        registry_.upsert(cfg);
        EXPECT_FALSE(broker_.subscriptions.contains(LIGHT_SET_TOPIC));
        cfg.set_enabled(true);
        registry_.upsert(cfg);
    }
    EXPECT_EQ(broker_.subscriptions.at(LIGHT_SET_TOPIC).size(), 1u);
    broker_.deliver(LIGHT_SET_TOPIC, R"({"state":"ON"})");
    EXPECT_EQ(dev->sender.queue_size(), 1u);
}

TEST_F(MqttCoverTest, RemovingCoverClearsAllRetainedState) {
    add_cover();
    report_closed();
    registry_.remove(0xA831E5, DeviceType::COVER);
    for (const auto &[topic, payload] : broker_.retained) {
        EXPECT_FALSE(topic.starts_with("elero/cover/a831e5/")) << topic;
    }
    EXPECT_FALSE(broker_.retained.contains(DISCOVERY_TOPIC));
}

TEST_F(MqttCoverTest, BootDiscardsOldStateUntilMotorReports) {
    broker_.retained[STATE_TOPIC] = "open";
    broker_.retained[POSITION_TOPIC] = "100";
    broker_.retained[TILT_TOPIC] = "100";
    add_cover();
    EXPECT_FALSE(broker_.retained.contains(STATE_TOPIC));
    EXPECT_FALSE(broker_.retained.contains(POSITION_TOPIC));
    EXPECT_FALSE(broker_.retained.contains(TILT_TOPIC));
    report_closed();
    EXPECT_EQ(broker_.retained.at(STATE_TOPIC), "closed");
}

TEST_F(MqttCoverTest, StartupWhileDisconnectedClearsOldStateOnReconnect) {
    broker_.connected = false;
    mqtt_.loop();
    broker_.retained[STATE_TOPIC] = "open";
    broker_.retained[POSITION_TOPIC] = "100";
    add_cover();

    broker_.connected = true;
    mqtt_.loop();
    mock_time_.advance(501);
    mqtt_.loop();
    EXPECT_FALSE(broker_.retained.contains(STATE_TOPIC));
    EXPECT_FALSE(broker_.retained.contains(POSITION_TOPIC));
    EXPECT_TRUE(broker_.retained.contains(DISCOVERY_TOPIC));
}

// Explicit buttons share the existing cover command topic and registry path.
TEST_F(MqttCoverTest, DiscoversFourStatelessActionsOnTheCoverDevice) {
    add_cover();
    const std::pair<const char *, const char *> expected[] = {
        {"step_up", "Short press up"}, {"step_down", "Short press down"},
        {"full_up", "Full up"}, {"full_down", "Full down"},
    };
    for (const auto &[action, name] : expected) {
        const auto id = std::string("test-gateway_cover_a831e5_") + action;
        const auto topic = "homeassistant/button/" + id + "/config";
        ASSERT_TRUE(broker_.retained.contains(topic));
        JsonDocument button;
        ASSERT_FALSE(deserializeJson(button, broker_.retained.at(topic)));
        EXPECT_EQ(button["name"].as<std::string>(), name);
        EXPECT_TRUE(button["has_entity_name"].as<bool>());
        EXPECT_EQ(button["unique_id"].as<std::string>(), id);
        EXPECT_EQ(button["command_topic"].as<std::string>(), SET_TOPIC);
        EXPECT_EQ(button["payload_press"].as<std::string>(), action);
        EXPECT_FALSE(button["retain"].as<bool>());
        EXPECT_TRUE(button["state_topic"].isNull());
        EXPECT_EQ(button["availability_topic"].as<std::string>(), "elero/status");
        EXPECT_EQ(button["device"]["identifiers"][0].as<std::string>(), "test-gateway_cover_a831e5");
        EXPECT_EQ(button["device"]["via_device"].as<std::string>(), "test-gateway");
    }
    EXPECT_EQ(broker_.subscriptions.at(SET_TOPIC).size(), 1u);
    add_light();
    for (const auto &[topic, payload] : broker_.retained) {
        if (topic.starts_with("homeassistant/button/")) EXPECT_NE(topic.find("_cover_"), std::string::npos);
    }
}

TEST_F(MqttCoverTest, NativeAndMqttButtonsUseIdenticalRegistryTransactions) {
    auto *dev = add_cover();
    for (const auto *action : {"step_up", "step_down", "full_up", "full_down"}) {
        broker_.deliver(SET_TOPIC, action);
        const auto mqtt = drain_commands(*dev);
        EspCoverActionButton button(registry_, dev->config.dst_address, action);
        button.press();
        const auto native = drain_commands(*dev);
        ASSERT_EQ(native.size(), mqtt.size()) << action;
        ASSERT_FALSE(native.empty());
        for (size_t i = 0; i < mqtt.size(); ++i) {
            EXPECT_EQ(native[i].type, mqtt[i].type);
            EXPECT_EQ(native[i].type2, mqtt[i].type2);
            EXPECT_EQ(native[i].hop, mqtt[i].hop);
            EXPECT_EQ(native[i].payload[4], mqtt[i].payload[4]);
            EXPECT_EQ(native[i].src_addr, mqtt[i].src_addr);
            EXPECT_EQ(native[i].dst_addr, mqtt[i].dst_addr);
            EXPECT_EQ(native[i].channel, mqtt[i].channel);
        }
    }
}

TEST_F(MqttCoverTest, ButtonIdentitySurvivesRenameAndDiscoveryFollowsLifecycle) {
    auto cfg = make_cover_config(0xA831E5);
    registry_.upsert(cfg);
    std::vector<std::string> topics;
    for (const auto &[topic, payload] : broker_.retained) {
        if (topic.starts_with("homeassistant/button/")) topics.push_back(topic);
    }
    ASSERT_EQ(topics.size(), 4u);
    cfg.set_name("New cover name");
    registry_.upsert(cfg);
    for (const auto &topic : topics) {
        JsonDocument button;
        ASSERT_FALSE(deserializeJson(button, broker_.retained.at(topic)));
        EXPECT_EQ(button["device"]["name"].as<std::string>(), "New cover name");
    }
    cfg.set_enabled(false);
    registry_.upsert(cfg);
    for (const auto &topic : topics) EXPECT_FALSE(broker_.retained.contains(topic));
    EXPECT_FALSE(broker_.subscriptions.contains(SET_TOPIC));
    cfg.set_enabled(true);
    registry_.upsert(cfg);
    for (const auto &topic : topics) EXPECT_TRUE(broker_.retained.contains(topic));
    ASSERT_TRUE(registry_.remove(cfg.dst_address, DeviceType::COVER));
    for (const auto &topic : topics) EXPECT_FALSE(broker_.retained.contains(topic));
    EXPECT_FALSE(broker_.subscriptions.contains(SET_TOPIC));
}

TEST_F(MqttCoverTest, CleanupRemovesOnlyStaleOwnedButtonsAndReconnectKeepsOneHandler) {
    add_cover();
    const std::string active = "homeassistant/button/test-gateway_cover_a831e5_full_up/config";
    const std::string stale = "homeassistant/button/test-gateway_cover_deadbe_full_up/config";
    const std::string foreign = "homeassistant/button/other-gateway_cover_deadbe_full_up/config";
    broker_.retained[stale] = R"({"device":{"identifiers":["test-gateway_cover_deadbe"]}})";
    broker_.retained[foreign] = R"({"device":{"identifiers":["other-gateway_cover_deadbe"]}})";
    const auto &callbacks = broker_.subscriptions.at("homeassistant/button/+/config");
    for (const auto &topic : {active, stale, foreign}) {
        for (const auto &callback : callbacks) callback(topic.c_str(), broker_.retained.at(topic).c_str());
    }
    mock_time_.advance(501);
    mqtt_.loop();
    EXPECT_TRUE(broker_.retained.contains(active));
    EXPECT_FALSE(broker_.retained.contains(stale));
    EXPECT_TRUE(broker_.retained.contains(foreign));
    EXPECT_EQ(std::count(broker_.cleared_topics.begin(), broker_.cleared_topics.end(), active), 0);
    EXPECT_EQ(std::count(broker_.cleared_topics.begin(), broker_.cleared_topics.end(), foreign), 0);
    for (int i = 0; i < 2; ++i) {
        broker_.connected = false;
        mqtt_.loop();
        broker_.connected = true;
        mqtt_.loop();
        mock_time_.advance(501);
        mqtt_.loop();
    }
    EXPECT_TRUE(broker_.retained.contains(active));
    EXPECT_EQ(broker_.subscriptions.at(SET_TOPIC).size(), 1u);
}

TEST_F(DeviceRegistryTest, NativeButtonRejectsDisabledRemovedAndReusedSlots) {
    auto *original = add_cover();
    EspCoverActionButton button(registry_, original->config.dst_address, "full_up");
    auto config = original->config;
    config.set_enabled(false);
    registry_.upsert(config);
    button.press();
    EXPECT_EQ(original->sender.queue_size(), 0u);
    EXPECT_FALSE(registry_.command_cover_action(*original, "full_up"));
    config.set_enabled(true);
    registry_.upsert(config);
    button.press();
    EXPECT_FALSE(drain_commands(*original).empty());
    ASSERT_TRUE(registry_.remove(config.dst_address, DeviceType::COVER));
    auto *replacement = add_cover(0xA831E6);
    ASSERT_EQ(replacement, original);  // The slot is reused, but its identity is different.
    button.press();
    EXPECT_EQ(replacement->sender.queue_size(), 0u);
    EXPECT_FALSE(registry_.command_cover_action(config.dst_address, "full_up"));
}

TEST_F(DeviceRegistryTest, NativeButtonReadsCurrentAddressConfigurationFromRegistry) {
    auto *dev = add_cover();
    EspCoverActionButton button(registry_, dev->config.dst_address, "full_up");
    auto config = dev->config;
    config.src_address = 0x123456;
    config.channel = 8;
    config.set_name("Renamed");
    registry_.upsert(config);
    button.press();
    const auto commands = drain_commands(*dev);
    ASSERT_FALSE(commands.empty());
    EXPECT_EQ(commands.front().src_addr, 0x123456u);
    EXPECT_EQ(commands.front().channel, 8);
    EXPECT_EQ(commands.front().payload[4], 0x21);
    EXPECT_EQ(commands.front().type, 0x6a);
}
