/*
 * ESP32-P4 PSRAM 容量与带宽测试
 *
 * 为什么单独测 PSRAM：
 *   P4 的核心优势就是最大 32MB 外部 PSRAM。但 PSRAM 走 SPI/OSPI，
 *   速度比内部 SRAM 慢一个数量级。做视觉/视频必须知道真实带宽，
 *   否则分配了帧缓冲才发现刷新率上不去。
 *
 * 测试内容：
 *   1. 容量：能分配多少
 *   2. 写带宽：memset 大块
 *   3. 读带宽：memcpy 回来
 *   4. 延迟：随机访问 1 万次
 *
 * 注意：
 *   - 必须在 menuconfig 里开 SPIRAM，否则 heap_caps_get_free_size(MALLOC_CAP_SPIRAM) 返回 0
 *   - P4 常用 SPIRAM_MODE_OCT（8 线），带宽比 QUAD 高约一倍
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "PSRAM";

// 测试用的块大小：1MB
#define BLOCK_SIZE  (1024 * 1024)
// 重复次数（越多越准，但耗时长）
#define REPEAT      20

static int64_t now_us(void)
{
    return esp_timer_get_time();
}

void app_main(void)
{
    printf("\n");
    printf("========================================\n");
    printf("  ESP32-P4 PSRAM 测试\n");
    printf("========================================\n");

    // ---- 1. 容量 ----
    size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t psram_free  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t int_total   = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    size_t int_free    = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

    printf("\n[内存概览]\n");
    printf("  PSRAM : 总 %8" PRIu32 " 字节 (%" PRIu32 " MB), 空闲 %8" PRIu32 "\n",
           (uint32_t)psram_total, (uint32_t)(psram_total / 1024 / 1024),
           (uint32_t)psram_free);
    printf("  内部  : 总 %8" PRIu32 " 字节, 空闲 %8" PRIu32 "\n",
           (uint32_t)int_total, (uint32_t)int_free);

    if (psram_total == 0) {
        printf("\n");
        printf("!!! PSRAM 未启用 !!!\n");
        printf("请在 menuconfig 里打开：\n");
        printf("  Component config -> ESP PSRAM -> Support for external SPI RAM\n");
        printf("  Component config -> ESP PSRAM -> SPI RAM config -> Mode (QUAD/OCT)\n");
        printf("\n");
        while (1) vTaskDelay(pdMS_TO_TICKS(5000));
    }

    // ---- 2. 分配测试块 ----
    printf("\n[分配 1MB PSRAM 缓冲]\n");
    uint8_t *buf = (uint8_t *)heap_caps_malloc(BLOCK_SIZE, MALLOC_CAP_SPIRAM);
    if (!buf) {
        printf("  分配失败！PSRAM 可能不足\n");
        while (1) vTaskDelay(pdMS_TO_TICKS(5000));
    }
    printf("  分配成功: %p\n", buf);

    // ---- 3. 写带宽 ----
    printf("\n[写带宽测试] memset %d 次 x %d KB\n",
           REPEAT, BLOCK_SIZE / 1024);

    int64_t t0 = now_us();
    for (int i = 0; i < REPEAT; i++) {
        memset(buf, i & 0xFF, BLOCK_SIZE);
    }
    int64_t t1 = now_us();

    double write_sec = (t1 - t0) / 1000000.0;
    double write_mbps = (BLOCK_SIZE * (double)REPEAT / 1024.0 / 1024.0) / write_sec;
    printf("  耗时: %.2f ms\n", write_sec * 1000);
    printf("  带宽: %.1f MB/s\n", write_mbps);

    // ---- 4. 读带宽 ----
    printf("\n[读带宽测试] memcpy %d 次\n", REPEAT);

    uint8_t *buf2 = (uint8_t *)heap_caps_malloc(BLOCK_SIZE, MALLOC_CAP_SPIRAM);
    if (!buf2) {
        printf("  第二块分配失败，跳过读测试\n");
    } else {
        t0 = now_us();
        for (int i = 0; i < REPEAT; i++) {
            memcpy(buf2, buf, BLOCK_SIZE);
        }
        t1 = now_us();

        double copy_sec = (t1 - t0) / 1000000.0;
        double copy_mbps = (BLOCK_SIZE * (double)REPEAT / 1024.0 / 1024.0) / copy_sec;
        printf("  耗时: %.2f ms\n", copy_sec * 1000);
        printf("  带宽: %.1f MB/s（读+写各一半，所以实际读约 %.1f MB/s）\n",
               copy_mbps, copy_mbps / 2);
        heap_caps_free(buf2);
    }

    // ---- 5. 随机访问延迟 ----
    printf("\n[随机访问延迟] 10000 次随机字节读写\n");

    #define RANDOM_ACCESSES 10000
    volatile uint8_t sink = 0;
    t0 = now_us();
    uint32_t seed = 12345;
    for (int i = 0; i < RANDOM_ACCESSES; i++) {
        seed = seed * 1103515245 + 12345;      // 线性同余，够随机
        size_t idx = (seed >> 8) % BLOCK_SIZE;
        sink = buf[idx];
    }
    t1 = now_us();

    double lat_us = (t1 - t0) / (double)RANDOM_ACCESSES;
    printf("  平均延迟: %.2f us/次\n", lat_us);
    printf("  （内部 SRAM 通常 <0.01us，PSRAM 慢一个数量级是正常的）\n");

    heap_caps_free(buf);

    // ---- 6. 结论 ----
    printf("\n========================================\n");
    printf("  测试完成\n");
    printf("========================================\n");
    printf("帧缓冲估算参考：\n");
    printf("  720p RGB888 单帧 = 1280*720*3 = %.1f MB\n", 1280.0 * 720 * 3 / 1024 / 1024);
    printf("  720p RGB565 单帧 = 1280*720*2 = %.1f MB\n", 1280.0 * 720 * 2 / 1024 / 1024);
    if (psram_total > 0) {
        printf("  本板 PSRAM 可存 RGB565 720p 帧数: %" PRIu32 "\n",
               (uint32_t)(psram_free / (size_t)(1280 * 720 * 2)));
    }
    printf("\n");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));
        printf("[心跳] PSRAM 空闲 %" PRIu32 " 字节\n",
               (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }
}