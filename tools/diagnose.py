# -*- coding: utf-8 -*-
"""
ESP32-P4 串口诊断工具

用途：怀疑板子有问题时，跑一遍就能定位是硬件、固件还是通信问题。

检查项：
  1. 端口是否存在
  2. 端口能否打开
  3. 是否有任何输出（判断固件是否在跑）
  4. 复位后能否看到 ROM 日志（判断芯片是否活着）
  5. 能否进入下载模式（判断能否烧录）

用法：
  python diagnose.py COM12
  python diagnose.py            # 自动找端口
"""

import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("请先安装 pyserial:  pip install pyserial")
    sys.exit(1)


def find_port():
    """自动找疑似 ESP32 的端口"""
    ports = list(list_ports.comports())
    if not ports:
        return None

    # 优先找乐鑫、沁恒、CP210x
    for p in ports:
        desc = (p.description or "").lower()
        if any(k in desc for k in ("ch34", "cp210", "usb 串行", "usb-serial")):
            return p.device
    return ports[0].device


def check_port_exists(port):
    """检查 1：端口是否在系统里"""
    print(f"[1/5] 端口存在性 {port} ...", end=" ")
    ports = [p.device for p in list_ports.comports()]
    if port in ports:
        print("OK")
        return True
    print("失败")
    print(f"      当前可用端口: {ports if ports else '(无)'}")
    return False


def check_can_open(port):
    """检查 2：能否打开端口"""
    print(f"[2/5] 打开端口 ...", end=" ")
    try:
        s = serial.Serial(port, 115200, timeout=0.5)
        s.close()
        print("OK")
        return True
    except Exception as e:
        print("失败")
        print(f"      {type(e).__name__}: {e}")
        print("      可能原因：端口被别的程序占用（串口助手、monitor 没关）")
        return False


def check_output(port, seconds=6):
    """检查 3：不复位，看是否有输出（固件在跑的表现）"""
    print(f"[3/5] 静默监听 {seconds} 秒 ...", end=" ")
    try:
        s = serial.Serial(port, 115200, timeout=0.5)
        s.reset_input_buffer()
        lines = []
        t0 = time.time()
        while time.time() - t0 < seconds:
            data = s.readline()
            if data:
                t = data.decode("utf-8", "replace").rstrip()
                if t.strip():
                    lines.append(t)
        s.close()

        if lines:
            print(f"有输出（{len(lines)} 行）")
            for l in lines[:8]:
                print(f"      {l}")
            return True
        print("无输出")
        print("      固件可能没在运行，继续测复位")
        return False
    except Exception as e:
        print(f"异常 {e}")
        return False


def check_reset_boot(port, seconds=12):
    """检查 4：复位后能否看到 ROM 启动日志"""
    print(f"[4/5] 复位并抓启动日志 ...", end=" ")
    try:
        s = serial.Serial(port, 115200, timeout=0.5,
                          dsrdtr=False, rtscts=False)
        s.reset_input_buffer()

        # 标准复位序列
        s.setRTS(True)
        s.setDTR(False)
        time.sleep(0.05)
        s.setDTR(True)
        time.sleep(0.15)
        s.setDTR(False)
        time.sleep(0.05)
        s.setRTS(False)

        lines = []
        t0 = time.time()
        while time.time() - t0 < seconds:
            data = s.readline()
            if data:
                t = data.decode("utf-8", "replace").rstrip()
                if t.strip():
                    lines.append(t)
        s.close()

        if not lines:
            print("无输出")
            print("      芯片可能没上电，或者串口线只接了 TX 没接 RX")
            return False

        print(f"有输出（{len(lines)} 行）")
        for l in lines[:12]:
            print(f"      {l}")

        # 判断关键信息
        text = "\n".join(lines)
        print()
        if "ESP-ROM" in text:
            chip = "未知"
            if "esp32p4" in text:
                chip = "ESP32-P4"
            elif "esp32s3" in text:
                chip = "ESP32-S3"
            elif "esp32c6" in text:
                chip = "ESP32-C6"
            print(f"      芯片识别: {chip}")

        if "waiting for download" in text:
            print("      状态: 已进入下载模式")
        elif "entry 0x" in text:
            print("      状态: 正常启动流程（ROM 加载完跳转应用）")

        # 应用层标志
        app_markers = ["BOOT", "app_main", "Hello", "===", "心跳"]
        has_app = any(m in text for m in app_markers)
        if has_app:
            print("      ✅ 应用程序在运行")
        else:
            print("      ⚠️  没看到应用层输出，可能固件缺失或启动失败")

        return True
    except Exception as e:
        print(f"异常 {e}")
        return False


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else find_port()

    if not port:
        print("没找到任何串口设备")
        print("请检查 USB 线是否插好（要数据线，不是纯充电线）")
        sys.exit(1)

    print("=" * 52)
    print(f" ESP32-P4 串口诊断  {port}")
    print("=" * 52)
    print()

    r1 = check_port_exists(port)
    if not r1:
        sys.exit(1)

    r2 = check_can_open(port)
    if not r2:
        sys.exit(1)

    r3 = check_output(port)
    r4 = check_reset_boot(port)

    print()
    print("=" * 52)
    print(" 诊断结论")
    print("=" * 52)

    if r4:
        print("  硬件通信正常（芯片能启动、串口能收发）")
        if r3:
            print("  固件在运行")
        else:
            print("  固件未运行 → 需要烧录")
    else:
        print("  硬件通信异常 → 检查供电、USB 线、驱动")

    print()
    print("[5/5] 下载模式测试")
    print("      手动操作：按住 BOOT → 点 RST → 松开 BOOT")
    print("      成功的话串口应输出 'waiting for download'")
    print("      这样才能烧录固件")


if __name__ == "__main__":
    main()