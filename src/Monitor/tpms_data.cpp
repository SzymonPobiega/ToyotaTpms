#include "tpms_data.hpp"
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_wifi.h>

namespace tpms {

volatile uint32_t latestPressure[kMaxSensors] = {0};
volatile uint16_t latestTemp[kMaxSensors]     = {0};
volatile bool     hasData                 = false;
volatile uint32_t lastUpdated[kMaxSensors]    = {0};
volatile uint32_t totalPeriods[kMaxSensors]   = {0};
volatile uint32_t last_update                 = 0;
volatile uint32_t start_time                  = 0;

uint32_t getIndex(uint32_t sensor)
{
    if (sensor == 7549971)  return 0;
    else if (sensor == 1126691)  return 1;
    else if (sensor == 14554163) return 2;
    else if (sensor == 4592707)  return 3;
    return 0;
}

void onEspNowRecv(const esp_now_recv_info_t *info,
                  const uint8_t *data, int len)
{
    uint32_t now = millis();
    if (len < (int)sizeof(TpmsPacket)) {
        last_update = now;
        return;
    }

    TpmsPacket pkt;
    memcpy(&pkt, data, sizeof(pkt));

    uint32_t idx = getIndex(pkt.sensorId);
    if (idx >= kMaxSensors) {
        Serial0.printf("ESP-NOW: unknown sensorId %u\n", pkt.sensorId);
        return;
    }

    latestPressure[idx] = pkt.pressure;
    latestTemp[idx]     = pkt.temp;
    totalPeriods[idx]++;
    lastUpdated[idx]    = now;
    last_update         = now;
    hasData         = true;
}

bool initEspNow()
{
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    uint8_t staMac[6];
    esp_err_t err = esp_read_mac(staMac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) {
        Serial0.printf("esp_read_mac failed: %d\n", err);
        return false;
    }

    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             staMac[0], staMac[1], staMac[2],
             staMac[3], staMac[4], staMac[5]);

    Serial0.print("ESP-NOW (STA) MAC: ");
    Serial0.println(macStr);

    if (esp_now_init() != ESP_OK) {
        Serial0.println("ESP-NOW init failed!");
        return false;
    }

    esp_now_register_recv_cb(onEspNowRecv);
    start_time  = millis();
    last_update = start_time;
    return true;
}

} // namespace tpms
