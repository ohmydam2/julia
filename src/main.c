#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>


// CONSTANTS

#define TAG "JULIA"

#define JULIA_SSID "julia"
#define JULIA_PASS "ihatedelilah"

#define WIFI_LED_PERIOD_MS 500
#define WIFI_LED_PIN 4

#define EVTBIT_CLIENT_CONNECTED BIT0
#define EVTBIT_CLIENT_HAS_IP BIT1


// GLOBAL VARS

TimerHandle_t wifi_led_timer;

EventGroupHandle_t client_evt_group;
int32_t client_ip;

esp_netif_t *netif;

int sock;


// PROTOTYPES

void die(char const *msg);
void die_errno(const char *msg);
void die_esp(esp_err_t err);

void wifi_init(void);
void wifi_start_led(void);
void wifi_led_cb(TimerHandle_t timer);
void wifi_evt_cb(
        void *arg,
        esp_event_base_t evt_base,
        int32_t evt_id,
        void *evt_data
);
void wifi_ip_evt_cb(
        void *arg,
        esp_event_base_t evt_base,
        int32_t evt_id,
        void *evt_data
);

void socket_open(void);

void cmd_listener_task(void *arg);
void cmd_process(uint8_t cmd);


// MAIN

void app_main(void)
{
        wifi_init();
        wifi_start_led();

        socket_open();

        xTaskCreate(
                cmd_listener_task,
                "cmd_listener_task",
                1<<12,
                NULL,
                tskIDLE_PRIORITY + 1,
                NULL
        );
}


// DEFINITIONS

void die(const char *msg)
{
    ESP_LOGE(TAG, "FATAL: %s", msg ? msg : "(null)");
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_restart();
}

void die_errno(const char *msg)
{
    int err = errno;
    const char *err_str = strerror(err);

    ESP_LOGE(TAG, "FATAL: %s: errno=%d (%s)",
             msg ? msg : "(null)",
             err,
             err_str ? err_str : "unknown");

    vTaskDelay(pdMS_TO_TICKS(50));
    esp_restart();
}


void die_esp(esp_err_t err)
{
    if (err == ESP_OK) return;

    ESP_LOGE(TAG, "ESP-IDF error: %s (0x%x)", esp_err_to_name(err), (unsigned)err);
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_restart();
}

void wifi_init(void)
{
        die_esp(nvs_flash_init());
        die_esp(esp_netif_init());

        die_esp(esp_event_loop_create_default());
        client_evt_group = xEventGroupCreate();

        netif = esp_netif_create_default_wifi_ap();

        wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
        die_esp(esp_wifi_init(&init_cfg));

        wifi_config_t cfg = {
                .ap = {
                        .ssid = JULIA_SSID,
                        .ssid_len = strlen(JULIA_SSID),
                        .password = JULIA_PASS,
                        .max_connection = 1,
                        .authmode = WIFI_AUTH_WPA_WPA2_PSK
                }
        };
        die_esp(esp_wifi_set_mode(WIFI_MODE_AP));
        die_esp(esp_wifi_set_config(WIFI_IF_AP, &cfg));

        die_esp(esp_event_handler_register(
                WIFI_EVENT,
                ESP_EVENT_ANY_ID,
                &wifi_evt_cb,
                NULL
        ));

        die_esp(esp_event_handler_register(
                IP_EVENT,
                IP_EVENT_AP_STAIPASSIGNED,
                &wifi_ip_evt_cb,
                NULL
        ));

        die_esp(esp_wifi_start());
}

void wifi_start_led(void)
{
        gpio_set_direction(WIFI_LED_PIN, GPIO_MODE_OUTPUT);
        wifi_led_timer = xTimerCreate(
                "wifi_led_timer",
                pdMS_TO_TICKS(WIFI_LED_PERIOD_MS),
                1,
                NULL,
                &wifi_led_cb
        );

        xTimerStart(wifi_led_timer, 0);
}

void wifi_evt_cb(
        void *arg,
        esp_event_base_t evt_base,
        int32_t evt_id,
        void *evt_data
)
{
        switch (evt_id)
        {
        case WIFI_EVENT_AP_STACONNECTED:
                xEventGroupSetBits(client_evt_group, EVTBIT_CLIENT_CONNECTED);
                xEventGroupClearBits(client_evt_group, EVTBIT_CLIENT_HAS_IP);
                break;

        case WIFI_EVENT_AP_STADISCONNECTED:
                xEventGroupClearBits(client_evt_group,
                        EVTBIT_CLIENT_CONNECTED | EVTBIT_CLIENT_HAS_IP);
                break;
        }
}

void wifi_ip_evt_cb(
        void *arg,
        esp_event_base_t evt_base,
        int32_t evt_id,
        void *evt_data
)
{
        ip_event_ap_staipassigned_t *data =
                (ip_event_ap_staipassigned_t *) evt_data;
        
        xEventGroupSetBits(client_evt_group, EVTBIT_CLIENT_HAS_IP);
        client_ip = data->ip.addr;
}

void wifi_led_cb(TimerHandle_t timer)
{
        static uint8_t led_state = 0;
        static uint8_t slow_cnt = 0;

        EventBits_t bits = xEventGroupGetBits(client_evt_group);

        if (bits & EVTBIT_CLIENT_HAS_IP)
                led_state = 1;
        else if (bits & EVTBIT_CLIENT_CONNECTED)
                led_state = !led_state;
        else
        {
                if (++slow_cnt >= 2)
                {
                        slow_cnt = 0;
                        led_state = !led_state;
                }
        }


        gpio_set_level(WIFI_LED_PIN, led_state);
}

void socket_open(void)
{
        sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock < 0)
                die_errno("udp socket creation failed");

        struct sockaddr_in addr = {0};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(80);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

        if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
                die_errno("udp socket creation failed");
}

void cmd_listener_task(void *arg)
{
        struct pollfd pfd = {
                .fd = sock,
                .events = POLLIN
        };

        while (1)
        {
                vTaskDelay(100/portTICK_PERIOD_MS);
                xEventGroupWaitBits(client_evt_group,
                        EVTBIT_CLIENT_CONNECTED | EVTBIT_CLIENT_HAS_IP,
                        pdFALSE,
                        pdTRUE,
                        portMAX_DELAY
                );

                int ret = poll(&pfd, 1, 0);
                if (ret == -1)
                        die_errno("polling failed");

                if (ret == 0)
                        continue;

                if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL))
                        die_errno("socket error/hangup");

                if (pfd.revents & POLLIN)
                {
                        uint8_t byte;
                        ssize_t n = recv(sock, &byte, 1, 0);
                        if (n == 1)
                                cmd_process(byte);
                        else if (n == 0)
                              die("client closed");
                        else
                        {
                                if (errno == EAGAIN)
                                        continue;
                                else
                                        die_errno("receiving failed");
                        }
                }
        }
}

void cmd_process(uint8_t cmd)
{
        printf("received byte %X\n", cmd);
}