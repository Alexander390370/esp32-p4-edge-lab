/*
 * ESP32-P4 最小可运行固件
 *
 * 目的：验证「代码能编译、能烧进去、能跑起来、能输出」这条链路。
 * 如果这个都跑不通，后面什么功能都别谈。
 *
 * 期望输出：
 *   ========================================
 *    ESP32-P4 Hello UART
 *   ========================================
 *   chip      : ESP32-P4
 *   cores     : 2
 *   revision  : 0
 *   freq      : 400 MHz
 *   ...
 *   [心跳] 运行 5 秒，空闲堆 xxxxx 字节
 *
 * 烧录：
 *   idf.py set-target esp32p4
 *   idf.py -p COM12 flash monitor
 */

#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

void app_main(void)
{
    printf("\n");
    printf("========================================\n");
    printf("  ESP32-P4 Hello UART\n");
    printf("========================================\n");

    // ---- 芯片信息 ----
    esp_chip_info_t info;
    esp_chip_info(&info);

    const char *model = "unknown";
    switch (info.model) {
        case CHIP_ESP32:    model = "ESP32";    break;
        case CHIP_ESP32S2:  model = "ESP32-S2"; break;
        case CHIP_ESP32S3:  model = "ESP32-S3"; break;
        case CHIP_ESP32C2:  model = "ESP32-C2"; break;
        case CHIP_ESP32C3:  model = "ESP32-C3"; break;
        case CHIP_ESP32C6:  model = "ESP32-C6"; break;
        case CHIP_ESP32H2:  model = "ESP32-H2"; break;
        case CHIP_ESP32P4:  model = "ESP32-P4"; break;
        default: break;
    }

    printf("chip      : %s\n", model);
    printf("cores     : %d\n", info.cores);
    printf("revision  : %d\n", info.revision);
    printf("freq      : %" PRIu32 " MHz\n", esp_clk_cpu_freq() / 1000000);

    // ---- Flash ----
    uint32_t flash_size = 0;
    if (esp_flash_get_size(NULL, &flash_size) == ESP_OK) {
        printf("flash     : %" PRIu32 " MB\n", flash_size / (1024 * 1024));
    }

    // ---- 内存 ----
    printf("heap int  : %" PRIu32 " 字节\n",
           (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    printf("heap psram: %" PRIu32 " 字节\n",
           (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    printf("heap dma  : %" PRIu32 " 字节\n",
           (uint32_t)heap_caps_get_free_size(MALLOC_CAP_DMA));

    printf("========================================\n");
    printf("如果你能看到这些字，说明：\n");
    printf("  1. 固件烧录成功\n");
    printf("  2. 应用程序启动成功\n");
    printf("  3. UART 输出正常\n");
    printf("========================================\n\n");

    // ---- 心跳循环 ----
    // 每秒打印一次，确认程序在稳定运行（不是启动完就卡死）
    int sec = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        sec++;
        if (sec % 5 == 0) {
            printf("[心跳] 运行 %d 秒，内部堆 %" PRIu32 " 字节\n",
                   sec,
                   (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        }
    }
}