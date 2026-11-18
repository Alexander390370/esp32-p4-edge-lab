# 查找当前接上的 ESP32 串口
# 用法: .\find-port.ps1
#
# 为什么需要这个脚本：
#   同一个板子每次插电脑，Windows 分配的 COM 号可能变
#   （COM8 → COM12 → COM3 ...）
#   写死端口号的脚本会失效，用这个动态查

$ports = [System.IO.Ports.SerialPort]::GetPortNames()

if ($ports.Count -eq 0) {
    Write-Host "没有检测到任何串口设备" -ForegroundColor Red
    Write-Host "请检查："
    Write-Host "  1. USB 线是否插好（要能传数据，不能是纯充电线）"
    Write-Host "  2. 板子是否上电"
    Write-Host "  3. 驱动是否安装"
    exit 1
}

Write-Host "当前串口：" -ForegroundColor Cyan

$espPorts = @()
foreach ($p in $ports) {
    $dev = Get-PnpDevice -Class Ports -PresentOnly -ErrorAction SilentlyContinue |
           Where-Object { $_.FriendlyName -match $p }
    $name = if ($dev) { $dev.FriendlyName } else { "未知设备" }

    # 乐鑫、沁恒、CP210x 都可能是 ESP32 板子
    $isEsp = $name -match 'CH34|CP210|USB 串行设备|Silicon Labs'
    $mark = if ($isEsp) { "  ← 可能是 ESP32" } else { "" }
    $color = if ($isEsp) { "Green" } else { "Gray" }

    Write-Host ("  {0,-8} {1}{2}" -f $p, $name, $mark) -ForegroundColor $color

    if ($isEsp) { $espPorts += $p }
}

Write-Host ""
if ($espPorts.Count -eq 1) {
    Write-Host "推荐使用: $($espPorts[0])" -ForegroundColor Yellow
    Write-Host "烧录命令: idf.py -p $($espPorts[0]) flash monitor" -ForegroundColor Yellow
} elseif ($espPorts.Count -gt 1) {
    Write-Host "检测到多个可能的端口，请手动确认：" -ForegroundColor Yellow
    $espPorts | ForEach-Object { Write-Host "  $_" }
} else {
    Write-Host "没找到疑似 ESP32 的端口" -ForegroundColor Red
}