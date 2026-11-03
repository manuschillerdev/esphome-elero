// Included after the DeviceRegistryTest fixture so these tests exercise the
// real registry, snapshot computation and MQTT adapter together.

class FakeMqttPublisher : public MqttPublisher {
 public:
    std::map<std::string, std::string> retained;
    std::map<std::string, std::function<void(const char *, const char *)>> subscriptions;
    bool connected{true};

    bool publish(const char *topic, const char *payload, bool retain) override {
        if (!connected) return false;
        if (retain) {
            if (payload[0] == '\0') retained.erase(topic);
            else retained[topic] = payload;
        }
        return true;
    }
    bool subscribe(const char *topic, std::function<void(const char *, const char *)> cb) override {
        subscriptions[topic] = std::move(cb);
        return true;
    }
    void unsubscribe(const char *topic) override { subscriptions.erase(topic); }
    bool is_connected() const override { return connected; }
};

class MqttCoverTest : public DeviceRegistryTest {
 protected:
    FakeMqttPublisher broker_;
    MqttAdapter mqtt_{broker_};
    static constexpr const char *STATE_TOPIC = "elero/cover/a831e5/state";
    static constexpr const char *POSITION_TOPIC = "elero/cover/a831e5/position";
    static constexpr const char *TILT_TOPIC = "elero/cover/a831e5/tilt_state";
    static constexpr const char *SET_TOPIC = "elero/cover/a831e5/set";
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
    broker_.subscriptions.at(SET_TOPIC)(SET_TOPIC, "open");
    EXPECT_EQ(dev->sender.queue_size(), 2u);
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
