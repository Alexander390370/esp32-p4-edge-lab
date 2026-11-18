# ESP32-P4 边缘计算实验室

<p align="center">
  <img src="https://img.shields.io/badge/Chip-ESP32--P4-4D6BFE?logo=espressif&logoColor=white" alt="ESP32-P4">
  <img src="https://img.shields.io/badge/Core-Dual--RISC--V%20400MHz-red" alt="RISC-V">
  <img src="https://img.shields.io/badge/PSRAM-32MB%20Verified-green" alt="PSRAM">
  <img src="https://img.shields.io/badge/YOLO-14.3%20FPS%20%2F%2070ms-orange" alt="YOLO">
  <img src="https://img.shields.io/badge/WiFi-via%20C6-blue" alt="WiFi">
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Build-ESP--IDF%20v5.5.5-orange?logo=espressif&logoColor=white" alt="ESP-IDF">
  <img src="https://img.shields.io/badge/License-MIT-yellow.svg" alt="License">
</p>

> 硬件：**Waveshare ESP32-P4-NANO**（16MB Flash / 32MB PSRAM / MIPI-DSI / MIPI-CSI / ES8311 / microSD / 以太网）
>
> 记录从一块"上电只吐 ROM 日志"的空板，到**跑通 YOLO 实测 14.3 FPS** 的完整过程。
> 这个仓库不写"官方文档里已经有的东西"，只记 **实际动手时踩到的坑和验证过的数据**。

---

## ⚡ 核心数据速查

| 指标 | 实测值 | 说明 |
|---|---|---|
| **YOLO 推理帧率** | **14.30 FPS** | 69.95 ms/帧，抖动仅 0.1ms |
| **单帧延迟** | **69.95 ms** | 640×480 输入（含 19ms 缩放） |
| **PSRAM** | **32 MB** | 读写验证通过，200MHz |
| **Flash** | **16 MB** | GigaDevice |
| **WiFi** | **-42 dBm** | IP 10.35.0.80，连接耗时 3.76s |
| **内部可用堆** | **574 KB** | 不是数据手册的 768KB |
| **模型大小** | **567.7 KB** | espdl 格式（INT8 量化） |
| **PSRAM 占用** | **1.66 MB** | 32MB 里用了 5% |

**对比旧方案：PC 跑 4B 多模态模型需 25-30 秒 → P4 跑 YOLO 只要 70 毫秒，快约 400 倍。**

---

## 一、先搞清楚 P4 是什么

### 它和 S3 的根本区别

很多人以为 P4 是"S3 的升级版"，**这个理解是错的**。最关键的差别：

> **ESP32-P4 没有内置 Wi-Fi 和蓝牙。**

这是芯片设计上的取舍：乐鑫把无线射频整个拿掉，换来的是**双核 400MHz RISC-V + 32MB PSRAM + MIPI 接口 + H.264 硬编码**。

如果你想联网，必须在板上额外挂一颗 **ESP32-C6** 做无线协处理器，通过 SDIO 通信。

### 参数对比

| | ESP32-S3 | ESP32-P4 |
|---|---|---|
| 内核 | Xtensa LX7 双核 | **RISC-V 双核** |
| 主频 | 240 MHz | **400 MHz**（实测） |
| 片上 SRAM | 512 KB | **768 KB** |
| 实测可用堆 | 约 320 KB（DRAM） | **574 KB** |
| 外部 PSRAM | 最大 8 MB | **32 MB**（实测） |
| Wi-Fi | 内置 | **❌ 无**（需外挂 C6） |
| 蓝牙 | 内置 | **❌ 无**（需外挂 C6） |
| 摄像头接口 | DVP（并口） | **MIPI-CSI** + DVP |
| 显示屏接口 | RGB/8080/SPI | **MIPI-DSI** + RGB |
| 视频编码 | ❌ | **H.264 硬件编码 1080p@30** |
| USB | 1.1 FS | **2.0 HS** |
| 典型用途 | 通用 IoT、语音 | **边缘视觉、HMI、视频** |

### 什么时候选 P4，什么时候选 S3

**选 P4：**

