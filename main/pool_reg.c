/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_gmf_pool.h"
#include "esp_gmf_err.h"
#include "esp_log.h"

#include "sdkconfig.h"

#include "esp_audio_enc_default.h"
#include "esp_audio_dec_default.h"
#include "esp_audio_simple_dec_default.h"
#include "esp_gmf_audio_dec.h"
#include "esp_gmf_audio_enc.h"
#if CONFIG_A2DP_BIS_PCM_CVT_ASRC
#include "esp_gmf_asrc.h"
#else
#include "esp_gmf_ch_cvt.h"
#include "esp_gmf_rate_cvt.h"
#endif
#include "esp_gmf_copier.h"
#include "esp_gmf_io_bt.h"

#include "a2dp_bis_bridge.h"
#include "pool_reg.h"

static const char *TAG = "POOL_REG";

esp_gmf_err_t pool_reg(esp_gmf_pool_handle_t pool)
{
    ESP_GMF_NULL_CHECK(TAG, pool, return ESP_GMF_ERR_INVALID_ARG);

    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_element_handle_t element = NULL;
    esp_gmf_io_handle_t io = NULL;

    ret = esp_audio_dec_register_default();
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register audio decoders");
    ret = esp_audio_simple_dec_register_default();
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register audio simple decoders");
    ret = esp_audio_enc_register_default();
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register audio encoders");

    esp_audio_simple_dec_cfg_t dec_cfg = DEFAULT_ESP_GMF_AUDIO_DEC_CONFIG();
    ret = esp_gmf_audio_dec_init(&dec_cfg, &element);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to init audio decoder");
    ret = esp_gmf_pool_register_element(pool, element, NULL);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register audio decoder");

    esp_audio_enc_config_t enc_cfg = DEFAULT_ESP_GMF_AUDIO_ENC_CONFIG();
    ret = esp_gmf_audio_enc_init(&enc_cfg, &element);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to init audio encoder");
    ret = esp_gmf_pool_register_element(pool, element, NULL);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register audio encoder");

#if CONFIG_A2DP_BIS_PCM_CVT_ASRC
    esp_asrc_cfg_t asrc_cfg = DEFAULT_ESP_GMF_ASRC_CONFIG();
    ret = esp_gmf_asrc_init(&asrc_cfg, &element);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to init ASRC");
    ret = esp_gmf_pool_register_element(pool, element, NULL);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register ASRC");
#else
    esp_ae_ch_cvt_cfg_t ch_cfg = DEFAULT_ESP_GMF_CH_CVT_CONFIG();
    ret = esp_gmf_ch_cvt_init(&ch_cfg, &element);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to init channel converter");
    ret = esp_gmf_pool_register_element(pool, element, NULL);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register channel converter");

    esp_ae_rate_cvt_cfg_t rate_cfg = DEFAULT_ESP_GMF_RATE_CVT_CONFIG();
    ret = esp_gmf_rate_cvt_init(&rate_cfg, &element);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to init rate converter");
    ret = esp_gmf_pool_register_element(pool, element, NULL);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register rate converter");
#endif

    esp_gmf_copier_cfg_t copier_cfg = {
        .copy_num = A2DP_BIS_BRIDGE_STREAM_COUNT,
    };
    ret = esp_gmf_copier_init(&copier_cfg, &element);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to init copier");
    ret = esp_gmf_pool_register_element(pool, element, NULL);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register copier");

    bt_io_cfg_t bt_rx_cfg = ESP_GMF_BT_IO_CFG_DEFAULT();
    bt_rx_cfg.dir = ESP_GMF_IO_DIR_READER;
    ret = esp_gmf_io_bt_init(&bt_rx_cfg, &io);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to init BT reader");
    ret = esp_gmf_pool_register_io(pool, io, NULL);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register BT reader");

    bt_io_cfg_t bt_tx_cfg = ESP_GMF_BT_IO_CFG_DEFAULT();
    bt_tx_cfg.dir = ESP_GMF_IO_DIR_WRITER;
    ret = esp_gmf_io_bt_init(&bt_tx_cfg, &io);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to init BT writer");
    ret = esp_gmf_pool_register_io(pool, io, NULL);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register BT writer");

#if A2DP_BIS_BRIDGE_STREAM_COUNT > 1
    ESP_LOGI(TAG, "GMF pool registered for A2DP to dual BIS");
#else
    ESP_LOGI(TAG, "GMF pool registered for A2DP to mono BIS");
#endif
    return ESP_GMF_ERR_OK;
}
