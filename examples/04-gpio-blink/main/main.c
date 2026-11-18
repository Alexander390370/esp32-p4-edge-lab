/*
 * ESP32-P4 GPIO 输出 + 串口交互验证
 *
 * 目的：
 *   1. 验证 GPIO 输出正常（用串口反馈间接验证）
 *   2. 验证串口输入（不只是输出）
 *   3. 提供一个可手动控制的 GPIO，方便接外部 LED 或继电器测试
 *
 * ⚠️ 关于 P4-NANO 的板载 LED：
 *   微雪 P4-NANO 板上那个 USER-LED 是「电源指示灯」，
 *   直接接电源，不受 GPIO 控制 —— 点它没用。
 *
 *   所以这个例子默认 GPIO 悬空，通过串口打印电平变化来验证逻辑。
 *   如果你要真实看到灯亮，外接一个 LED：
 *
 *        GPIO25 ── 220Ω ── LED正极 ── LED负极 ── GND
 *
 *   然后把下面的 LED_GPIO 改成你实际接的引脚。
 *
 * 串口命令（在 monitor 里直接输入）：
 *   1     拉高
 *   0     拉低
 *   t     切换
 *   b     开始/停止翻转
 *   s     打印状态
 *
 * 关于 P4 的 GPIO：
 *   - P4 的 GPIO 编号和 S3 完全不同，不能照抄 S3 的引脚
 *   - GPIO0 是 strapping 引脚（启动模式），别占用
 *   - GPIO 电压 3.3V，单脚驱动能力约 20mA，不能直驱电机/大功率负载
 *   - MIPI-DSI/CSI 用的是专用差分引脚，不要拿来做普通 GPIO
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"

// 改成你实际外接 LED 的引脚。
// 默认用 GPIO25（P4-NANO 排针上引出，且不是 strapping 脚）。
#define LED_GPIO        25
#define BLINK_PERIOD_MS 500

static bool g_blinking = false;

static void led_set(int on)
{
    gpio_set_level(LED_GPIO, on ? 1 : 0);
    printf("  GPIO%d -> %s\n", LED_GPIO, on ? "高电平 (3.3V)" : "低电平 (0V)");
}

static void blink_task(void *arg)
{
    while (1) {
        if (g_blinking) {
            gpio_set_level(LED_GPIO, 1);
            vTaskDelay(pdMS_TO_TICKS(BLINK_PERIOD_MS));
            gpio_set_level(LED_GPIO, 0);
            vTaskDelay(pdMS_TO_TICKS(BLINK_PERIOD_MS));
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

static void cmd_task(void *arg)
{
    printf("\n可用命令（输入后按回车）：\n");
    printf("  1  点灯\n");
    printf("  0  灭灯\n");
    printf("  t  切换\n");
    printf("  b  开始/停止闪烁\n");
    printf("  s  打印引脚状态\n");
    printf("\n> ");

    char line[64];
    int pos = 0;

    while (1) {
        int c = getchar();
        if (c == EOF || c < 0) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (c == '\r' || c == '\n') {
            if (pos > 0) {
                line[pos] = 0;
                printf("\n");

                if (strcmp(line, "1") == 0) {
                    g_blinking = false;
                    led_set(1);
                } else if (strcmp(line, "0") == 0) {
                    g_blinking = false;
                    led_set(0);
                } else if (strcmp(line, "t") == 0) {
                    g_blinking = false;
                    int cur = gpio_get_level(LED_GPIO);
                    led_set(!cur);
                } else if (strcmp(line, "b") == 0) {
                    g_blinking = !g_blinking;
                    printf("  闪烁 %s\n", g_blinking ? "开始" : "停止");
                } else if (strcmp(line, "s") == 0) {
                    printf("  GPIO%d 电平: %d\n", LED_GPIO,
                           gpio_get_level(LED_GPIO));
                    printf("  闪烁状态  : %s\n", g_blinking ? "开" : "关");
                } else {
                    printf("  未知命令: %s\n", line);
                }

                pos = 0;
                printf("\n> ");
            }
        } else if (pos < (int)sizeof(line) - 1) {
            line[pos++] = (char)c;
        }
    }
}

void app_main(void)
{
    printf("\n");
    printf("========================================\n");
    printf("  ESP32-P4 GPIO 点灯\n");
    printf("========================================\n");
    printf("LED 引脚: GPIO%d\n", LED_GPIO);

    // ---- 配置 GPIO ----
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode         = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        printf("GPIO 配置失败: %s\n", esp_err_to_name(err));
        return;
    }
    printf("GPIO 配置成功\n");

    // 开机先闪三下，确认硬件工作
    printf("开机自检：电平翻转三下...\\n");
    for (int i = 0; i < 3; i++) {
        gpio_set_level(LED_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(150));
        gpio_set_level(LED_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(150));
    }
    printf("自检完成\n");

    xTaskCreate(blink_task, "blink", 2048, NULL, 3, NULL);
    xTaskCreate(cmd_task, "cmd", 4096, NULL, 4, NULL);
}