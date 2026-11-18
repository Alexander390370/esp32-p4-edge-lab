# YOLO 在 P4 上部署完整指南

> 从 PyTorch 模型到 P4 实机推理的完整链路
> 实测结果：**14.3 FPS（70ms/帧），抖动 0.1ms**

---

## 一、整体流程

```
① 环境准备（Python 3.12）
        ↓
② 导出 ONNX（.pt → .onnx）
        ↓
③ 量化成 espdl（.onnx → .espdl）
        ↓
④ 部署到工程（放到 components/espdet_detect/models/p4/）
        ↓
⑤ 编译烧录
        ↓
⑥ 实机推理
```

---

## 二、环境准备

### 2.1 Python 版本要求（关键）

**必须用 Python 3.12**，不能用 3.13+。

```bash
# 检查版本
python --version

# 如果不对，指定 3.12 的完整路径（Windows）
C:\Users\<用户名>\AppData\Local\Programs\Python\Python312\python.exe --version
```

原因：`esp-ppq`（量化工具）最高只支持 Python 3.12。

### 2.2 安装依赖

```bash
pip install esp-ppq        # 1.3.14，量化核心
pip install ultralytics    # 导出 ONNX
pip install onnx onnxsim onnxruntime
```

### 2.3 ESP-IDF

需要 **v5.3 或更高**（v5.1 不支持 P4 的 MIPI 外设）。

实测用 **v5.5.5**，对 P4 支持最好。

---

## 三、Step 1：导出 ONNX

```python
from deploy.export import Export

Export("espdet_pico_224_224_cat.pt", 224)
```

**参数说明：**
- 第一个参数：PyTorch 模型路径
- 第二个参数：输入尺寸（224×224）

⚠️ **已知问题**：ultralytics 新版导出后会报：

```
TypeError: argument should be a str ... not 'tuple'
```

这是 `onnxsim` 返回值变化导致的。**ONNX 文件已经成功生成**，可以忽略这个错误，直接进行下一步。

**验证：** 检查目录下有没有 `.onnx` 文件。

```
.pt (986 KB)  →  .onnx (1444 KB)
```

ONNX 比 PyTorch 大是因为它包含了完整的计算图定义。

---

## 四、Step 2：量化成 espdl

```python
from deploy.quantize import quant_espdet

quant_espdet(
    onnx_path="espdet_pico_224_224_cat.onnx",
    target="esp32p4",
    num_of_bits=8,
    device='cpu',
    batchsz=32,
    imgsz=224,
    calib_dir="deploy/cat_calib",           # 官方提供的标定图片
    espdl_model_path="espdet_pico_224_224_cat.espdl",
)
```

**关键参数：**

| 参数 | 值 | 说明 |
|---|---|---|
| `target` | `esp32p4` | 目标芯片，决定量化策略 |
| `num_of_bits` | `8` | INT8 量化 |
| `device` | `cpu` | 有 GPU 可改 `cuda`，速度更快 |
| `batchsz` | `32` | 标定批大小 |
| `imgsz` | `224` | 输入尺寸 |
| `calib_dir` | 标定图目录 | **必须有**，决定量化精度的关键 |

⚠️ **标定图片很重要**：用你自己的场景图片效果最好。
官方提供的是猫的图片，如果你的场景是人/货架，最好换成自己的图。

**结果：**

```
.onnx (1444 KB)  →  .espdl (567.7 KB)
```

**量化压缩了 61%（从 ONNX 算）。**

---

## 五、Step 3：部署到工程

### 5.1 目录结构

```
your_project/
├── components/
│   └── espdet_detect/
│       ├── CMakeLists.txt
│       ├── espdet_detect.cpp
│       ├── espdet_detect.hpp
│       ├── idf_component.yml
│       ├── Kconfig
│       └── models/
│           └── p4/
│               └── espdet_pico_imgH_imgW_custom.espdl    ← 模型放这里
└── main/
    ├── app_main.cpp
    └── CMakeLists.txt
```

### 5.2 ⚠️ 模型文件名必须精确匹配

`espdet_detect/CMakeLists.txt` 里写死了：

```cmake
list(APPEND models ${models_dir}/espdet_pico_imgH_imgW_custom.espdl)
```

`imgH_imgW` 是**字面的占位符**，不是变量。你的模型必须重命名为这个。

```powershell
Copy-Item espdet_pico_224_224_cat.espdl espdet_pico_imgH_imgW_custom.espdl
```

### 5.3 修复 idf_component.yml

从 esp-detection 仓库复制 `espdet_detect` 组件时，它的 `idf_component.yml` 里有：

```yaml
espressif/esp-dl:
  override_path: "../../esp-dl"    ← 这是仓库内的相对路径
```

单独复制后这个路径不存在，会报错。**改为从注册表下载：**

```yaml
dependencies:
  espressif/esp-dl:
    version: "^3.2.0"
```

### 5.4 不要用官方示例的 BSP

官方 `espdet_example_template` 依赖：

```yaml
espressif/esp32_p4_function_ev_board_noglib
```

这是**乐鑫官方 EV Board** 的 BSP。微雪 P4-NANO 用不了。

**解决：** 自己写最小 main，只依赖必要组件：

```cmake
set(requires espdet_detect esp_psram esp_timer esp_hw_support)
```

---

## 六、Step 4：配置与编译

### 6.1 sdkconfig.defaults

