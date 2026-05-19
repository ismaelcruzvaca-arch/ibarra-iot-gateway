#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include "EMAFilter.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "include/secrets.h"
#include "PayloadSerializer.h"
#include <time.h>
#include <LittleFS.h>


#ifndef INPUT_PIN
#define INPUT_PIN 18
#endif

// Struct matching specification
struct ProductionEvent {
    uint32_t cycleCount;      // Monotonically increasing counter for production cycles
    unsigned long timestamp;  // Local timestamp of the event in milliseconds
    uint8_t pinId;            // The hardware ID of the digital pin that triggered the event
};

struct SpooledEvent {
    uint32_t cycleCount;
    time_t epochTime;
};

// Queue handle
QueueHandle_t productionEventQueue = NULL;

// ISR variables
volatile int64_t lastInterruptTime = 0;
volatile uint32_t globalCycleCount = 0;

// Global ADC and EMA Filter
Adafruit_ADS1115 ads;
EMAFilter emaFilter(0.1f);

// Connection State Machine Definition
enum ConnectionState {
    STATE_DISCONNECTED,
    STATE_CONNECTING_WIFI,
    STATE_SYNCING_TIME,
    STATE_CONNECTING_MQTT,
    STATE_CONNECTED
};

WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);


// ISR decorated with IRAM_ATTR
void IRAM_ATTR inputISR() {
    int64_t currentTime = esp_timer_get_time();
    // 50ms software debounce (50,000 microseconds)
    if (currentTime - lastInterruptTime >= 50000) {
        lastInterruptTime = currentTime;
        globalCycleCount++;

        ProductionEvent event;
        event.cycleCount = globalCycleCount;
        event.timestamp = currentTime / 1000;
        event.pinId = INPUT_PIN;

        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xQueueSendFromISR(productionEventQueue, &event, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken) {
            portYIELD_FROM_ISR();
        }
    }
}

