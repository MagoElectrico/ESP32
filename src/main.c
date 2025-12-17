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

#define WIFI_SSID "JIGALO"
#define WIFI_PASS "J1L1M4CASA"

/* IP DEL PC DONDE CORRE LA GUI */
#define GUI_IP "192.168.0.33"
#define UDP_PORT 5006

/* UART PARA PUTTY (USB) */
#define UART_NUM UART_NUM_0
#define BUF_SIZE 256

static const char *TAG = "UART_UDP";

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

static void uart_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = 9600,   // Igual que PuTTY
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };

    ESP_ERROR_CHECK(uart_driver_install(UART_NUM, BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM, &uart_config));
}

static void uart_udp_task(void *arg)
{
    char data[BUF_SIZE];

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

    ESP_LOGI(TAG, "Listo: UART -> UDP");

    while (1) {
        int len = uart_read_bytes(
            UART_NUM,
            (uint8_t *)data,
            BUF_SIZE - 1,
            pdMS_TO_TICKS(100)
        );

        if (len > 0) {
            data[len] = '\0';

            ESP_LOGI(TAG, "UART RX: %s", data);

            sendto(sock,
                   data,
                   strlen(data),
                   0,
                   (struct sockaddr *)&dest_addr,
                   sizeof(dest_addr));
        }
    }
}

void app_main(void)
{
    wifi_init();
    uart_init();

    xTaskCreate(
        uart_udp_task,
        "uart_udp_task",
        4096,
        NULL,
        5,
        NULL
    );
}
