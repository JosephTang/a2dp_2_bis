/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_log.h"

#include "sdkconfig.h"

#include "esp_gmf_audio_dec.h"
#include "esp_gmf_audio_enc.h"
#if CONFIG_A2DP_BIS_PCM_CVT_ASRC
#include "esp_gmf_asrc.h"
#else
#include "esp_gmf_ch_cvt.h"
#include "esp_gmf_rate_cvt.h"
#endif
#include "esp_gmf_new_databus.h"
#include "esp_gmf_element.h"
#include "esp_gmf_io_bt.h"
#include "esp_gmf_obj.h"
#include "esp_gmf_pipeline.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_port.h"
#include "esp_gmf_task.h"

#include "esp_bt_audio_le.h"
#include "esp_bt_audio_stream.h"

#include "a2dp_bis_bridge.h"

static const char *TAG = "A2DP_BIS_BRIDGE";

#define BIS_NUM             A2DP_BIS_BRIDGE_STREAM_COUNT
#define CVT_WEIGHT_LEN      2
#define RB_BLOCKS           1
#define RB_BLOCK_SIZE       1024 * 1024
#define PORT_PAYLOAD        4096
#define WORKER_Q_LEN        16

typedef enum {
    ACT_LOAD = 0,
    ACT_RUN,
    ACT_STOP,
} act_t;

typedef struct {
    act_t                      act;
    esp_gmf_pipeline_handle_t  pipe;
} cmd_t;

typedef struct {
    esp_gmf_pipeline_handle_t     pipe;
    esp_gmf_task_handle_t         task;
    esp_gmf_db_handle_t           rb;
    esp_bt_audio_stream_handle_t  stream;
    float                         weight[CVT_WEIGHT_LEN];
    bool                          ready;
    bool                          started;
    bool                          running;
} leg_t;

typedef struct {
    esp_gmf_pipeline_handle_t     head_pipe;
    esp_gmf_task_handle_t         head_task;
    leg_t                         legs[BIS_NUM];
    esp_gmf_io_bt_tx_order_handle_t tx_order;
    esp_bt_audio_stream_handle_t  a2dp_stream;
    QueueHandle_t                 queue;
    bool                          bsrc_started;
    bool                          a2dp_started;
    bool                          head_loaded;
    bool                          head_running;
    bool                          inited;
    uint8_t                       bis_ready;
    uint8_t                       bis_started;
    esp_gmf_info_sound_t          a2dp_sound;
} bridge_t;

static bridge_t s_br;

static esp_audio_type_t to_audio_type(esp_bt_audio_stream_codec_type_t codec)
{
    switch (codec) {
        case ESP_BT_AUDIO_STREAM_CODEC_SBC:
            return ESP_AUDIO_TYPE_SBC;
        case ESP_BT_AUDIO_STREAM_CODEC_AAC:
            return ESP_AUDIO_TYPE_AAC;
        case ESP_BT_AUDIO_STREAM_CODEC_LC3:
            return ESP_AUDIO_TYPE_LC3;
        default:
            return ESP_AUDIO_TYPE_UNSUPPORT;
    }
}

static uint32_t lc3_rate(void)
{
#if CONFIG_A2DP_BIS_LC3_PRESET_48KHZ_10MS_100B_HQ || CONFIG_A2DP_BIS_LC3_PRESET_48KHZ_10MS_100B_HQ_MONO
    return 48000;
#else
    return 16000;
#endif
}

