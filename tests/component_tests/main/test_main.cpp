#include "HMS_Messaging.h"
#include "unity.h"
#include "unity_test_runner.h"
#include <array>
#include <cstring>

namespace {
class TestTransport final : public HMS_Interface {
public:
    TestTransport(const std::array<uint8_t, 6>& mac, size_t maxPayload)
        : mac_(mac), configuredMaxPayload_(maxPayload) {}

    HMS_Protocol protocol() const noexcept override { return HMS_Protocol::NONE; }
    bool begin() override { maxPayload_ = configuredMaxPayload_; return true; }
    void end() override { maxPayload_ = 0; }
    size_t maxPayload() const noexcept override { return maxPayload_; }
    bool broadcast(const uint8_t*, size_t) override { return false; }

    bool send(const uint8_t* peerAddr, const uint8_t* data, size_t len) override {
        if (!peer_ || !peerAddr || !data || len == 0 || len > maxPayload_ ||
            std::memcmp(peerAddr, peer_->mac_.data(), mac_.size()) != 0) {
            return false;
        }
        ++sendCount_;
        std::memcpy(lastFrame_.data(), data, len);
        lastFrameLength_ = len;
        peer_->dispatchReceive(mac_.data(), data, len, 0);
        return true;
    }

    void connect(TestTransport& peer) { peer_ = &peer; }
    void inject(const uint8_t* source, const uint8_t* data, size_t len) {
        dispatchReceive(source, data, len, 0);
    }

    const std::array<uint8_t, 6>& mac() const { return mac_; }
    const std::array<uint8_t, 250>& lastFrame() const { return lastFrame_; }
    size_t lastFrameLength() const { return lastFrameLength_; }
    size_t sendCount() const { return sendCount_; }

private:
    std::array<uint8_t, 6> mac_;
    size_t configuredMaxPayload_;
    size_t maxPayload_ = 0;
    TestTransport* peer_ = nullptr;
    std::array<uint8_t, 250> lastFrame_{};
    size_t lastFrameLength_ = 0;
    size_t sendCount_ = 0;
};

constexpr std::array<uint8_t, 6> NODE_A_MAC = {0x02, 0, 0, 0, 0, 1};
constexpr std::array<uint8_t, 6> NODE_B_MAC = {0x02, 0, 0, 0, 0, 2};
}

TEST_CASE("messaging uses the active transport payload limit", "[messaging]") {
    TestTransport radioA(NODE_A_MAC, 250);
    TestTransport radioB(NODE_B_MAC, 250);
    radioA.connect(radioB);
    radioB.connect(radioA);
    HMS_Messaging nodeA(radioA);
    HMS_Messaging nodeB(radioB);

    TEST_ASSERT_EQUAL_UINT32(0, nodeA.maxApplicationPayload());
    const uint8_t unavailablePayload = 0;
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(HMS_ReliableResult::NOT_STARTED),
                             static_cast<uint32_t>(nodeA.sendReliable(NODE_B_MAC.data(), &unavailablePayload, 1, 20, 0)));
    TEST_ASSERT_TRUE(radioA.begin());
    TEST_ASSERT_TRUE(radioB.begin());
    TEST_ASSERT_EQUAL_UINT32(232, nodeA.maxApplicationPayload());

    std::array<uint8_t, 232> maximumPayload{};
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(HMS_ReliableResult::OK),
                             static_cast<uint32_t>(nodeA.sendReliable(NODE_B_MAC.data(), maximumPayload.data(), maximumPayload.size(), 20, 0)));
    TEST_ASSERT_EQUAL_UINT8(2, radioA.lastFrame()[2]);
    TEST_ASSERT_EQUAL_UINT32(250, radioA.lastFrameLength());

    std::array<uint8_t, 233> oversizedPayload{};
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(HMS_ReliableResult::INVALID_ARGUMENT),
                             static_cast<uint32_t>(nodeA.sendReliable(NODE_B_MAC.data(), oversizedPayload.data(), oversizedPayload.size(), 20, 0)));
}

TEST_CASE("messaging ACKs duplicates and distinguishes sender sessions", "[messaging]") {
    TestTransport radioA(NODE_A_MAC, 250);
    TestTransport radioB(NODE_B_MAC, 250);
    radioA.connect(radioB);
    radioB.connect(radioA);
    TEST_ASSERT_TRUE(radioA.begin());
    TEST_ASSERT_TRUE(radioB.begin());
    HMS_Messaging nodeA(radioA);
    HMS_Messaging nodeB(radioB);
    size_t delivered = 0;
    const uint8_t payload[] = {1, 2, 3};
    nodeB.setMessageCallback([&delivered](const uint8_t*, const uint8_t*, size_t) { ++delivered; });

    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(HMS_ReliableResult::OK),
                             static_cast<uint32_t>(nodeA.sendReliable(NODE_B_MAC.data(), payload, sizeof(payload), 20, 0)));
    TEST_ASSERT_EQUAL_UINT32(1, delivered);
    TEST_ASSERT_EQUAL_UINT32(18, radioA.lastFrameLength() - sizeof(payload));

    radioB.inject(NODE_A_MAC.data(), radioA.lastFrame().data(), radioA.lastFrameLength());
    TEST_ASSERT_EQUAL_UINT32(1, delivered);

    auto newSessionFrame = radioA.lastFrame();
    for (size_t i = 4; i < 12; ++i) newSessionFrame[i] = 0xFF;
    if (std::memcmp(newSessionFrame.data() + 4, radioA.lastFrame().data() + 4, 8) == 0) {
        newSessionFrame[4] = 0xFE;
    }
    radioB.inject(NODE_A_MAC.data(), newSessionFrame.data(), radioA.lastFrameLength());
    TEST_ASSERT_EQUAL_UINT32(2, delivered);
    TEST_ASSERT_EQUAL_UINT32(3, radioB.sendCount());
}

extern "C" void app_main(void) {
    unity_run_all_tests();
}
