/*
 * ESP32-P4 性能基准测试
 *
 * 为什么不直接用 CoreMark 源码：
 *   官方 CoreMark 移植比较重，这里用几个自己写的微基准，
 *   覆盖实际项目最常遇到的场景，数据更有参考意义。
 *
 * 测试项：
 *   1. 整数运算   —— 加乘除法循环
 *   2. 浮点运算   —— sin/cos/sqrt
 *   3. 内存拷贝   —— memcpy 内部 SRAM vs PSRAM
 *   4. 函数调用   —— 递归斐波那契（测调用开销）
 *   5. 分支预测   —— 条件跳转密集循环
 *
 * 单核 vs 双核：
 *   默认单核运行。要测双核，把 RUN_DUAL_CORE 改成 1，
 *   会起两个任务分别跑同一份基准，比较总耗时。
 */

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_cpu.h"

#define RUN_DUAL_CORE  0

static int64_t now_us(void) { return esp_timer_get_time(); }

// ---------- 1. 整数运算 ----------
static volatile uint32_t g_sink = 0;

static void bench_int(void)
{
    const uint32_t N = 5000000;
    int64_t t0 = now_us();

    uint32_t a = 1, b = 3;
    for (uint32_t i = 0; i < N; i++) {
        a = a * 1103515245u + 12345u;
        b = (a >> 7) ^ (b + i);
        if (b & 1) a += b; else a -= b;
    }
    g_sink = a + b;

    int64_t t1 = now_us();
    double sec = (t1 - t0) / 1000000.0;
    printf("  [整数] %" PRIu32 " 次迭代, %.1f ms, %.2f M次/秒\n",
           N, sec * 1000, N / sec / 1000000.0);
}

// ---------- 2. 浮点运算 ----------
static void bench_float(void)
{
    const uint32_t N = 1000000;
    int64_t t0 = now_us();

    double acc = 0.0;
    for (uint32_t i = 0; i < N; i++) {
        double x = i * 0.0001;
        acc += sin(x) * cos(x) + sqrt(x + 1.0);
    }
    g_sink = (uint32_t)acc;

    int64_t t1 = now_us();
    double sec = (t1 - t0) / 1000000.0;
    printf("  [浮点] %" PRIu32 " 次 sin/cos/sqrt, %.1f ms, %.2f M次/秒\n",
           N, sec * 1000, N / sec / 1000000.0);
    printf("         （P4 无 FPU 硬件双精度，双精度会明显慢于单精度）\n");
}

// ---------- 3. 内存拷贝 ----------
static void bench_mem(size_t total_bytes, const char *label, uint32_t caps)
{
    uint8_t *src = heap_caps_malloc(total_bytes, caps);
    uint8_t *dst = heap_caps_malloc(total_bytes, caps);
    if (!src || !dst) {
        printf("  [内存] %s 分配失败，跳过\n", label);
        if (src) heap_caps_free(src);
        if (dst) heap_caps_free(dst);
        return;
    }

    memset(src, 0xAA, total_bytes);
    const int REPEAT = 20;
    int64_t t0 = now_us();
    for (int i = 0; i < REPEAT; i++) {
        memcpy(dst, src, total_bytes);
    }
    int64_t t1 = now_us();

    double sec = (t1 - t0) / 1000000.0;
    double mb = total_bytes / 1024.0 / 1024.0;
    printf("  [内存] %s memcpy %.0f MB x %d, %.1f ms, %.1f MB/s\n",
           label, mb, REPEAT, sec * 1000, mb * REPEAT / sec);

    heap_caps_free(src);
    heap_caps_free(dst);
}

// ---------- 4. 函数调用 ----------
static uint32_t fib(uint32_t n)
{
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}

static void bench_call(void)
{
    int64_t t0 = now_us();
    uint32_t r = fib(30);
    int64_t t1 = now_us();

    double sec = (t1 - t0) / 1000000.0;
    printf("  [递归] fib(30) = %" PRIu32 ", %.1f ms\n", r, sec * 1000);
    printf("         （约 270 万次函数调用，测调用开销）\n");
    g_sink = r;
}

// ---------- 5. 分支预测 ----------
static void bench_branch(void)
{
    const uint32_t N = 10000000;
    uint8_t buf[1000];
    for (int i = 0; i < 1000; i++) buf[i] = (i * 37) & 0xFF;

    int64_t t0 = now_us();
    uint32_t sum = 0;
    for (uint32_t i = 0; i < N; i++) {
        // 伪随机分支，模拟不可预测的条件跳转
        if (buf[i % 1000] > 127) sum += i;
        else sum -= i;
    }
    int64_t t1 = now_us();

    double sec = (t1 - t0) / 1000000.0;
    printf("  [分支] %" PRIu32 " 次条件跳转, %.1f ms, %.2f M次/秒\n",
           N, sec * 1000, N / sec / 1000000.0);
    g_sink = sum;
}

#if RUN_DUAL_CORE
static void bench_task(void *arg)
{
    bench_int();
    vTaskDelete(NULL);
}
#endif

void app_main(void)
{
    printf("\n");
    printf("========================================\n");
    printf("  ESP32-P4 性能基准\n");
    printf("========================================\n");

    printf("\n[芯片]\n");
    printf("  CPU  : %d 核 @ %" PRIu32 " MHz\n",
           esp_cpu_get_core_count(),
           esp_clk_cpu_freq() / 1000000);
    printf("  SRAM : 空闲 %" PRIu32 " 字节\n",
           (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    printf("  PSRAM: 空闲 %" PRIu32 " 字节\n",
           (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

#if RUN_DUAL_CORE
    printf("\n[双核模式] 起两个任务并行跑整数基准\n");
    int64_t t0 = now_us();
    xTaskCreatePinnedToCore(bench_task, "b0", 8192, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(bench_task, "b1", 8192, NULL, 5, NULL, 1);
    vTaskDelay(pdMS_TO_TICKS(8000));
    int64_t t1 = now_us();
    printf("  双核总耗时: %.1f ms\n", (t1 - t0) / 1000.0);
#else
    printf("\n[单核模式]\n");
    bench_int();
    bench_float();
    bench_mem(1024 * 1024, "SRAM ", MALLOC_CAP_INTERNAL);
    bench_mem(1024 * 1024, "PSRAM", MALLOC_CAP_SPIRAM);
    bench_call();
    bench_branch();
#endif

    printf("\n========================================\n");
    printf("  基准完成\n");
    printf("========================================\n");
    printf("\n把上面的数据填进 README 的实测表格。\n\n");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));
        printf("[心跳] 空闲堆 %" PRIu32 " 字节\n",
               (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    }
}