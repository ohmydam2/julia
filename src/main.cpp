// =============================================================================
// INCLUDES
// =============================================================================
#include <Arduino.h>

#include <WiFi.h>
#include <WiFiAP.h>
#include <WiFiUdp.h>

#include <esp32cam.h>

// =============================================================================
// DEFINES
// =============================================================================
#define LED_CONNECTED_PIN 33
#define LED_CONNECTED_PERIOD_MS 500

#define WIFI_SSID "julia"
#define WIFI_PASSWORD "ihatedelilah"
#define WIFI_CHANNEL 1
#define WIFI_MAX_CONNECTED 1

#define UDP_PORT 80
#define UDP_POLL_PERIOD_MS 500

// =============================================================================
// GLOBALS
// =============================================================================
bool client_is_connected = false;

WiFiUDP wifi_udp;

// =============================================================================
// PROTOTYPES
// =============================================================================
void wifi_connected_callback(arduino_event_id_t id);
void led_connected_callback(TimerHandle_t timer);
void command_process(uint8_t command);

// =============================================================================
// MAIN
// =============================================================================
void setup()
{
    // Serial
    Serial.begin(115200);

    // IO
    pinMode(LED_CONNECTED_PIN, OUTPUT);

    // WiFi
    WiFi.softAP(
        WIFI_SSID,
        WIFI_PASSWORD,
        WIFI_CHANNEL,
        0,
        WIFI_MAX_CONNECTED,
        false
    );

    WiFi.onEvent(wifi_connected_callback, ARDUINO_EVENT_MAX);

    // Connection indicator
    TimerHandle_t led_connected_timer = xTimerCreate(
        "led_connected_timer",
        pdMS_TO_TICKS(LED_CONNECTED_PERIOD_MS),
        pdTRUE,
        NULL,
        led_connected_callback
    );

    xTimerStart(led_connected_timer, 0);

    // UDP
    wifi_udp.begin(UDP_PORT);

    // Camera
    esp32cam::Config camera_config;
    camera_config.setPins(esp32cam::pins::AiThinker);
    // Sets resolution to QVGA, minimum supported by the datasheet
    camera_config.setResolution(esp32cam::Resolution::find(300, 240));
    // Sets pixel format to RGB565
    camera_config.setRgb();

    esp32cam::Camera.begin(camera_config);
}

void loop()
{
    int size = wifi_udp.parsePacket();

    switch (size)
    {
        case 1:
            break;
        case 0:
            vTaskDelay(pdMS_TO_TICKS(UDP_POLL_PERIOD_MS));
            return;
        default:
            Serial.println("Invalid command size");
            vTaskDelay(pdMS_TO_TICKS(UDP_POLL_PERIOD_MS));
            return;
    }

    uint8_t command = wifi_udp.read();
    command_process(command);
}

// =============================================================================
// FUNCTIONS
// =============================================================================
void wifi_connected_callback(arduino_event_id_t id)
{
    switch (id)
    {
        case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
            Serial.println("Client connected");
            client_is_connected = true;
            break;
        case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
        Serial.println("Client disconnected");
            client_is_connected = false;
            break;
        default:
            break;
    }
}

void led_connected_callback(TimerHandle_t timer)
{
    static bool led_is_on = false;
    
    if (client_is_connected) {
        led_is_on = true;
    }
    else {
        led_is_on = !led_is_on;
    }

    // In schematic, pulling to ground turns LED on
    digitalWrite(LED_CONNECTED_PIN, !led_is_on);
}

void command_process(uint8_t command)
{
    // This stops the compiler from complaining about frame's lifetime
    std::unique_ptr<esp32cam::Frame> frame;

    switch (command)
    {
        case 's':
            frame = esp32cam::capture();
            printf("Frame size: %u\n", frame->size());
            break;
        default:
            printf("dummy");
            break;
    }
}