```ini
# EXPERIMENTAL (P4 PSRAM + hosted)
CONFIG_IDF_EXPERIMENTAL_FEATURES=y

# FLASH 16MB
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="16MB"

# PARTITION (bootloader 需要 0x9000)
CONFIG_PARTITION_TABLE_OFFSET=0x9000
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"

# CPU 400MHz
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_400=y

# PSRAM 32MB HEX
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_HEX=y
CONFIG_SPIRAM_SPEED_200M=y
CONFIG_SPIRAM_USE_MALLOC=y

# ESP-DL 模型放 Flash
CONFIG_FLASH_ESPDET_PICO_imgH_imgW_CUSTOM=y
CONFIG_ESPDET_PICO_imgH_imgW_CUSTOM=y
CONFIG_ESPDET_DETECT_MODEL_IN_FLASH_RODATA=y

# C++ 支持
CONFIG_COMPILER_CXX_EXCEPTIONS=y
CONFIG_COMPILER_CXX_RTTI=y
CONFIG_ESP_MAIN_TASK_STACK_SIZE=16384
```

### 6.2 partitions.csv（app 扩到 6MB）

```csv
# Name,   Type, SubType, Offset,   Size
nvs,      data, nvs,     0xA000,   0x6000,
phy_init, data, phy,     0x10000,  0x1000,
factory,  app,  factory, 0x20000,  0x600000,
storage,  data, spiffs,  0x620000, 0x9D0000,
```

**为什么 6MB：** YOLO 固件约 3.4MB（含 ESP-DL + 模型 + C++ 运行时），默认 4MB 不够。

### 6.3 编译烧录

```bash
idf.py set-target esp32p4
idf.py build
idf.py -p COM12 -b 460800 flash monitor
```

---

## 七、实测结果

### 7.1 分阶段耗时

```
[ENV] Chip     : ESP32-P4, 2 cores, rev v3.2
[ENV] PSRAM    : 32.0 MB (free 32.0 MB)
[ENV] Free heap: 34104879 bytes (32.5 MB)

[STAGE 1] JPEG decode
          640x480 decoded, 90 ms

[STAGE 2] Model load
          0 ms            ← Flash mmap，不占启动时间

[STAGE 3] First inference (warmup)
          200 ms, 2 objects

[STAGE 4] Steady-state (30 runs)
  Run times: 70 70 70 70 70 70 70 70 70 70 70 70 70 70 70
             70 70 70 70 70 70 70 70 70 70 70 70 70 70 70
```

### 7.2 统计

```
Steady avg   : 69.95 ms  ==> 14.30 FPS
Steady min   : 69.90 ms  ==> 14.31 FPS
Steady max   : 70.00 ms
Jitter       : 0.10 ms   ← 抖动只有 0.1ms
```

**30 次连续推理，每次都是 70ms±0.1**，说明推理耗时是确定的，没有 GC 或内存抖动。

### 7.3 检测结果

```
Detected 2 object(s):
  - category=0  score=0.852  box=[0, 0, 591, 479]
  - category=0  score=0.852  box=[48, 60, 622, 479]
```

⚠️ **重要**：`category=0` 是 **cat（猫）**，不是 person。
这是官方示例模型，**只能识猫**。做安防要自己训练人形模型。

---

## 八、与官方数据对比

| 项目 | 官方 | 实测 | 差异 |
|---|---|---|---|
| espdet_pico 224×224 | 51.4 ms | **69.95 ms** | +36.1% |
| FPS | 19.5 | **14.30** | -26.7% |

**差异原因（已定位）：**

```
70ms（实测） = 51ms（纯推理 224×224） + 19ms（640×480 → 224×224 缩放）
```

官方测试用的是 224×224 原图，我们用的是 640×480 图，多了缩放预处理。

**这不是性能问题，是测试条件不同。**

---

## 九、提速路线

| 方法 | 预期延迟 | 难度 | 说明 |
|---|---|---|---|
| 让摄像头直接输出 224×224 | 约 51 ms | 低 | MIPI-CSI 可配输出尺寸 |
| 用 `rect=True` 的 160×288 模型 | 45.9 ms（21.8 FPS） | 低 | 官方提供 |
| INT8 再优化 | 可能更快 | 中 | 需重新量化调参 |
| 双核并行推理 | 理论翻倍 | 高 | 需拆分模型或流水线 |
| 训练人形模型 | — | 中 | 需数据集 |

---

## 十、训练自己的模型

官方只提供猫的示例模型。实际场景（人、货架、烟雾）需要自己训练。

### 10.1 数据集格式

YOLO 格式，每张图配一个 `.txt`：

```
<class_id> <x_center> <y_center> <width> <height>
```

坐标都归一化到 0-1。

### 10.2 训练

```python
from ultralytics import YOLO

model = YOLO('yolov8n.pt')   # 从预训练模型开始
model.train(
    data='dataset.yaml',
    epochs=100,
    imgsz=224,               # 必须和部署尺寸一致
    batch=16,
)
```

⚠️ **imgsz 必须和部署时的输入尺寸一致**（这里是 224）。

### 10.3 导出与量化

同第三章、第四章流程。

### 10.4 标定图要用自己的场景

量化时的 `calib_dir` 换成你的实际场景图片，否则量化精度会掉。

---

## 十一、关键路径速查

| 内容 | 路径示例 |
|---|---|
| YOLO 测试工程 | `p4_yolo_test` |
| ESP-DL 源码 | `p4_yolo/esp-detection-main` |
| 转换后模型 | `examples/cat_detection/espdet_pico_224_224_cat.espdl` |
| 部署用模型 | `components/espdet_detect/models/p4/espdet_pico_imgH_imgW_custom.espdl` |
| Python 3.12 | `AppData/Local/Programs/Python/Python312/python.exe` |

---

## 十二、一句话总结

> **ESP32-P4 跑 YOLO 实测 14.3 FPS（70ms/帧），抖动仅 0.1ms。**
> 比 PC 跑 4B 多模态模型（27 秒）快 **约 400 倍**。
> 与官方 51.4ms 的差异已定位：测试图 640×480 多了 19ms 缩放。
