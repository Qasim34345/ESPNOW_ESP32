#include "demo_modes.h"
#include "peer_table.h"
#include "HMS_EspNowNode.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr char TAG[] = "ESPNOW_EXAMPLE";
constexpr uint32_t SEND_INTERVAL_MS = 1000;
constexpr char MESSAGE[] = "Hello! World";

bool initNvs() {
    esp_err_t err=nvs_flash_init();
    if (err==ESP_ERR_NVS_NO_FREE_PAGES || err==ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGE(TAG,"NVS recovery requires an explicit product data-migration/erase policy; refusing to erase the NVS partition");
        return false;
    }
    if (err!=ESP_OK) ESP_LOGE(TAG,"NVS initialization failed: %s",esp_err_to_name(err));
    return err==ESP_OK;
}

bool initNetworkEvents() {
    esp_err_t err=esp_netif_init();
    if (err!=ESP_OK && err!=ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG,"Network interface initialization failed: %s",esp_err_to_name(err));
        return false;
    }

    err=esp_event_loop_create_default();
    if (err!=ESP_OK && err!=ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG,"Default event loop initialization failed: %s",esp_err_to_name(err));
        return false;
    }
    return true;
}

void onReceive(const uint8_t* mac,const uint8_t* data,size_t len,int8_t rssi) {
    ESP_LOGI(TAG,"RX %02X:%02X:%02X:%02X:%02X:%02X RSSI=%d dBm len=%u",
        mac[0],mac[1],mac[2],mac[3],mac[4],mac[5],(int)rssi,(unsigned)len);
    ESP_LOG_BUFFER_HEXDUMP(TAG,data,len,ESP_LOG_INFO);
    // This example payload is text; use bounded formatting because RX data is not NUL-terminated.
    ESP_LOGI(TAG,"Text: %.*s",(int)len,reinterpret_cast<const char*>(data));
}
void onSend(const uint8_t* mac,bool success) {
    ESP_LOGI(TAG,"MAC TX status %02X:%02X:%02X:%02X:%02X:%02X: %s",
        mac[0],mac[1],mac[2],mac[3],mac[4],mac[5],success?"SUCCESS":"FAILED");
    if (!success) {
        ESP_LOGW(TAG,"No MAC-layer ACK; verify the peer is powered, configured to receive, and using the same Wi-Fi channel.");
    }
}
}

extern "C" void app_main(void) {
    if (!initNvs() || !initNetworkEvents()) return;

    HMS_EspNowNodeConfig cfg{};
    cfg.role=demo_modes::ROLE;
    cfg.topology=demo_modes::TOPOLOGY;
    cfg.addressing=demo_modes::ADDRESSING;
    cfg.peers=ESPNOW_EXAMPLE_PEERS;
    cfg.peer_count=ESPNOW_EXAMPLE_PEER_COUNT;
    cfg.filter_unicast_receivers=true;
    cfg.transport.wifi_policy=HMS_EspNowWifiPolicy::MANAGE_IF_UNINITIALIZED;
    cfg.transport.wifi_mode=WIFI_MODE_STA;
    cfg.transport.interface=WIFI_IF_STA;
    cfg.transport.channel=1; // Set the same channel on all participating nodes.
    cfg.transport.channel_policy=HMS_EspNowChannelPolicy::REQUIRE_MATCH;
    cfg.transport.payload_profile=HMS_EspNowPayloadProfile::V2;
    cfg.transport.peer_policy=HMS_EspNowPeerPolicy::EXPLICIT_ONLY;
    cfg.transport.allow_broadcast=true;
    cfg.transport.rx_queue_depth=16;
    cfg.transport.tx_queue_depth=24;
    cfg.transport.worker_stack_size=6144;

    static HMS_EspNowNode node(cfg);
    node.setReceiveCallback(onReceive);
    node.setSendCallback(onSend);
    esp_err_t err=node.begin();
    if (err!=ESP_OK) {
        ESP_LOGE(TAG,"ESP-NOW node startup failed: %s",esp_err_to_name(err));
        if (err==ESP_ERR_INVALID_ARG) {
            ESP_LOGE(TAG,"Check main/demo_modes.h and main/peer_table.h; peer entries must be other boards, not this device.");
        }
        return;
    }

    const auto caps=node.capabilities();
    ESP_LOGI(TAG,"Native ESP-NOW version=%lu | effective payload=%u | peers=%u",
        (unsigned long)caps.native_version,(unsigned)caps.effective_max_payload,(unsigned)node.peerCount());

    if (node.isTransmitter()) {
        TickType_t lastWake=xTaskGetTickCount();
        for (;;) {
            err=node.sendToPeers(reinterpret_cast<const uint8_t*>(MESSAGE),sizeof(MESSAGE)-1);
            if (err!=ESP_OK) ESP_LOGW(TAG,"Send submission failed: %s",esp_err_to_name(err));
            vTaskDelayUntil(&lastWake,pdMS_TO_TICKS(SEND_INTERVAL_MS));
        }
    }
    // RX-only stays alive while the component's event worker dispatches receive callbacks.
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        const auto s=node.stats();
        ESP_LOGI(TAG,"RX received=%lu delivered=%lu dropped(queue/invalid)=%lu/%lu | TX accepted=%lu success/fail=%lu/%lu",
            (unsigned long)s.rx_received,(unsigned long)s.rx_delivered,
            (unsigned long)s.rx_dropped_queue_full,(unsigned long)s.rx_dropped_invalid,
            (unsigned long)s.tx_requests_accepted,(unsigned long)s.tx_success,(unsigned long)s.tx_failures);
    }
}
