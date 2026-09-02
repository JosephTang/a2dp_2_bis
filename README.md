# A2DP Sink to Dual BIS

[中文](./README_CN.md)

- Regular Example: ⭐⭐

## Example Brief

- Relays Classic A2DP Sink PCM from a phone into one LE Audio Broadcast Source BIG. Default is two BIS streams (front left / front right). menuconfig can select 16 kHz or 48 kHz mono with a single BIS.
- Uses `esp_bt_audio` plus GMF pipelines: `io_bt` (A2DP) → `aud_dec` → `copier` → one or two legs (`aud_ch_cvt` → `aud_rate_cvt` → `aud_enc` → `io_bt`). Dual-BIS writers submit packets in configured stream order; controller flow control remains in `esp_bt_audio`. No local I2S playback.

### Typical Scenarios

- Phone music over A2DP, shared to Auracast / BIS headphones or a broadcast sink board.
- Dual-mode BTDM demos on ESP32-S31 Korvo-class boards without SD-card media.

### File Structure

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

## Environment Setup

### Hardware Required

- ESP32-S31 board with PSRAM (default board: `esp32_s31_korvo_1`)
- Phone that can play A2DP
- LE Audio broadcast sink (ESP board running BMR / BIS sink, or compatible earbuds)

### Default IDF Branch

ESP32-S31 needs an IDF tree with preview target support (for example an internal/preview IDF that lists `esp32s31`). Use:

```
idf.py --preview set-target esp32s31
```

### Software Requirements

- Local `esp-gmf` checkout providing `esp_bt_audio` (wired via `override_path` in `main/idf_component.yml`)
- `esp_bt_audio` on a branch that includes BMS-only link guards (no PACS/unicast required for this example)

## Build and Flash

### Build Preparation

```
cd /path/to/esp-idf
./install.sh
. ./export.sh
cd /path/to/a2dp_2_bis
idf.py --preview set-target esp32s31
idf.py build
idf.py -p PORT flash monitor
```

### Configuration

menuconfig highlights:

- `A2DP to Dual BIS Example` → device / broadcast name, broadcast code, LC3 preset (16/48 kHz stereo or mono)
- Bluetooth → LE Audio → `CONFIG_BT_BAP_BROADCAST_SRC_STREAM_COUNT=2` (stack maximum; actual BIS count follows the LC3 preset)
- Keep Bluedroid BTDM enabled (Classic + LE)

Default LC3 preset is `16 kHz / 10 ms / 40 B / HQ, stereo (1 BIG / 2 BIS)` to match the IDF `a2dp_sink_broadcast_source` sample. Mono presets mix A2DP stereo down to one BIS.

## How to Use

1. Flash and reset the board; it advertises Classic name / LE broadcast name from Kconfig.
2. Connect a phone and play music (A2DP Sink).
3. On A2DP audio start, the app starts the LE broadcast source and bridges audio into one or two BIS streams according to the selected preset.
4. Sync a BIS sink to the broadcast name (and code if set).
5. Use the UART console prompt `A2DP-2-BIS >` for sys commands:

| Command | Usage |
|---|---|
| `restart` | Software reset |
| `free` | Print internal / PSRAM free and minimum heap |
| `tasks` | Print 1 s task CPU / stack stats |
| `log_level` | `log_level <tag\|*> <none\|error\|warn\|info\|debug\|verbose\|0-5>` |

### Log Output

Key lines to expect:

```
I (x) A2DP_2_BIS: Ready: pair a phone as A2DP source; audio is relayed to dual BIS
I (x) A2DP_BIS_BRIDGE: A2DP prepared rate=44100
I (x) A2DP_BIS_BRIDGE: BIS[0] prepared rate=16000
I (x) A2DP_BIS_BRIDGE: BIS[1] prepared rate=16000
```

Mono preset logs one `BIS[0]` line and `relayed to mono BIS`.

## Troubleshooting

- Phone cannot find the device: confirm Classic scan mode and BTDM target; check `CONFIG_BTDM_CTRL_MODE_BTDM=y`.
- Sink cannot sync: verify broadcast name/code and that `stream_num` matches the selected preset (2 for stereo, 1 for mono).
- Build fails on `button` / ADC macros: this example does not use the full `bt_audio` UI stack; keep dependencies slim as in `idf_component.yml`.

## Technical Support

- [ESP-GMF issues](https://github.com/espressif/esp-gmf/issues)
- [ESP-ADF forum](https://esp32.com/)
