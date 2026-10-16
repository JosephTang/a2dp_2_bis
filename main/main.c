/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include "esp_bt.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "esp_gmf_pool.h"

#include "esp_bt_audio.h"
#include "esp_bt_audio_classic.h"
#include "esp_bt_audio_defs.h"
#include "esp_bt_audio_event.h"
#include "esp_bt_audio_host.h"

#include "a2dp_bis_bridge.h"
#include "cmd_reg.h"
#include "pool_reg.h"
#include "sdkconfig.h"

#define LE_SRC_SEND_TASK_CORE_ID       0
#define LE_SRC_SEND_TASK_PRIO          16
#define LE_SRC_SEND_TASK_STACK_SIZE    4096

static const char *TAG = "A2DP_2_BIS";

static void bytes_from_string(const char *str, uint8_t *out, size_t out_len)
{
    memset(out, 0, out_len);
    if (str == NULL) {
        return;
    }
    memcpy(out, str, strnlen(str, out_len));
}

static void bt_audio_event_cb(esp_bt_audio_event_t event, void *event_data, void *user_data)
{
    (void)user_data;
    switch (event) {
        case ESP_BT_AUDIO_EVENT_CONNECTION_STATE_CHG: {
            esp_bt_audio_event_connection_st_t *st = event_data;
            ESP_LOGI(TAG, "Connection tech=%d connected=%d", (int)st->tech, st->connected);
            if (st->tech == ESP_BT_AUDIO_TECH_CLASSIC) {
                if (st->connected) {
                    esp_bt_audio_classic_set_scan_mode(false, false);
                } else {
                    esp_bt_audio_classic_set_scan_mode(true, true);
                }
            }
            break;
        }
        case ESP_BT_AUDIO_EVENT_STREAM_STATE_CHG: {
            esp_bt_audio_event_stream_st_t *st = event_data;
            a2dp_bis_bridge_stream_state_chg(st->stream_handle, st->state);
            break;
        }
        default:
            break;
    }
}

static void fill_bt_config(esp_bt_audio_config_t *cfg)
{
    cfg->classic.roles = ESP_BT_AUDIO_CLASSIC_ROLE_A2DP_SNK | ESP_BT_AUDIO_CLASSIC_ROLE_AVRC_TG;

    /* BMS-only: do not use TMAP (needs CAP initiator/acceptor). Load broadcast source by LE role. */
    cfg->le.user_case = ESP_BT_AUDIO_LE_USER_CASE_UNKNOWN;
    cfg->le.roles = ESP_BT_AUDIO_LE_ROLE_BROADCAST_SOURCE;
    cfg->le.snk_cnt = 0;
    cfg->le.src_cnt = 0;
    cfg->le.src_send_task_core_id = LE_SRC_SEND_TASK_CORE_ID;
    cfg->le.src_send_task_prio = LE_SRC_SEND_TASK_PRIO;
    cfg->le.src_send_task_stack_size = LE_SRC_SEND_TASK_STACK_SIZE;
    cfg->le.pacs.sink_enabled = 0;
    cfg->le.pacs.source_enabled = 0;
    cfg->le.vcp_rend.volume = 50;
    cfg->le.vcp_rend.mute = 0;
    cfg->le.vcp_rend.step = 10;

    snprintf((char *)cfg->le.bsrc.broadcast_name, sizeof(cfg->le.bsrc.broadcast_name),
             "%s", CONFIG_A2DP_BIS_DEVICE_NAME);
    bytes_from_string(CONFIG_A2DP_BIS_BROADCAST_CODE, cfg->le.bsrc.broadcast_code,
                      sizeof(cfg->le.bsrc.broadcast_code));
    cfg->le.bsrc.stream_num = A2DP_BIS_BRIDGE_STREAM_COUNT;
#if CONFIG_A2DP_BIS_LC3_PRESET_16KHZ_10MS_40B_HQ_MONO
    cfg->le.bsrc.stream_locations[0] = 0;
    cfg->le.bsrc.lc3_preset = ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_16KHZ_10MS_40B_HQ;
#elif CONFIG_A2DP_BIS_LC3_PRESET_48KHZ_10MS_155B_HQ_MONO
    cfg->le.bsrc.stream_locations[0] = 0;
    cfg->le.bsrc.lc3_preset = ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_48KHZ_10MS_155B_HQ;
#else
    cfg->le.bsrc.stream_locations[0] = ESP_BT_AUDIO_AUDIO_LOC_FRONT_LEFT;
    cfg->le.bsrc.stream_locations[1] = ESP_BT_AUDIO_AUDIO_LOC_FRONT_RIGHT;
    cfg->le.bsrc.lc3_preset = ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_16KHZ_10MS_40B_HQ;
#endif
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    esp_gmf_pool_handle_t pool = NULL;
    ESP_ERROR_CHECK(esp_gmf_pool_init(&pool));
    ESP_ERROR_CHECK(pool_reg(pool));
    ESP_ERROR_CHECK(a2dp_bis_bridge_init(pool));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BTDM));

    esp_bt_audio_host_bluedroid_cfg_t host_cfg = ESP_BT_AUDIO_HOST_BLUEDROID_CFG_DEFAULT();
    snprintf(host_cfg.dev_name, sizeof(host_cfg.dev_name), "%s", CONFIG_A2DP_BIS_DEVICE_NAME);

    esp_bt_audio_config_t bt_config = {
        .host_config = &host_cfg,
        .event_cb = bt_audio_event_cb,
        .event_user_ctx = NULL,
    };
    fill_bt_config(&bt_config);

    ESP_LOGI(TAG, "Device/broadcast name: %s", CONFIG_A2DP_BIS_DEVICE_NAME);
    ESP_LOGI(TAG, "BIS stream count: %u", bt_config.le.bsrc.stream_num);
    ESP_LOGI(TAG, "LC3 preset: %d", (int)bt_config.le.bsrc.lc3_preset);

    ESP_ERROR_CHECK(esp_bt_audio_init(&bt_config));
    ESP_ERROR_CHECK(esp_bt_audio_classic_set_scan_mode(true, true));
    cli_init();
#if A2DP_BIS_BRIDGE_STREAM_COUNT > 1
    ESP_LOGI(TAG, "Ready: pair a phone as A2DP source; audio is relayed to dual BIS");
#else
    ESP_LOGI(TAG, "Ready: pair a phone as A2DP source; audio is relayed to mono BIS");
#endif
}