- 要接**高分辨率屏幕**（720p 以上，MIPI-DSI）
- 要接**摄像头做视觉**（MIPI-CSI，比 DVP 快得多）
- 要做**视频编码/推流**（H.264 硬件编码，CPU 几乎不占）
- 需要**大内存**（32MB PSRAM 能跑完整帧缓冲）
- 不需要 Wi-Fi，或者愿意外挂 C6

**选 S3：**

- 需要 Wi-Fi/蓝牙，且不想外挂芯片
- 项目简单，不需要高分辨率显示
- 成本敏感
- 生态成熟度优先（S3 的 Arduino 支持更完善）

**一句话：P4 是边缘视觉/多媒体芯片，不是通用 IoT 芯片。**

---

## 二、这块板子到手时的状态

### 现象

插上 USB，打开串口（115200），复位后只能看到这些：

```
ESP-ROM:esp32p4-eco7-20260109
Build:Jan  9 2026
rst:0x1 (POWERON),boot:0x30f (SPI_FAST_FLASH_BOOT)
SPI mode:DIO, clock div:1
load:0x4ffb60d0,len:0x173c
load:0x4ffac2c0,len:0x1064
load:0x4ffaefc0,len:0x3650
entry 0x4ffac2ca
```

然后就没了。**没有进入应用程序，没有任何用户代码输出。**

### 这说明了什么

| 观察 | 结论 |
|---|---|
| `ESP-ROM:esp32p4-...` | 芯片是 P4，ROM 正常 |
| `boot:0x30f (SPI_FAST_FLASH_BOOT)` | 从 flash 启动，不是下载模式 |
| `load:0x...` 三行 | ROM 成功加载了二级引导程序 |
| `entry 0x4ffac2ca` | 跳转到应用程序入口 |
| **之后无输出** | **应用程序不存在或启动失败** |

### 结论

**硬件是好的，板子里没有可运行的固件。**

这不是故障，是空板/固件被擦的状态。需要自己烧一个。

---

## 三、快速开始

### 1. 装 ESP-IDF

P4 需要 **ESP-IDF v5.3 或更高**（v5.1 不支持 P4 的 MIPI 外设）。
**实测用 v5.5.5，对 P4 支持最好。**

```bash
# Windows 用官方安装器最省事
# https://dl.espressif.com/dl/esp-idf/
```

### 2. 进下载模式（关键）

P4 板子**自动复位进下载模式经常失败**，手动最可靠：

```
1. 按住 BOOT 键不放
2. 点一下 RST 键
3. 松开 BOOT 键
4. 此时串口会输出 "waiting for download"
```

如果看到 `waiting for download`，说明成功。

### 3. 编译烧录

```bash
cd examples/01-hello-uart
idf.py set-target esp32p4
idf.py build
idf.py -p COM12 -b 460800 flash monitor
```

`COM12` 换成你的实际端口（用 `tools/find-port.ps1` 自动找）。

### 4. 退不出 monitor

`Ctrl + ]`（不是 Ctrl+C）。

---

## 四、能跑通什么

| 能力 | 状态 | 实测数据 |
|---|---|---|
| 芯片识别 | ✅ | ESP32-P4 rev v3.2, 双核 400MHz |
| Flash 16MB | ✅ | 16777216 bytes |
| **PSRAM 32MB** | ✅ | 读写验证通过，200MHz |
| 分区表 | ✅ | 6MB app + 9.8MB storage |
| **C6 协处理器** | ✅ | SDIO 40MHz，WLAN over SDIO |
| **WiFi 连接** | ✅ | IP 10.35.0.80, RSSI -42dBm |
| **YOLO 推理** | ✅ | **14.3 FPS / 70ms**，抖动 0.1ms |
| HTTP 抓图 | ✅ | ESP32-CAM `/capture` 平均 285ms |
| BLE | ⚠️ 未测 | C6 固件 0.0.0 版本太旧 |
| 语音（ESP-SR） | ⏳ 待做 | — |

---

## 五、踩过的坑（摘要）

> 完整 18 条见 [docs/04-踩坑记录.md](docs/04-踩坑记录.md)

