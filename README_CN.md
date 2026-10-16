# A2DP Sink 转双路 BIS

[English](./README.md)

- 中级示例: ⭐⭐

## 示例简介

- 将手机 Classic A2DP Sink 音频桥接到一路 LE Audio Broadcast Source。默认 48 kHz、10 ms、155 字节 HQ、1 个 BIG、1 条 BIS。menuconfig 可选 16 kHz 单声道（1 条 BIS）或 16 kHz 立体声（1 个 BIG、2 条 BIS，前左/前右）。
- 基于 `esp_bt_audio` 与 GMF：`io_bt`(A2DP) → `aud_dec` → `copier` → 一路或两路（PCM 转换 → `aud_enc` → `io_bt`）。芯片支持时 PCM 转换默认使用硬件 `aud_asrc`，也可改为软件 `aud_ch_cvt` + `aud_rate_cvt`。双路 BIS 的 writer 按配置顺序提交 packet，controller 流控仍由 `esp_bt_audio` 处理。无本地 I2S 播放。

### 典型场景

- 手机 A2DP 音乐分享到 Auracast / BIS 耳机或广播接收板。
- 在 ESP32-S31 Korvo 类开发板上演示双模 BTDM，无需 SD 卡音源。

### 文件结构

```
a2dp_2_bis/
├── main/
│   ├── a2dp_bis_bridge.c
│   ├── a2dp_bis_bridge.h
│   ├── cmd_reg.c
│   ├── cmd_reg.h
│   ├── main.c
│   ├── pool_reg.c
│   ├── pool_reg.h
│   ├── CMakeLists.txt
│   ├── idf_component.yml
│   └── Kconfig.projbuild
├── CMakeLists.txt
├── partitions_esp32s31.csv
├── sdkconfig.defaults
├── README.md
└── README_CN.md
```

## 环境准备

### 硬件要求

- 带 PSRAM 的 ESP32-S31 开发板（默认板型：`esp32_s31_korvo_1`）
- 支持 A2DP 播放的手机
- LE Audio 广播接收端（运行 BMR/BIS Sink 的 ESP 板或兼容耳机）

### 默认 IDF 分支

ESP-IDF 6.2。`esp32s31` 是 preview target：

```
idf.py --preview set-target esp32s31
```

### 软件要求

- 组件管理器按 `main/idf_component.yml` 从 ESP Component Registry 拉取依赖
- `espressif/esp_bt_audio` 1.2 支持仅 BMS（广播源，不依赖 PACS 或 unicast）

## 编译与烧录

### 编译准备

```
cd /path/to/esp-idf
./install.sh
. ./export.sh
cd /path/to/a2dp_2_bis
idf.py --preview set-target esp32s31
idf.py build
idf.py -p PORT flash monitor
```

### 配置说明

menuconfig 关注项：

- `A2DP to Dual BIS Example`：设备/广播名、广播码、PCM 转换路径（默认硬件 `aud_asrc`，或软件 `aud_ch_cvt` + `aud_rate_cvt`）、LC3 preset（48 kHz 单声道，或 16 kHz 单声道/立体声）
- Bluetooth → LE Audio：`CONFIG_BT_BAP_BROADCAST_SRC_STREAM_COUNT=2`（协议栈上限；实际 BIS 数量跟随 LC3 preset）
- 保持 Bluedroid BTDM（Classic + LE）开启

默认 LC3 preset 为 `48 kHz / 10 ms / 155 B / HQ, mono (1 BIG / 1 BIS)`。16 kHz 立体声为 1 个 BIG、2 条 BIS。单声道 preset 将 A2DP 立体声混合为一条 BIS。

## 使用说明

1. 烧录并复位；设备使用 Kconfig 中的 Classic/广播名称广播。
2. 手机连接并播放音乐（A2DP Sink）。
3. A2DP 开播后，应用启动 LE 广播源，并按所选 preset 将音频送入一条或两条 BIS。
4. 接收端按广播名（及广播码，如已配置）同步。
5. UART 控制台提示符为 `A2DP-2-BIS >`，可用 sys 命令：

| 命令 | 说明 |
|---|---|
| `restart` | 软件复位 |
| `free` | 打印 internal / PSRAM 当前与最小空闲堆 |
| `tasks` | 打印 1 s 任务 CPU / 栈统计 |
| `log_level` | `log_level <tag\|*> <none\|error\|warn\|info\|debug\|verbose\|0-5>` |

### 日志输出

关键日志示例：

```
I (x) A2DP_2_BIS: Ready: pair a phone as A2DP source; audio is relayed to mono BIS
I (x) A2DP_BIS_BRIDGE: A2DP prepared rate=44100
I (x) A2DP_BIS_BRIDGE: BIS[0] prepared rate=48000
```

立体声 preset 会打印 `relayed to dual BIS`，并多一条 `BIS[1]`。

## 故障排查

- 手机搜不到设备：确认 Classic 可发现，以及 `CONFIG_BTDM_CTRL_MODE_BTDM=y`。
- 接收端无法同步：核对广播名/码，以及 `stream_num` 与所选 preset 一致（立体声为 2，单声道为 1）。
- 编译报 `button`/ADC 宏错误：本示例不依赖完整 `bt_audio` UI 栈，请保持 `idf_component.yml` 精简依赖。

## 技术支持

- [ESP-GMF issues](https://github.com/espressif/esp-gmf/issues)
- [ESP-ADF 论坛](https://esp32.com/)
