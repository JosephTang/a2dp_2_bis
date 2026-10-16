/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "esp_err.h"
#include "esp_bt_audio_stream.h"
#include "esp_gmf_pool.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_A2DP_BIS_LC3_PRESET_16KHZ_10MS_40B_HQ_MONO || CONFIG_A2DP_BIS_LC3_PRESET_48KHZ_10MS_155B_HQ_MONO
#define A2DP_BIS_BRIDGE_STREAM_COUNT  1
#else
#define A2DP_BIS_BRIDGE_STREAM_COUNT  2
#endif

esp_err_t a2dp_bis_bridge_init(esp_gmf_pool_handle_t pool);
void a2dp_bis_bridge_stream_state_chg(esp_bt_audio_stream_handle_t stream, esp_bt_audio_stream_state_t state);

#ifdef __cplusplus
}
#endif
