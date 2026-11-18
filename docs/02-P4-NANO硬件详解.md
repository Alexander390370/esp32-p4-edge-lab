# ESP32-P4-NANO 板子详解

> 板子：Waveshare ESP32-P4-NANO
> 芯片：ESP32-P4NRW32X（双核 HP RISC-V + 低功耗核）

## 一、和官方 EV Board 的区别

买开发板时容易搞混。**P4-NANO 和乐鑫官方的 ESP32-P4-Function-EV-Board 不是同一块板**：

| 项目 | 乐鑫官方 EV Board | **P4-NANO** |
|---|---|---|
| 主控型号 | ESP32-P4 | **ESP32-P4NRW32X** |
| 主频 | 400 MHz | **360 MHz** |
| Flash | 16 MB | 16 MB |
| PSRAM | 32 MB | 32 MB |
| 屏幕接口 | MIPI-DSI | **双通道 MIPI-DSI** |
| 摄像头 | MIPI-CSI | MIPI-CSI（官方示例配 OV5647） |
| 音频 | 无 | **ES8311 codec**（模拟麦 + 喇叭） |
| 存储 | 无 | **microSD（4-bit SDMMC）** |
| 网络 | 无 | **以太网** |
| 触摸 | 无 | **GT911**（I2C） |
| USB | 2.0 | USB 2.0 HS |

**关键差异：P4-NANO 自带以太网口。**

这一点很重要 —— 因为 P4 没有 WiFi，但**有网线就能联网**。
所以如果你不需要无线，P4-NANO 可以直接插网线用，不需要外挂 C6。

## 二、板载资源一览

```
┌─────────────────────────────────────────────┐
│  ESP32-P4-NANO                              │
│                                             │
│  ┌───────────┐                              │
│  │ ESP32-P4  │  双核 RISC-V 360MHz           │
│  │  NRW32X   │  768KB SRAM                  │
│  └───────────┘  +32MB PSRAM（封装内）        │
│                                             │
│  Flash    : 16 MB（外部 QSPI）               │
│  PSRAM    : 32 MB（封装内 16-line）          │
│                                             │
│  显示     : 双通道 MIPI-DSI                  │
│  摄像     : MIPI-CSI                         │
│  触摸     : GT911（I2C）                     │
│  音频     : ES8311 codec                     │
│  存储     : microSD（4-bit SDMMC）           │
│  网络     : 以太网 PHY                        │
│  USB      : USB 2.0 HS（可当 UVC）           │
│  扩展     : GPIO 排针                        │
└─────────────────────────────────────────────┘
```

## 三、烧录与串口

### 串口位置

P4-NANO 通过 USB-C 连接，**板载 USB 转串口芯片**（CH343 或类似）。
插上电脑后在设备管理器里能看到 `USB-Enhanced-SERIAL CH343 (COMx)`。

⚠️ **端口号会变**。同一个板子，这次插是 COM12，下次可能是 COM8。
每次烧录前先确认。

```powershell
# 查当前端口
[System.IO.Ports.SerialPort]::GetPortNames()

# 或者用 idf.py
idf.py -p COM12 flash monitor
```

### 进入下载模式

P4-NANO **自动复位进下载模式经常不灵**，手动最可靠：

```
1. 按住 BOOT 键
2. 点一下 RST 键
3. 松开 BOOT
```

成功的话串口会输出：

```
waiting for download
```

如果没看到这行，就是没进下载模式，esptool 会报：

```
A fatal error occurred: Failed to connect to ESP32-P4: No serial data received.
```

### 烧录

```bash
cd examples/01-hello-uart
idf.py set-target esp32p4
idf.py build
idf.py -p COM12 flash monitor
```

退出 monitor：`Ctrl + ]`

## 四、空板现象

新板子或者固件被擦的板子，上电只会输出这些：

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

**然后就没了。没有进入应用程序。**

### 怎么解读这段日志

| 行 | 含义 |
|---|---|
| `ESP-ROM:esp32p4-eco7-20260109` | 芯片是 P4，ROM 版本 eco7 |
| `rst:0x1 (POWERON)` | 上电复位（不是看门狗、不是软件复位） |
| `boot:0x30f (SPI_FAST_FLASH_BOOT)` | 从 flash 快速启动，**不是下载模式** |
| `SPI mode:DIO, clock div:1` | flash 访问模式 |
| `load:0x4ffb60d0,len:0x173c` ×3 | ROM 加载二级引导程序到 SRAM |
| `entry 0x4ffac2ca` | 跳转到应用程序入口 |
| **之后无输出** | **应用程序不存在 / 启动失败** |

### 结论

**硬件是好的，问题是没固件。**

这不是故障。判断依据：如果 ROM 阶段（前面 7 行）能完整打印，说明芯片、flash、UART 全部正常。
问题一定在「应用程序」这一层，烧个固件就行。

## 五、PSRAM 说明

P4-NANO 的 32MB PSRAM 是**封装内（in-package）**的，不是外挂芯片。

芯片手册里的描述：

```
In-package PSRAM
  Data bus width : 16 bits
  Max clock      : 250 MHz
  Mode           : DDR（双沿传输）
  带宽           : 16 × 2 × 250 MHz = 8 Gbit/s（理论 1 GB/s）
```

**注意：这是理论峰值，实际用 memcpy 测出来会低不少**（cache miss、bank 冲突等因素）。
用 `examples/03-psram-test` 实测你自己的板子。

### menuconfig 关键项

```
Component config → ESP PSRAM
  [*] Support for external, SPI-connected RAM
      SPI RAM config
          Mode (QUAD/OCT)  →  选 OCT（16-line）
          SPI RAM clock speed  →  200MHz（保守稳定）
```

选错会启动失败或者容量识别错误。

## 六、P4-NANO 相比 EV Board 的优势

| 优势 | 说明 |
|---|---|
| **自带以太网** | P4 没 WiFi，但插网线就能联网，不用外挂 C6 |
| **自带 SD 卡槽** | 4-bit SDMMC，存视频/图片方便 |
| **自带音频 codec** | ES8311，能直接接麦克风和喇叭 |
| **自带触摸** | GT911，接屏就能做触控 HMI |
| **体积小** | NANO 尺寸，适合做产品原型 |

**如果你做的是视觉/多媒体设备，P4-NANO 比 EV Board 更实用。**

## 七、引脚注意事项

P4 的 GPIO 和 S3 完全不同，**不能照抄 S3 的引脚定义**。

### Strapping 引脚（启动时决定模式，别乱用）

| 引脚 | 作用 |
|---|---|
| GPIO0 | 启动模式选择 |
| GPIO其他 | 参考芯片手册 Boot Configurations 章节 |

### 常用外设引脚（P4-NANO 参考）

具体接线看微雪官方原理图，不同版本板子可能不一样。

**关键原则：**

1. **先查原理图再接线**，别猜
2. MIPI 差分线不能乱接（DSI/CSI 有专用引脚）
3. 电源引脚别接错（P4 有多个电压域：VDD_HP / VDD_IO / VDD_PSRAM）

## 八、参考资料

- [ESP32-P4 数据手册（中文）](https://documentation.espressif.com/esp32-p4_datasheet_cn.pdf)
- [Waveshare ESP32-P4-NANO Wiki](https://www.waveshare.com/wiki/ESP32-P4-Nano-StartPage)
- [P4-NANO 官方例程仓库](https://github.com/waveshareteam/ESP32-P4-NANO-WIFI6-DB)
- [ESP32-P4 芯片版本升级说明](https://www.espressif.com/zh-hans/news/ESP32_P4_v3.x_Upgrade)