// Task Networking (pinned to Core 0)
void vTaskNetworking(void *pvParameters) {
    ConnectionState currentState = STATE_DISCONNECTED;
    unsigned long lastWifiRetryMs = 0;
    unsigned long wifiConnectStartMs = 0;
    unsigned long lastMqttRetryMs = 0;
    bool ntpSynced = false;

    Serial.println("[Networking] Starting networking task on Core 0...");

    while (true) {
        // If not in disconnected/connecting wifi states, verify WiFi status
        if (currentState != STATE_DISCONNECTED && currentState != STATE_CONNECTING_WIFI) {
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("[Networking] Wi-Fi connection lost!");
                currentState = STATE_DISCONNECTED;
                lastWifiRetryMs = millis();
            }
        }

        switch (currentState) {
            case STATE_DISCONNECTED: {
                if (lastWifiRetryMs == 0 || (millis() - lastWifiRetryMs >= 5000)) {
                    Serial.println("[Networking] Initiating Wi-Fi connection...");
                    WiFi.disconnect(true);
                    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
                    wifiConnectStartMs = millis();
                    lastWifiRetryMs = millis();
                    currentState = STATE_CONNECTING_WIFI;
                }
                break;
            }

            case STATE_CONNECTING_WIFI: {
                if (WiFi.status() == WL_CONNECTED) {
                    Serial.println("[Networking] Wi-Fi connected. Synchronizing time via NTP...");
                    configTime(0, 0, "pool.ntp.org");
                    currentState = STATE_SYNCING_TIME;
                } else if (millis() - wifiConnectStartMs >= 15000) {
                    Serial.println("[Networking] Wi-Fi connection timeout. Retrying...");
                    WiFi.disconnect(true);
                    currentState = STATE_DISCONNECTED;
                    lastWifiRetryMs = millis();
                }
                break;
            }

            case STATE_SYNCING_TIME: {
                Serial.println("[Networking] Waiting for NTP sync...");
                time_t now = time(nullptr);
                struct tm timeinfo;
                gmtime_r(&now, &timeinfo);
                while (timeinfo.tm_year + 1900 <= 2024) {
                    if (WiFi.status() != WL_CONNECTED) {
                        break;
                    }
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    now = time(nullptr);
                    gmtime_r(&now, &timeinfo);
                }

                if (WiFi.status() == WL_CONNECTED) {
                    Serial.println("[Networking] NTP synchronized. Setting client certificates...");
                    ntpSynced = true;
                    espClient.setCACert(CA_CERT);
                    espClient.setCertificate(CLIENT_CERT);
                    espClient.setPrivateKey(CLIENT_KEY);
                    mqttClient.setServer(MQTT_BROKER_IP, 8883);
                    lastMqttRetryMs = 0; // Try connecting immediately
                    currentState = STATE_CONNECTING_MQTT;
                } else {
                    currentState = STATE_DISCONNECTED;
                    lastWifiRetryMs = millis();
                }
                break;
            }

            case STATE_CONNECTING_MQTT: {
                if (lastMqttRetryMs == 0 || (millis() - lastMqttRetryMs >= 5000)) {
                    lastMqttRetryMs = millis();
                    Serial.println("[Networking] Attempting MQTTS connection...");
                    const char* clientId = "norvi_001";
                    const char* lwtTopic = "novamex/ibarra/production/norvi_001";
                    const char* lwtPayload = "{\"node_health\":\"OFFLINE\",\"metrics\":[]}";
                    if (mqttClient.connect(clientId, lwtTopic, 1, true, lwtPayload)) {
                        Serial.println("[Networking] MQTTS connected. Publishing initial ONLINE status...");
                        time_t now = time(nullptr);
                        struct tm timeinfo;
                        gmtime_r(&now, &timeinfo);
                        char isoTimestamp[25];
                        strftime(isoTimestamp, sizeof(isoTimestamp), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
                        String payload = serializePayload(globalCycleCount, isoTimestamp);
                        mqttClient.publish(lwtTopic, payload.c_str(), true);
                        currentState = STATE_CONNECTED;
                    } else {
                        Serial.printf("[Networking] MQTTS connection failed, rc=%d. Will retry...\n", mqttClient.state());
                    }
                }
                break;
            }

            case STATE_CONNECTED: {
                if (!mqttClient.connected()) {
                    Serial.println("[Networking] MQTTS connection lost!");
                    currentState = STATE_CONNECTING_MQTT;
                    lastMqttRetryMs = millis();
                } else {
                    mqttClient.loop();

                    bool isSpoolPending = false;
                    if (LittleFS.exists("/spool.dat")) {
                        isSpoolPending = true;
                        File spoolFile = LittleFS.open("/spool.dat", "r");
                        if (spoolFile) {
                            int count = 0;
                            while (spoolFile.available() > 0 && count < 10) {
                                SpooledEvent se;
                                if (spoolFile.read((uint8_t*)&se, sizeof(SpooledEvent)) == sizeof(SpooledEvent)) {
                                    struct tm timeinfo;
                                    gmtime_r(&se.epochTime, &timeinfo);
                                    char isoTimestamp[25];
                                    strftime(isoTimestamp, sizeof(isoTimestamp), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
                                    String payload = serializePayload(se.cycleCount, isoTimestamp);
                                    const char* topic = "novamex/ibarra/production/norvi_001";
                                    mqttClient.publish(topic, payload.c_str(), true);
                                    count++;
                                } else {
                                    break;
                                }
                            }
                            if (spoolFile.available() == 0) {
                                spoolFile.close();
                                LittleFS.remove("/spool.dat");
                                isSpoolPending = false;
                                Serial.println("[Networking] Spool fully flushed.");
                            } else {
                                spoolFile.close();
                            }
                            if (count > 0) {
                                vTaskDelay(pdMS_TO_TICKS(50));
                            }
                        } else {
                            isSpoolPending = false;
                        }
                    }

                    if (!isSpoolPending) {
                        ProductionEvent event;
                        // Dequeue event and process
                        if (xQueueReceive(productionEventQueue, &event, 0) == pdTRUE) {
                            int64_t currentBootMs = esp_timer_get_time() / 1000;
                            int64_t timeDifferenceMs = currentBootMs - event.timestamp;
                            time_t eventEpochSec = time(nullptr) - (timeDifferenceMs / 1000);

                            struct tm timeinfo;
                            gmtime_r(&eventEpochSec, &timeinfo);
                            char isoTimestamp[25];
                            strftime(isoTimestamp, sizeof(isoTimestamp), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);

                            String payload = serializePayload(event.cycleCount, isoTimestamp);
                            const char* topic = "novamex/ibarra/production/norvi_001";
                            if (mqttClient.publish(topic, payload.c_str(), true)) {
                                Serial.printf("[Networking] Published event: cycleCount=%u, timestamp=%s\n", event.cycleCount, isoTimestamp);
                            } else {
                                Serial.println("[Networking] Failed to publish event!");
                            }
                        }
                    }
                }
                break;
            }
        }

        if (currentState != STATE_CONNECTED) {
            ProductionEvent event;
            if (xQueueReceive(productionEventQueue, &event, 0) == pdTRUE) {
                if (ntpSynced) {
                    int64_t currentBootMs = esp_timer_get_time() / 1000;
                    int64_t timeDifferenceMs = currentBootMs - event.timestamp;
                    time_t eventEpochSec = time(nullptr) - (timeDifferenceMs / 1000);

                    File spoolFile = LittleFS.open("/spool.dat", "a");
                    if (spoolFile) {
                        SpooledEvent se;
                        se.cycleCount = event.cycleCount;
                        se.epochTime = eventEpochSec;
                        spoolFile.write((uint8_t*)&se, sizeof(SpooledEvent));
                        spoolFile.close();
                        Serial.printf("[Networking] Spooled event offline: cycleCount=%u\n", se.cycleCount);
                    }
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}


// Task Application (pinned to Core 1)
void vTaskApplication(void *pvParameters) {
    while (true) {
        Serial.println("[Application] Active and processing logic...");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

// Task Analog Acquisition (pinned to Core 1)
void vTaskAnalog(void *pvParameters) {
    while (true) {
        int16_t raw_adc = ads.readADC_SingleEnded(0);
        float volts = ads.computeVolts(raw_adc);
        float filtered_volts = emaFilter.filter(volts);
        Serial.printf("[Analog] Raw ADC: %d, Raw Voltage: %.4f V, Filtered Voltage: %.4f V\n",
                      raw_adc, volts, filtered_volts);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void setup() {
    Serial.begin(115200);
    while (!Serial) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    Serial.println("[System] Initializing Norvi Concurrent Monitor...");

    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS Mount Failed");
    }

    // Initialize I2C Wire on GPIO 21 (SDA) and GPIO 22 (SCL)
    Wire.begin(21, 22);

    // Initialize ADS1115 ADC
    if (!ads.begin(0x48)) {
        Serial.println("[System] Error: Failed to initialize ADS1115 ADC!");
    } else {
        Serial.println("[System] ADS1115 ADC initialized successfully.");
    }
    ads.setGain(GAIN_ONE);

    // Create FreeRTOS Queue for 50 elements
    productionEventQueue = xQueueCreate(50, sizeof(ProductionEvent));
    if (productionEventQueue == NULL) {
        Serial.println("[System] Error: Failed to create productionEventQueue!");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    // Configure Pin and Attach Interrupt
    pinMode(INPUT_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(INPUT_PIN), inputISR, FALLING);
    Serial.printf("[System] Configured Input Pin: %d with FALLING interrupt\n", INPUT_PIN);

    // Create & Pin Tasks
    // Pinned to Core 0 (PRO_CPU)
    xTaskCreatePinnedToCore(
        vTaskNetworking,
        "vTaskNetworking",
        8192,
        NULL,
        1,
        NULL,
        0
    );
    Serial.println("[System] Task 'vTaskNetworking' pinned to Core 0");

    // Pinned to Core 1 (APP_CPU)
    xTaskCreatePinnedToCore(
        vTaskApplication,
        "vTaskApplication",
        4096,
        NULL,
        1,
        NULL,
        1
    );
    Serial.println("[System] Task 'vTaskApplication' pinned to Core 1");

    // Pinned to Core 1 (APP_CPU)
    xTaskCreatePinnedToCore(
        vTaskAnalog,
        "vTaskAnalog",
        4096,
        NULL,
        1,
        NULL,
        1
    );
    Serial.println("[System] Task 'vTaskAnalog' pinned to Core 1");

    Serial.println("[System] Initialization complete.");
}

void loop() {
    // Keep loop empty, delay to yield control
    vTaskDelay(pdMS_TO_TICKS(10000));
}