static esp_err_t post_cmd(act_t act, esp_gmf_pipeline_handle_t pipe)
{
    cmd_t cmd = { .act = act, .pipe = pipe };
    return (xQueueSend(s_br.queue, &cmd, pdMS_TO_TICKS(1000)) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void worker_task(void *arg)
{
    (void)arg;
    cmd_t cmd;
    while (xQueueReceive(s_br.queue, &cmd, portMAX_DELAY) == pdTRUE) {
        esp_gmf_err_t ret = ESP_GMF_ERR_OK;
        switch (cmd.act) {
            case ACT_LOAD:
                ret = esp_gmf_pipeline_loading_jobs(cmd.pipe);
                break;
            case ACT_RUN:
                ret = esp_gmf_pipeline_run(cmd.pipe);
                break;
            case ACT_STOP:
                ret = esp_gmf_pipeline_stop(cmd.pipe);
                if (ret == ESP_GMF_ERR_OK) {
                    ret = esp_gmf_pipeline_reset(cmd.pipe);
                }
                break;
            default:
                break;
        }
        if (ret != ESP_GMF_ERR_OK) {
            ESP_LOGE(TAG, "Pipeline action %d failed: 0x%x", cmd.act, ret);
        }
    }
}

static void fill_leg_weight(leg_t *leg, int slot)
{
#if A2DP_BIS_BRIDGE_STREAM_COUNT == 1
    (void)slot;
    leg->weight[0] = 0.5f;
    leg->weight[1] = 0.5f;
#else
    memset(leg->weight, 0, sizeof(leg->weight));
    leg->weight[slot == 0 ? 0 : 1] = 1.0f;
#endif
}

static void configure_leg_cvt(leg_t *leg, uint32_t rate, int slot)
{
    fill_leg_weight(leg, slot);
#if CONFIG_A2DP_BIS_PCM_CVT_ASRC
    esp_gmf_element_handle_t asrc = NULL;
    if (esp_gmf_pipeline_get_el_by_name(leg->pipe, "aud_asrc", &asrc) != ESP_GMF_ERR_OK || asrc == NULL) {
        ESP_LOGE(TAG, "aud_asrc missing");
        return;
    }
    esp_gmf_asrc_set_dest_rate(asrc, rate);
    esp_gmf_asrc_set_dest_ch(asrc, 1);
    esp_asrc_cfg_t *cfg = (esp_asrc_cfg_t *)OBJ_GET_CFG(asrc);
    if (cfg) {
        cfg->weight = leg->weight;
        cfg->weight_len = CVT_WEIGHT_LEN;
    }
#else
    esp_gmf_element_handle_t ch_cvt = NULL;
    esp_gmf_element_handle_t rate_cvt = NULL;
    if (esp_gmf_pipeline_get_el_by_name(leg->pipe, "aud_ch_cvt", &ch_cvt) != ESP_GMF_ERR_OK || ch_cvt == NULL) {
        ESP_LOGE(TAG, "aud_ch_cvt missing");
        return;
    }
    if (esp_gmf_pipeline_get_el_by_name(leg->pipe, "aud_rate_cvt", &rate_cvt) != ESP_GMF_ERR_OK || rate_cvt == NULL) {
        ESP_LOGE(TAG, "aud_rate_cvt missing");
        return;
    }
    esp_gmf_ch_cvt_set_dest_channel(ch_cvt, 1);
    esp_ae_ch_cvt_cfg_t *ch_cfg = (esp_ae_ch_cvt_cfg_t *)OBJ_GET_CFG(ch_cvt);
    if (ch_cfg) {
        ch_cfg->weight = leg->weight;
        ch_cfg->weight_len = CVT_WEIGHT_LEN;
    }
    esp_gmf_rate_cvt_set_dest_rate(rate_cvt, rate);
#endif
}

static void stop_all(void)
{
    if (s_br.bsrc_started) {
        esp_bt_audio_le_broadcast_source_stop();
        s_br.bsrc_started = false;
    }
    if (s_br.tx_order) {
        esp_gmf_io_bt_tx_order_stop(s_br.tx_order);
    }
    if (s_br.head_running) {
        post_cmd(ACT_STOP, s_br.head_pipe);
    }
    s_br.head_running = false;
    s_br.head_loaded = false;
    s_br.a2dp_started = false;
    for (int i = 0; i < BIS_NUM; i++) {
        if (s_br.legs[i].running) {
            post_cmd(ACT_STOP, s_br.legs[i].pipe);
        }
        s_br.legs[i].running = false;
        s_br.legs[i].ready = false;
        s_br.legs[i].started = false;
        s_br.legs[i].stream = NULL;
    }
    s_br.bis_ready = 0;
    s_br.bis_started = 0;
    memset(&s_br.a2dp_sound, 0, sizeof(s_br.a2dp_sound));
    s_br.a2dp_stream = NULL;
}

static void try_run(void)
{
    if (!s_br.a2dp_started || s_br.bis_ready < BIS_NUM ||
        s_br.bis_started < BIS_NUM || s_br.head_running) {
        return;
    }
    if (s_br.tx_order && esp_gmf_io_bt_tx_order_start(s_br.tx_order) != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "Failed to start BT TX ordering");
        return;
    }
    if (!s_br.head_loaded) {
        post_cmd(ACT_LOAD, s_br.head_pipe);
        s_br.head_loaded = true;
    }
    for (int i = 0; i < BIS_NUM; i++) {
        if (s_br.legs[i].ready && !s_br.legs[i].running) {
            post_cmd(ACT_RUN, s_br.legs[i].pipe);
            s_br.legs[i].running = true;
        }
    }
    post_cmd(ACT_RUN, s_br.head_pipe);
    s_br.head_running = true;
}

static void prepare_a2dp(esp_bt_audio_stream_handle_t stream)
{
    esp_bt_audio_stream_codec_info_t info = {0};
    if (esp_bt_audio_stream_get_codec_info(stream, &info) != ESP_OK) {
        ESP_LOGE(TAG, "A2DP codec info failed");
        return;
    }
    s_br.a2dp_stream = stream;
    esp_gmf_io_bt_set_stream(ESP_GMF_PIPELINE_GET_IN_INSTANCE(s_br.head_pipe), stream);

    esp_audio_simple_dec_cfg_t dec_cfg = {
        .dec_type = to_audio_type(info.codec_type),
        .dec_cfg = info.codec_cfg,
        .cfg_size = info.cfg_size,
        .use_frame_dec = true,
    };
    esp_gmf_audio_dec_reconfig(s_br.head_pipe->head_el, &dec_cfg);
    s_br.a2dp_sound.sample_rates = info.sample_rate;
    s_br.a2dp_sound.channels = __builtin_popcount(info.channels);
    s_br.a2dp_sound.bits = 16;
    ESP_LOGI(TAG, "A2DP prepared rate=%lu", (unsigned long)info.sample_rate);
}

static leg_t *find_bis_leg(esp_bt_audio_stream_handle_t stream)
{
    for (int i = 0; i < BIS_NUM; i++) {
        if (s_br.legs[i].stream == stream) {
            return &s_br.legs[i];
        }
    }
    return NULL;
}

static void prepare_bis(esp_bt_audio_stream_handle_t stream)
{
    int slot = -1;
    for (int i = 0; i < BIS_NUM; i++) {
        if (s_br.legs[i].stream == NULL) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        ESP_LOGE(TAG, "No free BIS slot");
        return;
    }

    esp_bt_audio_stream_codec_info_t info = {0};
    if (esp_bt_audio_stream_get_codec_info(stream, &info) != ESP_OK) {
        ESP_LOGE(TAG, "BIS codec info failed");
        return;
    }

    leg_t *leg = &s_br.legs[slot];
    leg->stream = stream;
    esp_gmf_io_bt_set_stream(ESP_GMF_PIPELINE_GET_OUT_INSTANCE(leg->pipe), stream);
    configure_leg_cvt(leg, info.sample_rate ? info.sample_rate : lc3_rate(), slot);

    esp_audio_enc_config_t enc_cfg = {
        .type = to_audio_type(info.codec_type),
        .cfg = info.codec_cfg,
        .cfg_sz = info.cfg_size,
    };
    esp_gmf_element_handle_t encoder = NULL;
    if (esp_gmf_pipeline_get_el_by_name(leg->pipe, "aud_enc", &encoder) != ESP_GMF_ERR_OK ||
        encoder == NULL) {
        ESP_LOGE(TAG, "aud_enc missing");
        leg->stream = NULL;
        return;
    }
    esp_gmf_audio_enc_reconfig(encoder, &enc_cfg);
    /*
     * The leg head is connected to another pipeline, so it receives sound
     * information asynchronously. Report the known decoded A2DP PCM format
     * now, allowing converter and encoder jobs to be registered before pipeline_run().
     */
    esp_gmf_pipeline_report_info(leg->pipe, ESP_GMF_INFO_SOUND,
                                 &s_br.a2dp_sound, sizeof(s_br.a2dp_sound));
    leg->ready = true;
    s_br.bis_ready++;
    ESP_LOGI(TAG, "BIS[%d] prepared rate=%lu", slot, (unsigned long)info.sample_rate);
}

esp_err_t a2dp_bis_bridge_init(esp_gmf_pool_handle_t pool,
                               esp_gmf_io_bt_tx_order_handle_t tx_order)
{
    ESP_RETURN_ON_FALSE(pool != NULL, ESP_ERR_INVALID_ARG, TAG, "pool is NULL");
#if A2DP_BIS_BRIDGE_STREAM_COUNT > 1
    ESP_RETURN_ON_FALSE(tx_order != NULL, ESP_ERR_INVALID_ARG, TAG, "tx_order is NULL");
#endif
    ESP_RETURN_ON_FALSE(!s_br.inited, ESP_ERR_INVALID_STATE, TAG, "already inited");

    memset(&s_br, 0, sizeof(s_br));
    s_br.tx_order = tx_order;
    s_br.queue = xQueueCreate(WORKER_Q_LEN, sizeof(cmd_t));
    ESP_RETURN_ON_FALSE(s_br.queue != NULL, ESP_ERR_NO_MEM, TAG, "queue alloc failed");

    const char *head_els[] = {"aud_dec", "copier"};
    ESP_RETURN_ON_FALSE(esp_gmf_pool_new_pipeline(pool, "io_bt", head_els,
                                                  sizeof(head_els) / sizeof(head_els[0]),
                                                  NULL, &s_br.head_pipe) == ESP_GMF_ERR_OK,
                        ESP_FAIL, TAG, "head pipeline failed");

    for (int i = 0; i < BIS_NUM; i++) {
#if CONFIG_A2DP_BIS_PCM_CVT_ASRC
        const char *leg_els[] = {
            "aud_asrc",
            "aud_enc",
        };
        const char *leg_in = "aud_asrc";
#else
        const char *leg_els[] = {
            "aud_ch_cvt",
            "aud_rate_cvt",
            "aud_enc",
        };
        const char *leg_in = "aud_ch_cvt";
#endif
        ESP_RETURN_ON_FALSE(esp_gmf_pool_new_pipeline(pool, NULL, leg_els,
                                                      sizeof(leg_els) / sizeof(leg_els[0]),
                                                      "io_bt", &s_br.legs[i].pipe) == ESP_GMF_ERR_OK,
                            ESP_FAIL, TAG, "leg pipeline %d failed", i);
        if (tx_order) {
            ESP_RETURN_ON_FALSE(
                esp_gmf_io_bt_set_tx_order(ESP_GMF_PIPELINE_GET_OUT_INSTANCE(s_br.legs[i].pipe),
                                           tx_order, i) == ESP_GMF_ERR_OK,
                ESP_FAIL, TAG, "set TX order for leg %d failed", i);
        }
        ESP_RETURN_ON_FALSE(esp_gmf_db_new_ringbuf(RB_BLOCKS, RB_BLOCK_SIZE, &s_br.legs[i].rb) == ESP_GMF_ERR_OK,
                            ESP_FAIL, TAG, "ringbuf %d failed", i);

        esp_gmf_port_handle_t out_port =
            NEW_ESP_GMF_PORT_OUT_BYTE(esp_gmf_db_acquire_write, esp_gmf_db_release_write,
                                      NULL, s_br.legs[i].rb, PORT_PAYLOAD, ESP_GMF_MAX_DELAY);
        esp_gmf_port_handle_t in_port =
            NEW_ESP_GMF_PORT_IN_BYTE(esp_gmf_db_acquire_read, esp_gmf_db_release_read,
                                     NULL, s_br.legs[i].rb, PORT_PAYLOAD, 1);
        ESP_RETURN_ON_FALSE(out_port && in_port, ESP_ERR_NO_MEM, TAG, "port alloc failed");
        ESP_RETURN_ON_FALSE(esp_gmf_pipeline_connect_pipe(s_br.head_pipe, "copier", out_port,
                                                          s_br.legs[i].pipe, leg_in, in_port) == ESP_GMF_ERR_OK,
                            ESP_FAIL, TAG, "connect leg %d failed", i);
    }

    esp_gmf_task_cfg_t head_cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    head_cfg.name = "a2dp_head";
    head_cfg.thread.core = 0;
    head_cfg.thread.prio = 14;
    head_cfg.thread.stack = 6144;
    head_cfg.thread.stack_in_ext = true;
    ESP_RETURN_ON_FALSE(esp_gmf_task_init(&head_cfg, &s_br.head_task) == ESP_GMF_ERR_OK,
                        ESP_FAIL, TAG, "head task failed");
    esp_gmf_pipeline_bind_task(s_br.head_pipe, s_br.head_task);

    for (int i = 0; i < BIS_NUM; i++) {
        esp_gmf_task_cfg_t leg_cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
        leg_cfg.name = (A2DP_BIS_BRIDGE_STREAM_COUNT == 1) ? "bis_m" : ((i == 0) ? "bis_l" : "bis_r");
        leg_cfg.thread.core = (i == 0) ? 1 : 0;
        leg_cfg.thread.prio = 15;
        leg_cfg.thread.stack = 6144;
        leg_cfg.thread.stack_in_ext = true;
        ESP_RETURN_ON_FALSE(esp_gmf_task_init(&leg_cfg, &s_br.legs[i].task) == ESP_GMF_ERR_OK,
                            ESP_FAIL, TAG, "leg task %d failed", i);
        esp_gmf_pipeline_bind_task(s_br.legs[i].pipe, s_br.legs[i].task);
    }

    ESP_RETURN_ON_FALSE(xTaskCreatePinnedToCore(worker_task, "a2dp_bis_w", 4096, NULL, 10, NULL, 0) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "worker failed");

    s_br.inited = true;
#if CONFIG_A2DP_BIS_PCM_CVT_ASRC
    ESP_LOGI(TAG, "Bridge initialized, BIS count=%d, pcm_cvt=asrc", BIS_NUM);
#else
    ESP_LOGI(TAG, "Bridge initialized, BIS count=%d, pcm_cvt=ch_cvt+rate_cvt", BIS_NUM);
#endif
    return ESP_OK;
}

void a2dp_bis_bridge_stream_state_chg(esp_bt_audio_stream_handle_t stream, esp_bt_audio_stream_state_t state)
{
    if (!s_br.inited || stream == NULL) {
        return;
    }

    esp_bt_audio_stream_dir_t dir = ESP_BT_AUDIO_STREAM_DIR_UNKNOWN;
    esp_bt_audio_stream_profile_t profile = ESP_BT_AUDIO_STREAM_PROFILE_UNKNOWN;
    esp_bt_audio_stream_get_dir(stream, &dir);
    esp_bt_audio_stream_get_profile(stream, &profile);

    ESP_LOGI(TAG, "stream %p state=%d dir=%d profile=%d", stream, (int)state, (int)dir, (int)profile);

    if (state == ESP_BT_AUDIO_STREAM_STATE_ALLOCATED) {
        if (dir == ESP_BT_AUDIO_STREAM_DIR_SINK && profile == ESP_BT_AUDIO_STREAM_PROFILE_CLASSIC_A2DP) {
            prepare_a2dp(stream);
        } else if (dir == ESP_BT_AUDIO_STREAM_DIR_SOURCE &&
                   profile == ESP_BT_AUDIO_STREAM_PROFILE_LE_BROADCAST) {
            prepare_bis(stream);
        }
    } else if (state == ESP_BT_AUDIO_STREAM_STATE_STARTED) {
        if (dir == ESP_BT_AUDIO_STREAM_DIR_SINK && profile == ESP_BT_AUDIO_STREAM_PROFILE_CLASSIC_A2DP) {
            s_br.a2dp_started = true;
            if (!s_br.bsrc_started) {
                if (esp_bt_audio_le_broadcast_source_start() == ESP_OK) {
                    s_br.bsrc_started = true;
                } else {
                    ESP_LOGE(TAG, "broadcast source start failed");
                }
            }
            try_run();
        } else if (dir == ESP_BT_AUDIO_STREAM_DIR_SOURCE &&
                   profile == ESP_BT_AUDIO_STREAM_PROFILE_LE_BROADCAST) {
            leg_t *leg = find_bis_leg(stream);
            if (leg && !leg->started) {
                leg->started = true;
                s_br.bis_started++;
            }
            try_run();
        }
    } else if (state == ESP_BT_AUDIO_STREAM_STATE_STOPPED ||
               state == ESP_BT_AUDIO_STREAM_STATE_RELEASED) {
        if (dir == ESP_BT_AUDIO_STREAM_DIR_SINK && profile == ESP_BT_AUDIO_STREAM_PROFILE_CLASSIC_A2DP) {
            stop_all();
        }
    }
}
