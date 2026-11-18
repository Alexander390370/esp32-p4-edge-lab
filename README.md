# ESP32-P4 边缘计算实验室

<p align="center">
  <img src="https://img.shields.io/badge/Chip-ESP32--P4-4D6BFE?logo=espressif&logoColor=white" alt="ESP32-P4">
  <img src="https://img.shields.io/badge/Core-Dual--RISC--V%20400MHz-red" alt="RISC-V">
  <img src="https://img.shields.io/badge/PSRAM-Up%20to%2032MB-green" alt="PSRAM">
  <img src="https://img.shields.io/badge/Camera-MIPI--CSI-9B59B6" alt="MIPI-CSI">
  <img src="https://img.shields.io/badge/Display-MIPI--DSI-E67E22" alt="MIPI-DSI">
  <img src="https://img.shields.io/badge/Codec-H.264%201080p30-3498DB" alt="H.264">
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Build-ESP--IDF%20v5.5-orange?logo=espressif&logoColor=white" alt="ESP-IDF">
  <img src="https://img.shields.io/badge/License-MIT-yellow.svg" alt="License">
</p>

> 硬件：**Waveshare ESP32-P4-NANO**（16MB Flash / 32MB PSRAM / MIPI-DSI / MIPI-CSI / ES8311 / microSD / 以太网）
>
> 记录从一块"上电只吐 ROM 日志"的空板，到能跑通 UART、PSRAM、GPIO、性能基准的完整过程。
> 这个仓库不写"官方文档里已经有的东西"，只记**实际动手时踩到的坑和验证过的数据**。

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
| 主频 | 240 MHz | **360 MHz**（P4-NANO 实测） |
| 片上 SRAM | 512 KB | **768 KB** |
| 外部 PSRAM | 最大 8 MB | **最大 32 MB** |
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

```bash
# Windows 用官方安装器最省事
# https://dl.espressif.com/dl/esp-idf/
```

装完确认：

```bash
idf.py --version
# 应该看到 v5.3 以上
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
idf.py -p COM12 flash monitor
```

`COM12` 换成你的实际端口。

### 4. 退不出 monitor

`Ctrl + ]`（不是 Ctrl+C）。

---

## 四、踩过的坑

### 坑 1：板子上电只输出 ROM 日志就停住

**不是坏了**，是没固件。

判断方法：看到 `ESP-ROM:esp32p4-...` 加三行 `load:` 说明 ROM 阶段完全正常，
问题一定出在"应用程序"这一层。

### 坑 2：esptool 发 SYNC 命令没回应

```
SYNC 回应 (0 字节): (空)
MAC 命令回应: (空)
```

说明芯片**没进下载模式**。ROM 在正常启动模式下不响应 esptool 协议。

解决：手动按 BOOT + RST（见上面第 2 步）。

### 坑 3：串口号会变

同一个板子，昨天是 COM8，今天插上变成 COM12。

Windows 每次重新枚举 USB 设备时可能分配新端口号。**不要写死端口**，每次用
`platformio device list` 或 `idf.py -p PORT` 时先确认。

### 坑 4：P4 没有 Wi-Fi，别照着 S3 的教程写

如果你从 S3 项目迁移代码，会遇到：

```
undefined reference to `esp_wifi_init`
```

这不是配置问题，是**P4 根本没有 WiFi 外设**。

两条路：
1. 板上有 C6 协处理器 → 用 [esp_hosted](https://github.com/espressif/esp-hosted) 通过 SDIO 通信
2. 板子没有 C6 → 加一个 ESP32-C6/C3 模块，串口或 SPI 通信

### 坑 5：PSRAM 配置要选对型号

P4 支持多种 PSRAM，配置错会启动失败或容量识别错误。

`menuconfig` → `Component config` → `ESP PSRAM`：

```
SPIRAM_MODE_OCT      → 8 线 PSRAM（速度快，P4 常用）
SPIRAM_MODE_QUAD     → 4 线
SPIRAM_SPEED_200M    → 200MHz
```

---

## 五、实测数据

> 数据来自本仓库的 examples，硬件为 ESP32-P4 Function EV Board。
> 每次实测后更新这张表。

### CoreMark 性能

| 项目 | 数值 | 备注 |
|---|---|---|
| 单核 | 待测 | |
| 双核 | 待测 | |

### PSRAM

| 项目 | 数值 |
|---|---|
| 总容量 | 待测 |
| 可用容量 | 待测 |
| 读写带宽 | 待测 |

---

## 六、示例代码

| 目录 | 内容 | 验证状态 |
|---|---|---|
| [01-hello-uart](examples/01-hello-uart/) | 最小可运行固件，验证烧录链路 | 待验证 |
| [02-bench-coremark](examples/02-bench-coremark/) | CoreMark 性能基准 | 待验证 |
| [03-psram-test](examples/03-psram-test/) | PSRAM 容量与带宽测试 | 待验证 |
| [04-gpio-blink](examples/04-gpio-blink/) | GPIO 点灯 + 串口交互 | 待验证 |

## 六点六、文档

| 文档 | 内容 |
|---|---|
| [docs/01-P4与S3选型.md](docs/01-P4与S3选型.md) | 什么场景选 P4、什么场景选 S3，迁移成本评估 |
| [docs/02-P4-NANO硬件详解.md](docs/02-P4-NANO硬件详解.md) | 板载资源、烧录、空板日志解读、PSRAM 配置 |

---

## 六点五、工具

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

## 七、参考文档


- [ESP32-P4 数据手册（中文）](https://documentation.espressif.com/esp32-p4_datasheet_cn.pdf)
- [ESP32-P4-Function-EV-Board 用户指南](https://docs.espressif.com/projects/esp-dev-kits/zh_CN/latest/esp32p4/esp32-p4-function-ev-board/user_guide.html)
- [乐鑫发布 ESP32-P4 官方公告](https://www.espressif.com/zh-hans/news/ESP32-P4)
- [ESP32-P4 没有 Wi-Fi 怎么联网（C6 协处理器方案）](https://zhuanlan.zhihu.com/p/2093378503425127066)

---

## License

MIT
