#include <Arduino.h>
#include <NimBLEDevice.h>
#include "esp_system.h"
#include <esp_mac.h> 
#include <WiFi.h>
#include <esp_now.h>

NimBLEScan* pBLEScan = nullptr;

uint8_t espNowPeerMac[] = { 0x50, 0x78, 0x7D, 0x13, 0x22, 0xF8 };

#pragma pack(push, 1)
struct TpmsPacket {
    uint32_t sensorId;
    uint32_t pressure;  // raw 32-bit value as you decode it
    uint16_t temp;      // raw 16-bit value
};
#pragma pack(pop)

void onEspNowSent(const wifi_tx_info_t* tx_info, esp_now_send_status_t status) {
    Serial.print("ESP-NOW send status: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "SUCCESS" : "FAIL");
}

class MyScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
        String name = advertisedDevice->getName().c_str();

        // Ignore unnamed devices
        if (!name.length()) {
            return;
        }

        // Filter: only devices whose name starts with "TPMS"
        if (!name.startsWith("TPMS")) {
            return;
        }

        Serial.print(name);
        Serial.print("  |  ");
        Serial.print(advertisedDevice->getAddress().toString().c_str());

        const std::vector<uint8_t>& payload = advertisedDevice->getPayload();
        size_t length = payload.size();

        if (length >= 32) {
            uint32_t pressure = 0;
            pressure |= (uint32_t)payload[17];
            pressure |= (uint32_t)payload[18] << 8;
            pressure |= (uint32_t)payload[19] << 16;
            pressure |= (uint32_t)payload[20] << 24;

            uint16_t temp = 0;
            temp |= (uint16_t)payload[21];
            temp |= (uint16_t)payload[22] << 8;

            uint32_t sensorId = 0;
            sensorId |= (uint32_t)payload[14];
            sensorId |= (uint32_t)payload[15] << 8;
            sensorId |= (uint32_t)payload[16] << 16;

            Serial.print("  |  Sensor: ");
            Serial.print(sensorId);
            Serial.print("  |  Pressure: ");
            Serial.print(pressure);
            Serial.print("  |  Temp: ");
            Serial.print(temp);

            // === Build ESP-NOW packet ===
            TpmsPacket pkt;
            pkt.sensorId = sensorId;
            pkt.pressure = pressure;
            pkt.temp     = temp;

            esp_err_t res = esp_now_send(espNowPeerMac, (uint8_t*)&pkt, sizeof(pkt));
            Serial.print("  |  ESP-NOW send: ");
            Serial.println(res == ESP_OK ? "OK" : String("ERR ") + res);
        } else {
            Serial.print("  |  Payload too short (");
            Serial.print(length);
            Serial.print(")");
        }

        Serial.println();
    }

    void onScanEnd(const NimBLEScanResults& results, int reason) override {
        Serial.println("Scan ended");
    }
};

// ==== Setup ==== //
void setup() {
    Serial.begin(115200);
    Serial.println();
    Serial.println("TPMS gateway: scanning TPMS* and forwarding via ESP-NOW.");

    WiFi.mode(WIFI_STA);        // required for ESP-NOW
    WiFi.disconnect();          // just to be safe

    // Read the real STA MAC from efuse
    uint8_t staMac[6];
    esp_read_mac(staMac, ESP_MAC_WIFI_STA);

    char macStr[18];
    snprintf(macStr, sizeof(macStr),
            "%02X:%02X:%02X:%02X:%02X:%02X",
            staMac[0], staMac[1], staMac[2],
            staMac[3], staMac[4], staMac[5]);

    Serial.print("ESP-NOW (STA) MAC: ");
    Serial.println(macStr);

    // Now init ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init failed!");
        while (true) { delay(1000); }
    }

    // New-style callback signature for your core:
    esp_now_register_send_cb(onEspNowSent);

    // Add peer (your ESP32-S3)
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, espNowPeerMac, 6);
    peerInfo.channel = 0;   // 0 = current Wi-Fi channel
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("Failed to add ESP-NOW peer!");
        while (true) { delay(1000); }
    }

    // --- BLE scanner init (NimBLE) --- //
    NimBLEDevice::init("TPMS-Gateway");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); // TX power for scan requests, etc.

    pBLEScan = NimBLEDevice::getScan();
    pBLEScan->setScanCallbacks(new MyScanCallbacks(), /*wantDuplicates=*/true);
    pBLEScan->setFilterPolicy(BLE_HCI_SCAN_FILT_NO_WL);
    pBLEScan->setActiveScan(true);  // request scan response
    
    Serial.println("Setup complete.");
}

void loop() {
    static uint32_t lastScanStart = 0;
    uint32_t now = millis();

    // If not currently scanning, start a 30-second scan every ~5s gap
    if (!pBLEScan->isScanning() && (now - lastScanStart > 5000)) {
        Serial.println("Starting TPMS scan...");
        // duration=30 (seconds), isContinue=false, restart=false
        pBLEScan->start(0, false, false);
        lastScanStart = now;
    }

    delay(5000);

    //PING
    uint8_t pingBuffer = 0;
    esp_err_t res = esp_now_send(espNowPeerMac, &pingBuffer, 1);
}