### 最坑的三个

| # | 坑 | 症状 | 根因 |
|---|---|---|---|
| 1 | **`CONFIG_SPIRAM` 配不上** | 配置项静默消失，**无任何报错** | `esp_psram` 组件没被加入构建，Kconfig 不加载 |
| 2 | **sdkconfig.defaults 中文注释** | 配置全部失效，**无报错** | 中文注释把配置项吞进同一行 |
| 3 | **C6 固件版本不匹配** | host=3.1.0 vs CP=0.0.0 | 出厂固件太旧，走兼容模式 |

### 其他常见坑

| # | 坑 | 解决 |
|---|---|---|
| 4 | Flash 默认只配 2MB | `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y` |
| 5 | bootloader 装不下 | `CONFIG_PARTITION_TABLE_OFFSET=0x9000` |
| 6 | 改 partitions.csv 不生效 | 先删 sdkconfig + build |
| 7 | esptool 连不上 | 手动 BOOT+RST 进下载模式 |
| 8 | 端口号每次都变 | 用 `tools/find-port.ps1` |
| 9 | ESP-IDF v5.5 头文件不再自动包含 | 手动 `#include "esp_chip_info.h"` |
| 10 | 非 ASCII SSID 变问号 | 用纯 ASCII，或从 NVS 读 |
| 11 | esp-ppq 要求 Python < 3.13 | 用 Python 3.12 |
| 12 | 组件 `override_path` 报错 | 删掉，改从注册表下载 |
| 13 | 模型文件名必须精确匹配 | 重命名为 `espdet_pico_imgH_imgW_custom.espdl` |
| 14 | 官方示例依赖官方板 BSP | 自己写最小 main |
| 15 | SDIO 单次读取超 1536 字节报错 | 分块读，每块 ≤1024 字节 |

---

## 六、实测数据

> 完整数据见 [docs/03-实测数据总表.md](docs/03-实测数据总表.md)

### 6.1 硬件规格（esptool 读取）

```
Chip model   : ESP32-P4
Silicon rev  : v3.2
Cores        : 2
CPU freq     : 400000000 Hz
Flash size   : 16777216 bytes (16.0 MB)
PSRAM total  : 33554432 bytes (32.0 MB)
PSRAM speed  : 200MHz
Internal heap: 588003 bytes (574.2 KB) free
```

### 6.2 WiFi 连接

```
I (7185) esp_netif_handlers: sta ip: 10.35.0.80, mask: 255.255.0.0
I (7185) WIFI_TEST: GOT IP: 10.35.0.80
```

| 指标 | 数值 |
|---|---|
| 发起连接 → 拿到 IP | **3.76 秒** |
| RSSI | **-42 到 -49 dBm** |
| 信道 | 11 |

### 6.3 YOLO 推理

```
[STAGE 1] JPEG decode    : 90 ms (640×480，一次性)
[STAGE 2] Model load     : 0 ms (Flash mmap)
[STAGE 3] First inference: 200 ms (warmup)
[STAGE 4] Steady-state（30 runs）:
          70 70 70 70 70 70 70 70 70 70 70 70 70 70 70
          70 70 70 70 70 70 70 70 70 70 70 70 70 70 70

Steady avg : 69.95 ms ==> 14.30 FPS
Jitter     : 0.10 ms
```

**与官方 51.4ms 差异已定位：**

```
70ms = 51ms（纯推理 224×224） + 19ms（640×480 → 224×224 缩放）
```

⚠️ **模型说明**：当前用的是 ESP-DL 官方的 `espdet_pico_224_224_cat` 模型，
`category=0` 是 **cat（猫）**，不是 person。做安防需自己训练人形模型。

---

## 七、示例代码

| 目录 | 内容 | 验证状态 |
|---|---|---|
| [01-hello-uart](examples/01-hello-uart/) | 最小可运行固件，验证烧录链路 | ✅ 已验证 |
| [02-bench-coremark](examples/02-bench-coremark/) | CoreMark 性能基准 | 待验证 |
| [03-psram-test](examples/03-psram-test/) | PSRAM 容量与带宽测试 | ✅ 已验证 32MB |
| [04-gpio-blink](examples/04-gpio-blink/) | GPIO 点灯 + 串口交互 | 待验证 |

