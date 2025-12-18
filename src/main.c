#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "driver/uart.h"

#define WIFI_SSID "RIEGO123"
#define WIFI_PASS "riego12345"

#define GUI_IP "192.168.1.100"
#define UDP_PORT 5006

#define UART_USB     UART_NUM_0
#define UART_SENSOR  UART_NUM_2
#define BUF_SIZE 256

static const char *TAG = "UART_UDP";

/* ================= WIFI ================= */
static void wifi_init(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Conectando a WiFi...");
    ESP_ERROR_CHECK(esp_wifi_connect());

    vTaskDelay(pdMS_TO_TICKS(5000));
}

/* ================= UART ================= */
static void uart_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };

    ESP_ERROR_CHECK(uart_driver_install(UART_USB, BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_USB, &uart_config));

    ESP_ERROR_CHECK(uart_driver_install(UART_SENSOR, BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_SENSOR, &uart_config));

    ESP_ERROR_CHECK(
        uart_set_pin(UART_SENSOR, 17, 16,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

/* ================= MAPEOS ================= */

// SOIL1 → sensor capacitivo normal (invertido)
static int map_soil1(int raw)
{
    int percent = (4095 - raw) * 100 / 4095;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    return percent;
}

// SOIL2 → sensor tipo lluvia (directo)
static int map_soil2(int raw)
{
    int percent = raw * 100 / 4095;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    return percent;
}

static int map_tank(int raw)
{
    const int min_val = 13;
    const int max_val = 4;

    int tank_percent = (min_val - raw) * 100 / (min_val - max_val);
    if (tank_percent < 0) tank_percent = 0;
    if (tank_percent > 100) tank_percent = 100;
    return tank_percent;
}

static int map_rain(int raw)
{
    return (raw > 1500) ? 1 : 0;
}

/* ================= TASK ================= */
static void uart_udp_task(void *arg)
{
    char buf[BUF_SIZE];

    struct sockaddr_in dest_addr = {
        .sin_addr.s_addr = inet_addr(GUI_IP),
        .sin_family = AF_INET,
        .sin_port = htons(UDP_PORT)
    };

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "No se pudo crear socket UDP");
        vTaskDelete(NULL);
    }

    ESP_LOGI(TAG, "Listo: UART2 -> UART0 / UDP");

    while (1) {
        int len = uart_read_bytes(
            UART_SENSOR,
            (uint8_t *)buf,
            BUF_SIZE - 1,
            pdMS_TO_TICKS(100));

        if (len > 0) {
            buf[len] = '\0';

            int soil1 = 0, soil2 = 0, tank = 0, rain = 0;
            float amb = 0.0f, temp = 0.0f;
            float value = 0.0f;

            char *token = strtok(buf, ";");
            while (token != NULL) {
                char key[16];

                if (sscanf(token, "%15[^=]=%f", key, &value) == 2) {

                    if (strcmp(key, "SOIL1") == 0)
                        soil1 = map_soil1((int)value);

                    else if (strcmp(key, "SOIL2") == 0)
                        soil2 = map_soil2((int)value);

                    else if (strcmp(key, "TANK") == 0)
                        tank = map_tank((int)value);

                    else if (strcmp(key, "RAIN") == 0)
                        rain = map_rain((int)value);

                    else if (strcmp(key, "AMB") == 0)
                        amb = value;

                    else if (strcmp(key, "TEMP") == 0)
                        temp = value;
                }

                token = strtok(NULL, ";");
            }

            char outbuf[BUF_SIZE];
            int out_len = snprintf(
                outbuf, BUF_SIZE,
                "SOIL1=%d;SOIL2=%d;RAIN=%d;TANK=%d;AMB=%.1f;TEMP=%.1f",
                soil1, soil2, rain, tank, amb, temp);

            uart_write_bytes(UART_USB, outbuf, out_len);
            sendto(sock, outbuf, out_len, 0,
                   (struct sockaddr *)&dest_addr,
                   sizeof(dest_addr));
        }
    }
}

/* ================= MAIN ================= */
void app_main(void)
{
    wifi_init();
    uart_init();

    xTaskCreate(uart_udp_task,
                "uart_udp_task",
                4096,
                NULL,
                5,
                NULL);
}