---

## 八、文档

| 文档 | 内容 |
|---|---|
| [docs/01-P4与S3选型.md](docs/01-P4与S3选型.md) | 什么场景选 P4、什么场景选 S3，迁移成本评估 |
| [docs/02-P4-NANO硬件详解.md](docs/02-P4-NANO硬件详解.md) | 板载资源、烧录、空板日志解读、PSRAM 配置 |
| [docs/03-实测数据总表.md](docs/03-实测数据总表.md) | **所有实测数据汇总（含 YOLO 14.3 FPS）** |
| [docs/04-踩坑记录.md](docs/04-踩坑记录.md) | **18 条真实踩坑，按症状可查** |
| [docs/05-YOLO部署指南.md](docs/05-YOLO部署指南.md) | **从 .pt 到 P4 实机推理的完整链路** |
| [docs/06-分级推理架构.md](docs/06-分级推理架构.md) | **为什么分级、怎么分级、实测验证** |

---

## 九、工具

| 工具 | 用途 |
|---|---|
| [tools/diagnose.py](tools/diagnose.py) | 串口一键诊断：端口存在性 / 能否打开 / 有无输出 / 复位日志 / 芯片识别 |
| [tools/find-port.ps1](tools/find-port.ps1) | 自动找当前接上的 ESP32 串口（COM 号会变，别写死） |

```bash
# 诊断板子
pip install pyserial
python tools/diagnose.py COM12

# 找端口
powershell -ExecutionPolicy Bypass -File tools/find-port.ps1
```

---

## 十、完整配置（可直接复用）

### sdkconfig.defaults

```ini
# EXPERIMENTAL (required for P4 PSRAM + hosted)
CONFIG_IDF_EXPERIMENTAL_FEATURES=y

# FLASH 16MB
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="16MB"
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y

# PARTITION (bootloader 0x60C0 needs 0x9000)
CONFIG_PARTITION_TABLE_OFFSET=0x9000
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"

# CPU 400MHz
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_400=y

# PSRAM 32MB HEX 200MHz
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_HEX=y
CONFIG_SPIRAM_SPEED_200M=y
CONFIG_SPIRAM_USE_MALLOC=y
CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384
CONFIG_SPIRAM_MEMTEST=y
CONFIG_SPIRAM_BOOT_INIT=y

# WiFi via C6
CONFIG_ESP_WIFI_REMOTE_ENABLED=y
```

### partitions.csv

```csv
# Name,   Type, SubType, Offset,   Size
nvs,      data, nvs,     0xA000,   0x6000,
phy_init, data, phy,     0x10000,  0x1000,
factory,  app,  factory, 0x20000,  0x600000,
storage,  data, spiffs,  0x620000, 0x9D0000,
```

### main/CMakeLists.txt（关键：必须显式列 esp_psram）

```cmake
idf_component_register(SRCS "main.c"
    PRIV_REQUIRES spi_flash esp_psram esp_timer esp_hw_support
                 esp_wifi nvs_flash esp_netif esp_event
                 espressif__esp_hosted
    INCLUDE_DIRS "")
```

---

## 十一、参考文档

- [ESP32-P4 数据手册（中文）](https://documentation.espressif.com/esp32-p4_datasheet_cn.pdf)
- [ESP32-P4-Function-EV-Board 用户指南](https://docs.espressif.com/projects/esp-dev-kits/zh_CN/latest/esp32p4/esp32-p4-function-ev-board/user_guide.html)
- [Waveshare ESP32-P4-NANO Wiki](https://www.waveshare.com/wiki/ESP32-P4-Nano-StartPage)
- [ESP-DL 深度学习框架](https://github.com/espressif/esp-dl)
- [esp-detection 示例](https://github.com/espressif/esp-detection)
- [ESP32-P4 没有 Wi-Fi 怎么联网（C6 协处理器方案）](https://zhuanlan.zhihu.com/p/2093378503425127066)

---

## License

MIT
